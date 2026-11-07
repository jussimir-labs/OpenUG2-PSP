//! One OS-owned lease for the complete launcher/conversion process family.
//! No PID/env assertion grants authority: children receive the same open file
//! object, validate it, and retain it until their work has finished.
use serde::{Deserialize, Serialize};
use std::{
    env,
    fs::{self, File, OpenOptions},
    io::{self, IsTerminal, Read, Write},
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    sync::Arc,
};

pub const BUSY: i32 = 76;
const LOCK: &str = "launcher.lock";
const PIPE: &str = "NFSU2_ROOT_LEASE_PIPE";

#[derive(Serialize, Deserialize)]
struct Handoff {
    handle: u64,
    #[serde(default)]
    input: Option<u64>,
    #[serde(default)]
    input_terminal: bool,
    #[serde(default)]
    input_console: bool,
    #[serde(default)]
    session: Option<serde_json::Value>,
    #[serde(default)]
    active: Vec<PathBuf>,
}

pub struct RootLease {
    file: File,
    root: PathBuf,
    active: Vec<PathBuf>,
}

fn open_exclusive(path: &Path) -> io::Result<File> {
    let mut opts = OpenOptions::new();
    opts.read(true).write(true).create(true).truncate(false);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        opts.share_mode(0);
    }
    let f = opts.open(path)?;
    #[cfg(unix)]
    {
        use std::os::fd::AsRawFd;
        if unsafe { libc::flock(f.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } != 0 {
            return Err(io::Error::last_os_error());
        }
    }
    Ok(f)
}

impl RootLease {
    pub fn enter(root: &Path) -> Result<(Arc<Self>, Option<serde_json::Value>), String> {
        let root = root.canonicalize().map_err(|e| e.to_string())?;
        let path = root.join(".updates").join(LOCK);
        if env::var(PIPE).is_ok_and(|v| v == "1") {
            env::remove_var(PIPE);
            if path
                .parent()
                .unwrap()
                .canonicalize()
                .map_err(|e| e.to_string())?
                != root.join(".updates")
                || fs::symlink_metadata(&path)
                    .map_err(|e| e.to_string())?
                    .file_type()
                    .is_symlink()
            {
                return Err("The generator state folder must not be linked".into());
            }
            let mut data = Vec::new();
            io::stdin()
                .take(32769)
                .read_to_end(&mut data)
                .map_err(|e| e.to_string())?;
            if data.len() > 32768 {
                return Err("Invalid launcher handoff".into());
            }
            let h: Handoff =
                serde_json::from_slice(&data).map_err(|_| "Invalid launcher handoff")?;
            let file = adopt(h.handle, &path).map_err(|e| format!("Invalid root lease: {e}"))?;
            #[cfg(windows)]
            if h.input_console {
                restore_console_input().map_err(|e| e.to_string())?;
            }
            if let Some(input) = h.input {
                restore_input(input).map_err(|e| e.to_string())?;
            }
            if h.input.is_some() || h.input_console {
                if io::stdin().is_terminal() != h.input_terminal {
                    return Err("The delegated input terminal could not be restored".into());
                }
            }
            let mut active = h.active;
            // Paths are used only to PRESERVE ancestors, never as authority.
            active.retain(|p| p.parent() == Some(&root.join(".updates")));
            return Ok((Arc::new(Self { file, root, active }), h.session));
        }
        fs::create_dir_all(path.parent().unwrap()).map_err(|e| e.to_string())?;
        if path
            .parent()
            .unwrap()
            .canonicalize()
            .map_err(|e| e.to_string())?
            != root.join(".updates")
            || fs::symlink_metadata(&path).is_ok_and(|m| m.file_type().is_symlink())
        {
            return Err("The generator state folder must not be linked".into());
        }
        let file = open_exclusive(&path).map_err(|e| {
            if e.kind() == io::ErrorKind::WouldBlock || e.raw_os_error().is_some_and(|n| [32, 33].contains(&n)) {
                "BUSY: The generator or an update is already running in this folder. Close it before starting another.".to_string()
            } else { format!("Cannot protect the generator folder ({e}). Nothing was started.") }
        })?;
        let mut bundles = vec![root.clone()];
        if let Ok(entries) = fs::read_dir(root.join(".updates")) {
            for e in entries.flatten() {
                if e.file_type().is_ok_and(|t| t.is_dir()) && e.path().join("bundle.json").is_file()
                {
                    bundles.push(e.path());
                }
            }
        }
        for bundle in bundles {
            let old = bundle.join(".tools/browser-generator.lock");
            if old.is_file() {
                let f = OpenOptions::new()
                    .read(true)
                    .write(true)
                    .open(&old)
                    .map_err(|e| e.to_string())?;
                legacy_lock(&f).map_err(|_| {
                    "BUSY: A previous generator is still open. Close it before updating."
                        .to_string()
                })?;
                // Detect an already active OLD generator at admission. It
                // cannot inherit our protocol; all old/direct instances must
                // be closed before migration. We do not claim to exclude an
                // old binary started after this finite check.
                drop(f);
            }
        }
        Ok((
            Arc::new(Self {
                file,
                root,
                active: Vec::new(),
            }),
            None,
        ))
    }
    pub fn owns(&self, root: &Path) -> bool {
        self.root == root
    }
    pub fn owns_version_dir(&self, dir: &Path) -> bool {
        dir.parent() == Some(&self.root.join(".updates"))
    }
    pub fn protects(&self, dir: &Path) -> bool {
        self.active.iter().any(|p| p == dir)
    }
    pub fn identity(&self) -> io::Result<String> {
        #[cfg(unix)]
        {
            use std::os::unix::fs::MetadataExt;
            let m = self.file.metadata()?;
            Ok(format!("{}:{}", m.dev(), m.ino()))
        }
        #[cfg(windows)]
        {
            use std::os::windows::io::AsRawHandle;
            use windows_sys::Win32::Storage::FileSystem::{
                GetFileInformationByHandle, BY_HANDLE_FILE_INFORMATION,
            };
            let mut m: BY_HANDLE_FILE_INFORMATION = unsafe { std::mem::zeroed() };
            if unsafe { GetFileInformationByHandle(self.file.as_raw_handle() as _, &mut m) } == 0 {
                return Err(io::Error::last_os_error());
            }
            Ok(format!(
                "{}:{}",
                m.dwVolumeSerialNumber,
                ((m.nFileIndexHigh as u64) << 32) | m.nFileIndexLow as u64
            ))
        }
    }

    /// The bundled Python keeps the same lock object open for as long as it
    /// lives, without any protocol of its own: its converters do not inherit
    /// it, and it ends with the interpreter.
    pub fn spawn_runtime(&self, cmd: &mut Command) -> io::Result<Child> {
        #[cfg(unix)]
        {
            use std::os::fd::AsRawFd;
            use std::os::unix::process::CommandExt;
            let fd = self.file.as_raw_fd();
            unsafe {
                cmd.pre_exec(move || {
                    if libc::fcntl(fd, libc::F_SETFD, 0) < 0 {
                        return Err(io::Error::last_os_error());
                    }
                    Ok(())
                });
            }
            cmd.spawn()
        }
        #[cfg(windows)]
        {
            use std::os::windows::io::AsRawHandle;
            let mut child = cmd.spawn()?;
            if let Err(e) = duplicate(self.file.as_raw_handle() as _, child.as_raw_handle() as _) {
                let _ = child.kill();
                let _ = child.wait();
                return Err(e);
            }
            Ok(child)
        }
    }

    /// Same object, no unlock/relock gap. Windows duplicates directly into the
    /// newly spawned child, which is blocked reading this anonymous pipe.
    pub fn spawn(
        &self,
        cmd: &mut Command,
        terminal: bool,
        session: Option<serde_json::Value>,
        here: &Path,
    ) -> io::Result<Child> {
        cmd.env(PIPE, "1")
            .env("NFSU2_ROOT_LEASE_ROOT", &self.root)
            .stdin(Stdio::piped());
        let mut active = self.active.clone();
        if here.parent() == Some(&self.root.join(".updates"))
            && !active.contains(&here.to_path_buf())
        {
            active.push(here.to_path_buf());
        }
        #[cfg(unix)]
        let (handle, input, saved) = {
            use std::os::fd::{AsRawFd, FromRawFd};
            use std::os::unix::process::CommandExt;
            let saved = if terminal {
                let fd = unsafe { libc::fcntl(0, libc::F_DUPFD_CLOEXEC, 3) };
                if fd < 0 {
                    return Err(io::Error::last_os_error());
                }
                Some(unsafe { File::from_raw_fd(fd) })
            } else {
                None
            };
            let fd = self.file.as_raw_fd();
            let input = saved.as_ref().map(|f| f.as_raw_fd());
            unsafe {
                cmd.pre_exec(move || {
                    for d in std::iter::once(fd).chain(input) {
                        if libc::fcntl(d, libc::F_SETFD, 0) < 0 {
                            return Err(io::Error::last_os_error());
                        }
                    }
                    Ok(())
                });
            }
            (fd as u64, input.map(|d| d as u64), saved)
        };
        let mut child = cmd.spawn()?;
        #[cfg(unix)]
        let input_console = false;
        #[cfg(windows)]
        let transferred = (|| -> io::Result<(u64, Option<u64>, bool)> {
            use std::os::windows::io::AsRawHandle;
            let handle = duplicate(self.file.as_raw_handle() as _, child.as_raw_handle() as _)?;
            let mut input_console = false;
            let input = if terminal {
                use windows_sys::Win32::System::Console::{
                    GetConsoleMode, GetStdHandle, STD_INPUT_HANDLE,
                };
                let input = unsafe { GetStdHandle(STD_INPUT_HANDLE) };
                if input.is_null() || input == windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE
                {
                    None // Desktop tasks can have no console input at all.
                } else {
                    let mut mode = 0;
                    input_console = unsafe { GetConsoleMode(input, &mut mode) } != 0;
                    // Microsoft only permits duplicating console handles for
                    // the SAME process. The child inherits our console and
                    // reopens CONIN$; ordinary file/pipe inputs are duplicated.
                    if input_console {
                        None
                    } else {
                        Some(duplicate(input, child.as_raw_handle() as _)?)
                    }
                }
            } else {
                None
            };
            Ok((handle, input, input_console))
        })();
        #[cfg(windows)]
        let (handle, input, input_console) = match transferred {
            Ok(h) => h,
            Err(e) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(e);
            }
        };
        let result = (|| {
            let data = serde_json::to_vec(&Handoff {
                handle,
                input,
                input_terminal: terminal && io::stdin().is_terminal(),
                input_console,
                session,
                active,
            })
            .map_err(io::Error::other)?;
            child
                .stdin
                .take()
                .ok_or_else(|| io::Error::other("Missing root lease pipe"))?
                .write_all(&data)
        })();
        #[cfg(unix)]
        drop(saved);
        if let Err(e) = result {
            let _ = child.kill();
            let _ = child.wait();
            return Err(e);
        }
        Ok(child)
    }
}

#[cfg(unix)]
fn adopt(raw: u64, path: &Path) -> io::Result<File> {
    use std::os::{
        fd::{AsRawFd, FromRawFd},
        unix::fs::MetadataExt,
    };
    let fd = i32::try_from(raw).map_err(|_| io::Error::other("Invalid lease fd"))?;
    if fd < 3 || unsafe { libc::fcntl(fd, libc::F_GETFD) } < 0 {
        return Err(io::Error::other("Invalid lease fd"));
    }
    let f = unsafe { File::from_raw_fd(fd) };
    let (a, b) = (f.metadata()?, fs::metadata(path)?);
    if !a.is_file() || (a.dev(), a.ino()) != (b.dev(), b.ino()) {
        return Err(io::Error::other("Lease belongs to another folder"));
    }
    if unsafe { libc::flock(f.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } != 0 {
        return Err(io::Error::last_os_error());
    }
    if unsafe { libc::fcntl(fd, libc::F_SETFD, libc::FD_CLOEXEC) } < 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(f)
}
#[cfg(windows)]
fn adopt(raw: u64, path: &Path) -> io::Result<File> {
    use std::os::windows::io::FromRawHandle;
    use windows_sys::Win32::Storage::FileSystem::GetFinalPathNameByHandleW;
    if raw == 0 || raw == u64::MAX {
        return Err(io::Error::other("Invalid lease handle"));
    }
    let mut buf = vec![0u16; 32768];
    let n = unsafe {
        GetFinalPathNameByHandleW(raw as usize as _, buf.as_mut_ptr(), buf.len() as u32, 0)
    } as usize;
    if n == 0 || n >= buf.len() {
        return Err(io::Error::other("Invalid lease handle"));
    }
    if !String::from_utf16_lossy(&buf[..n]).eq_ignore_ascii_case(&path.to_string_lossy()) {
        return Err(io::Error::other("Lease belongs to another folder"));
    }
    let f = unsafe { File::from_raw_handle(raw as usize as _) };
    if !f.metadata()?.is_file() {
        return Err(io::Error::other("Invalid lease handle"));
    }
    // A shared/nonexclusive handle cannot be used as a forged grant.
    match open_exclusive(path) {
        Err(e) if e.raw_os_error() == Some(32) => Ok(f),
        _ => Err(io::Error::other("Lease is not exclusive")),
    }
}
#[cfg(windows)]
fn duplicate(
    source: windows_sys::Win32::Foundation::HANDLE,
    target: windows_sys::Win32::Foundation::HANDLE,
) -> io::Result<u64> {
    use windows_sys::Win32::{
        Foundation::{DuplicateHandle, DUPLICATE_SAME_ACCESS},
        System::Threading::GetCurrentProcess,
    };
    let mut out = std::ptr::null_mut();
    if unsafe {
        DuplicateHandle(
            GetCurrentProcess(),
            source,
            target,
            &mut out,
            0,
            0,
            DUPLICATE_SAME_ACCESS,
        )
    } == 0
    {
        return Err(io::Error::last_os_error());
    }
    Ok(out as usize as u64)
}
#[cfg(unix)]
fn restore_input(raw: u64) -> io::Result<()> {
    let fd = i32::try_from(raw).map_err(|_| io::Error::other("Invalid input fd"))?;
    if fd < 3 || unsafe { libc::dup2(fd, 0) } < 0 {
        return Err(io::Error::last_os_error());
    }
    unsafe {
        libc::close(fd);
    }
    Ok(())
}
#[cfg(windows)]
fn restore_input(raw: u64) -> io::Result<()> {
    use windows_sys::Win32::System::Console::{SetStdHandle, STD_INPUT_HANDLE};
    if unsafe { SetStdHandle(STD_INPUT_HANDLE, raw as usize as _) } == 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}
#[cfg(windows)]
fn restore_console_input() -> io::Result<()> {
    use std::os::windows::{
        fs::OpenOptionsExt,
        io::{AsRawHandle, IntoRawHandle},
    };
    use windows_sys::Win32::System::Console::{SetStdHandle, STD_INPUT_HANDLE};
    let input = OpenOptions::new()
        .read(true)
        .write(true)
        .share_mode(3)
        .open("CONIN$")?;
    if unsafe { SetStdHandle(STD_INPUT_HANDLE, input.as_raw_handle() as _) } == 0 {
        return Err(io::Error::last_os_error());
    }
    let _ = input.into_raw_handle(); // The standard input owns this process-lifetime handle.
    Ok(())
}
fn legacy_lock(f: &File) -> io::Result<()> {
    #[cfg(unix)]
    {
        use std::os::fd::AsRawFd;
        if unsafe { libc::flock(f.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } != 0 {
            return Err(io::Error::last_os_error());
        }
    }
    #[cfg(windows)]
    {
        use std::os::windows::io::AsRawHandle;
        use windows_sys::Win32::Storage::FileSystem::{
            LockFileEx, LOCKFILE_EXCLUSIVE_LOCK, LOCKFILE_FAIL_IMMEDIATELY,
        };
        let mut overlap = unsafe { std::mem::zeroed() };
        if unsafe {
            LockFileEx(
                f.as_raw_handle() as _,
                LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                0,
                1,
                0,
                &mut overlap,
            )
        } == 0
        {
            return Err(io::Error::last_os_error());
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn excludes_another_instance_but_not_another_root() {
        let root = env::temp_dir().join(format!("nfsu2-rootlease-roots-{}", std::process::id()));
        fs::create_dir_all(root.join("other")).unwrap();
        let (guard, _) = RootLease::enter(&root).unwrap();
        assert!(RootLease::enter(&root).err().unwrap().starts_with("BUSY:"));
        let other = RootLease::enter(&root.join("other")).unwrap().0;
        assert_ne!(guard.identity().unwrap(), other.identity().unwrap());
        drop(guard);
        // A child another test is forking may hold a copy for a moment until
        // its exec closes it; the launcher never re-enters in one process.
        let again = (0..50).any(|_| {
            RootLease::enter(&root).is_ok()
                || {
                    std::thread::sleep(std::time::Duration::from_millis(20));
                    false
                }
        });
        assert!(again);
    }

    #[test]
    fn handoff_restores_actual_stdin_reads() {
        if let Ok(root) = env::var("NFSU2_LEASE_STDIN_TEST") {
            let (_guard, session) = RootLease::enter(Path::new(&root)).unwrap();
            assert!(session.is_none());
            let mut line = String::new();
            io::stdin().read_line(&mut line).unwrap();
            assert_eq!(line, "delegated input\n");
            return;
        }
        let root = env::temp_dir().join(format!("nfsu2-rootlease-stdin-{}", std::process::id()));
        fs::create_dir_all(&root).unwrap();
        let root = root.canonicalize().unwrap();
        fs::write(root.join("input"), b"delegated input\n").unwrap();
        let input = File::open(root.join("input")).unwrap();
        let (guard, _) = RootLease::enter(&root).unwrap();
        #[cfg(unix)]
        let restore = {
            use std::os::fd::{AsRawFd, FromRawFd};
            let fd = unsafe { libc::fcntl(0, libc::F_DUPFD_CLOEXEC, 3) };
            assert!(fd >= 3);
            let original = unsafe { File::from_raw_fd(fd) };
            assert!(unsafe { libc::dup2(input.as_raw_fd(), 0) } >= 0);
            original
        };
        #[cfg(windows)]
        let restore = {
            use std::os::windows::io::AsRawHandle;
            use windows_sys::Win32::System::Console::{
                GetStdHandle, SetStdHandle, STD_INPUT_HANDLE,
            };
            let original = unsafe { GetStdHandle(STD_INPUT_HANDLE) };
            assert_ne!(
                unsafe { SetStdHandle(STD_INPUT_HANDLE, input.as_raw_handle() as _) },
                0
            );
            original
        };
        let mut cmd = Command::new(env::current_exe().unwrap());
        cmd.args([
            "--exact",
            "lease::tests::handoff_restores_actual_stdin_reads",
            "--nocapture",
        ])
        .env("NFSU2_LEASE_STDIN_TEST", &root)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
        let child = guard.spawn(&mut cmd, true, None, &root);
        #[cfg(unix)]
        {
            use std::os::fd::AsRawFd;
            assert!(unsafe { libc::dup2(restore.as_raw_fd(), 0) } >= 0);
        }
        #[cfg(windows)]
        {
            use windows_sys::Win32::System::Console::{SetStdHandle, STD_INPUT_HANDLE};
            assert_ne!(unsafe { SetStdHandle(STD_INPUT_HANDLE, restore) }, 0);
        }
        let output = child.unwrap().wait_with_output().unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
    }
}
