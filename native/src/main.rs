//! Offline CLI supervisor. The browser UI and conversion engine use only the
//! private runtime shipped beside this executable. No setup/package manager.
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeMap,
    env, fs,
    io::{self, BufRead, Read},
    path::{Component, Path, PathBuf},
    process::{self, Command, Stdio},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread,
    time::{Duration, Instant},
};

#[derive(Deserialize)]
struct Manifest {
    schema: u32,
    platform: String,
    python: String,
    files: BTreeMap<String, String>,
}
fn safe_path(root: &Path, relative: &str) -> Result<PathBuf, String> {
    let p = Path::new(relative);
    if p.components().any(|x| !matches!(x, Component::Normal(_)))
        || relative.is_empty()
        || relative.contains('\\')
    {
        return Err(format!("Invalid bundle path: {relative}"));
    }
    let resolved = root.join(p).canonicalize().map_err(|_| {
        format!("Bundle file missing: {relative}. Extract the complete download again.")
    })?;
    if !resolved.starts_with(root) {
        return Err(format!("Bundle path escapes its folder: {relative}"));
    }
    Ok(resolved)
}
fn platform() -> &'static str {
    if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        "macos-arm64"
    } else if cfg!(all(target_os = "macos", target_arch = "x86_64")) {
        "macos-x64"
    } else if cfg!(all(target_os = "windows", target_arch = "x86_64")) {
        "windows-x64"
    } else {
        "unsupported"
    }
}
fn verify(root: &Path) -> Result<Manifest, String> {
    let data = fs::read(root.join("bundle.json")).map_err(|_| {
        "bundle.json missing. Keep the executable inside the complete extracted package."
            .to_string()
    })?;
    let m: Manifest =
        serde_json::from_slice(&data).map_err(|e| format!("Invalid bundle manifest: {e}"))?;
    if m.schema != 1 || m.platform != platform() {
        return Err(format!(
            "This bundle targets {}, this computer needs {}.",
            m.platform,
            platform()
        ));
    }
    for required in [
        m.python.as_str(),
        "generator.py",
        "build_game.py",
        "tools/native_runtime.py",
        "web/index.html",
        "web/app.js",
        "web/style.css",
        "eboot/EBOOT.PBP",
        "eboot/release.json",
    ] {
        if !m.files.contains_key(required) {
            return Err(format!("Incomplete bundle manifest: {required}"));
        }
    }
    for name in [
        "ffmpeg",
        "lame",
        "cjpegli",
        "movie_audio",
        "eaxa2pcm",
        "mt2pcm",
        "xaencode",
    ] {
        let rel = format!(
            "runtime/bin/{name}{}",
            if cfg!(windows) { ".exe" } else { "" }
        );
        if !m.files.contains_key(&rel) {
            return Err(format!("Converter missing from manifest: {rel}"));
        }
    }
    for (name, expected) in &m.files {
        let path = safe_path(root, name)?;
        let mut f = fs::File::open(path).map_err(|e| e.to_string())?;
        let mut h = Sha256::new();
        let mut buf = [0u8; 65536];
        loop {
            let n = f.read(&mut buf).map_err(|e| e.to_string())?;
            if n == 0 {
                break;
            }
            h.update(&buf[..n]);
        }
        if format!("{:x}", h.finalize()) != *expected {
            return Err(format!(
                "Bundle file changed or corrupted: {name}. Extract the original package again."
            ));
        }
    }
    Ok(m)
}
fn local_url(s: &str) -> bool {
    let Some(rest) = s.strip_prefix("http://127.0.0.1:") else {
        return false;
    };
    let Some((port, token)) = rest.split_once("/#token=") else {
        return false;
    };
    port.parse::<u16>().is_ok_and(|p| p != 0)
        && token.len() == 64
        && token.bytes().all(|b| b.is_ascii_hexdigit())
}
fn browser(url: &str) -> io::Result<()> {
    if !local_url(url) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "Invalid local URL",
        ));
    }
    #[cfg(target_os = "macos")]
    {
        Command::new("/usr/bin/open").arg(url).spawn()?;
    }
    #[cfg(windows)]
    {
        Command::new("rundll32.exe")
            .arg("url.dll,FileProtocolHandler")
            .arg(url)
            .spawn()?;
    }
    Ok(())
}
fn runtime_command(root: &Path, m: &Manifest) -> Result<Command, String> {
    let mut cmd = Command::new(safe_path(root, &m.python)?);
    cmd.current_dir(root)
        .env("NFSU2_BUNDLE_ROOT", root)
        .env("PYTHONNOUSERSITE", "1")
        .env("PYTHONDONTWRITEBYTECODE", "1")
        .env("PYTHONUTF8", "1")
        .env("PYTHONUNBUFFERED", "1")
        .env("NFSU2_MAC_SLOTS", "0");
    for key in [
        "PYTHONPATH",
        "PYTHONHOME",
        "VIRTUAL_ENV",
        "NFSU2_DIST",
        "NFSU2_CJPEGLI",
        "NFSU2_MAC_SLOTS_DIR",
        "DYLD_LIBRARY_PATH",
        "LD_LIBRARY_PATH",
    ] {
        cmd.env_remove(key);
    }
    // Keep OS utilities available for native dialogs/browser launch, but bundled
    // converters themselves are resolved by absolute paths, never global PATH.
    let mut paths = vec![root.join("runtime/bin")];
    if let Some(p) = env::var_os("PATH") {
        paths.extend(env::split_paths(&p));
    }
    cmd.env("PATH", env::join_paths(paths).map_err(|e| e.to_string())?);
    Ok(cmd)
}
#[cfg(windows)]
struct Job(windows_sys::Win32::Foundation::HANDLE);
#[cfg(windows)]
impl Job {
    fn attach(child: &process::Child) -> Result<Self, String> {
        use std::os::windows::io::AsRawHandle;
        use windows_sys::Win32::System::JobObjects::*;
        unsafe {
            let handle = CreateJobObjectW(std::ptr::null(), std::ptr::null());
            if handle.is_null() {
                return Err(io::Error::last_os_error().to_string());
            }
            let job = Self(handle);
            let mut limits: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if SetInformationJobObject(
                handle,
                JobObjectExtendedLimitInformation,
                &limits as *const _ as *const _,
                std::mem::size_of_val(&limits) as u32,
            ) == 0
                || AssignProcessToJobObject(handle, child.as_raw_handle() as _) == 0
            {
                return Err(io::Error::last_os_error().to_string());
            }
            Ok(job)
        }
    }
}
#[cfg(windows)]
impl Drop for Job {
    fn drop(&mut self) {
        unsafe {
            windows_sys::Win32::Foundation::CloseHandle(self.0);
        }
    }
}
fn main_result() -> Result<i32, String> {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.iter().any(|a| a == "--help" || a == "-h") {
        println!("NFSU2 PSP native helper\n\n  nfsu2-generator [serve] [--no-browser] [--port PORT]\n  nfsu2-generator doctor\n\nKeep this executable in the extracted bundle. No Python/WSL/Homebrew installation is needed.\nThe ISO and generated game files stay on this computer.");
        return Ok(0);
    }
    let mut mode = "serve";
    let mut no_browser = false;
    let mut port = None;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "serve" => mode = "serve",
            "doctor" => mode = "doctor",
            "--no-browser" => no_browser = true,
            "--port" => {
                i += 1;
                port = Some(
                    args.get(i)
                        .ok_or("Missing port")?
                        .parse::<u16>()
                        .map_err(|_| "Invalid port")?,
                );
            }
            a => return Err(format!("Unknown argument: {a}")),
        };
        i += 1;
    }
    let root = env::current_exe()
        .map_err(|e| e.to_string())?
        .parent()
        .ok_or("Missing bundle directory")?
        .canonicalize()
        .map_err(|e| e.to_string())?;
    println!("Checking the included runtime and converters...");
    let m = verify(&root)?;
    let mut cmd = runtime_command(&root, &m)?;
    cmd.arg(root.join("generator.py"));
    if mode == "doctor" {
        let status = cmd.arg("--probe").status().map_err(|e| e.to_string())?;
        return Ok(status.code().unwrap_or(1));
    }
    cmd.arg("--no-browser");
    if let Some(p) = port {
        cmd.args(["--port", &p.to_string()]);
    }
    cmd.stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit());
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        cmd.process_group(0);
    }
    let interrupted = Arc::new(AtomicBool::new(false));
    let flag = interrupted.clone();
    ctrlc::set_handler(move || flag.store(true, Ordering::SeqCst)).map_err(|e| e.to_string())?;
    let mut child = cmd
        .spawn()
        .map_err(|e| format!("Included Python could not start: {e}"))?;
    #[cfg(windows)]
    let _job = match Job::attach(&child) {
        Ok(j) => j,
        Err(e) => {
            let _ = child.kill();
            return Err(e);
        }
    };
    let stdout = child.stdout.take().ok_or("No helper output")?;
    thread::spawn(move || {
        for line in io::BufReader::new(stdout).lines().map_while(Result::ok) {
            if let Some(url) = line.strip_prefix("NFSU2_GENERATOR_URL=") {
                if local_url(url) {
                    println!("Local interface: {url}");
                    if !no_browser {
                        if let Err(e) = browser(url) {
                            eprintln!("Open the local URL above in your browser ({e}).")
                        }
                    }
                }
            } else {
                println!("{line}")
            }
        }
    });
    let mut stopping = None;
    loop {
        if let Some(status) = child.try_wait().map_err(|e| e.to_string())? {
            return Ok(status.code().unwrap_or(130));
        }
        if interrupted.load(Ordering::SeqCst) {
            let since = stopping.get_or_insert_with(|| {
                eprintln!("Closing the local helper and its conversion processes...");
                #[cfg(unix)]
                unsafe {
                    libc::kill(-(child.id() as i32), libc::SIGINT);
                }
                #[cfg(windows)]
                {
                    let _ = child.kill();
                }
                Instant::now()
            });
            if since.elapsed() > Duration::from_secs(8) {
                #[cfg(unix)]
                unsafe {
                    libc::kill(-(child.id() as i32), libc::SIGKILL);
                }
                let _ = child.kill();
                let _ = child.wait();
                return Ok(130);
            }
        }
        thread::sleep(Duration::from_millis(100));
    }
}
fn main() {
    match main_result() {
        Ok(code) => process::exit(code),
        Err(e) => {
            eprintln!("NFSU2 PSP: {e}");
            process::exit(1)
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn only_local_session_urls() {
        assert!(local_url(&format!(
            "http://127.0.0.1:4173/#token={}",
            "a".repeat(64)
        )));
        for s in [
            "https://example.com/",
            "http://127.0.0.1:0/#token=x",
            "http://127.0.0.1:2/#token=abc",
        ] {
            assert!(!local_url(s))
        }
    }
    #[test]
    fn rejects_bundle_path_traversal() {
        let root = env::temp_dir().canonicalize().unwrap();
        for s in [
            "../etc/passwd",
            "/etc/passwd",
            "runtime/../../outside",
            "runtime\\python.exe",
            "",
        ] {
            assert!(safe_path(&root, s).is_err())
        }
    }
}
