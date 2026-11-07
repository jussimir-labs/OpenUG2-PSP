//! CLI supervisor and launcher. The browser UI and conversion engine use only
//! the private runtime shipped beside this executable. No setup/package manager.
//! At each start it offers the newest beta release and installs it beside
//! this package (see `layout`).
mod config;
mod credentials;
mod github;
mod install;
mod layout;
mod lease;
mod semver;
mod updater;

use layout::{helper_name, Layout, Target, RESTART};
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
        Arc, OnceLock,
    },
    thread,
    time::{Duration, Instant},
};

#[derive(Deserialize)]
struct Manifest {
    schema: u32,
    platform: String,
    python: String,
    #[serde(default)]
    generator_version: String,
    #[serde(default)]
    launcher_protocol: u32,
    files: BTreeMap<String, String>,
}
fn file_sha256(path: &Path) -> io::Result<String> {
    let mut f = fs::File::open(path)?;
    let mut h = Sha256::new();
    let mut buf = [0u8; 65536];
    loop {
        let n = f.read(&mut buf)?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    Ok(format!("{:x}", h.finalize()))
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
pub(crate) fn platform() -> &'static str {
    if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        "macos-arm64"
    } else if cfg!(all(target_os = "macos", target_arch = "x86_64")) {
        "macos-x64"
    } else if cfg!(all(target_os = "windows", target_arch = "x86_64")) {
        "windows-x64"
    } else if cfg!(all(target_os = "linux", target_arch = "x86_64")) {
        "linux-x64"
    } else {
        "unsupported"
    }
}
pub(crate) fn verify(root: &Path) -> Result<Manifest, String> {
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
        if file_sha256(&path).map_err(|e| e.to_string())? != *expected {
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
fn interface_ready(url: &str) -> bool {
    if !local_url(url) {
        return false;
    }
    let Some((base, token)) = url.split_once("/#token=") else { return false };
    let agent = ureq::AgentBuilder::new().redirects(0).timeout(Duration::from_secs(2)).build();
    agent.get(&format!("{base}/api/state")).set("X-NFSU2-Token", token).call()
        .ok().and_then(|r| r.into_string().ok())
        .and_then(|s| serde_json::from_str::<serde_json::Value>(&s).ok())
        .is_some_and(|v| v.get("status").is_some_and(|s| s.is_string()) && v.get("busy").is_some_and(|b| b.is_boolean()))
}
fn browser(url: &str) -> io::Result<()> {
    if !local_url(url) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "Invalid local URL",
        ));
    }
    open_url(url)
}
/// GitHub's sign-in page, the only remote page the launcher opens.
pub(crate) fn open_web(url: &str) -> io::Result<()> {
    if url != format!("{}/login/device", config::WEB) {
        return Err(io::Error::new(io::ErrorKind::InvalidInput, "Unexpected address"));
    }
    open_url(url)
}
fn open_url(url: &str) -> io::Result<()> {
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
    #[cfg(target_os = "linux")]
    {
        // The desktop's default browser; xdg-open is part of every desktop
        // (KDE on SteamOS too), gio is GNOME's own opener.
        let mut last = io::Error::new(io::ErrorKind::NotFound, "xdg-open not found");
        for (program, args) in [("xdg-open", &[][..]), ("gio", &["open"][..])] {
            match Command::new(program)
                .args(args)
                .arg(url)
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
            {
                Ok(_) => return Ok(()),
                Err(e) => last = e,
            }
        }
        return Err(last);
    }
    #[allow(unreachable_code)]
    Ok(())
}
fn runtime_command(root: &Path, m: &Manifest, layout: &Layout) -> Result<Command, String> {
    layout.authority().map_err(|e| e.to_string())?;
    // Record the first generation even when update checks were skipped or
    // unavailable. A future launcher can then offer to update that game.
    fs::create_dir_all(&layout.updates).map_err(|e| format!("Cannot create the generator state folder ({e})"))?;
    let mut cmd = Command::new(safe_path(root, &m.python)?);
    cmd.current_dir(root)
        .env("NFSU2_BUNDLE_ROOT", root)
        // Outputs and the last generation are kept beside the extracted
        // package, so an update never moves or deletes them.
        .env("NFSU2_LAUNCHER_ROOT", &layout.root)
        .env("NFSU2_LAUNCHER_STATE", &layout.updates)
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
        "NFSU2_UPDATED_FROM",
        "NFSU2_SESSION_STDIN",
        "DYLD_LIBRARY_PATH",
        "LD_LIBRARY_PATH",
        "LD_PRELOAD",
        "WSL_DISTRO_NAME",
        "WSL_INTEROP",
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
static INTERRUPTS: OnceLock<Arc<AtomicBool>> = OnceLock::new();
/// Ctrl+C is handled from here on: the helper stops its children itself.
fn interrupts() -> Result<Arc<AtomicBool>, String> {
    if let Some(f) = INTERRUPTS.get() {
        return Ok(f.clone());
    }
    let flag = Arc::new(AtomicBool::new(false));
    let f = flag.clone();
    ctrlc::set_handler(move || f.store(true, Ordering::SeqCst)).map_err(|e| e.to_string())?;
    let _ = INTERRUPTS.set(flag.clone());
    Ok(flag)
}
pub(crate) fn interrupted() -> bool {
    INTERRUPTS.get().is_some_and(|f| f.load(Ordering::SeqCst))
}
#[derive(Clone, Copy, PartialEq)]
enum Mode {
    Serve,
    Doctor,
    Update,
    Login,
    Logout,
}
#[derive(Clone)]
struct Args {
    mode: Mode,
    no_browser: bool,
    port: Option<u16>,
    no_update: bool,
    yes: bool,
    raw: Vec<String>,
}
const HELP: &str = "NFSU2 PSP native helper

  nfsu2-generator [serve] [--no-browser] [--port PORT] [--no-update] [--yes]
  nfsu2-generator doctor
  nfsu2-generator update [--yes]
  nfsu2-generator login | logout

Keep this executable in the extracted bundle. No Python/WSL/Homebrew/distribution package is needed.
The ISO and generated game files stay on this computer.
At each start it offers the newest beta release (`--no-update` skips the check); `--yes`
accepts without asking. The GitHub sign-in is kept in this computer's password store;
`logout` removes it.";
fn parse_args() -> Result<Option<Args>, String> {
    let raw: Vec<String> = env::args().skip(1).collect();
    if raw.iter().any(|a| a == "--help" || a == "-h") {
        println!("{HELP}");
        return Ok(None);
    }
    let mut a = Args { mode: Mode::Serve, no_browser: false, port: None, no_update: false, yes: false, raw: raw.clone() };
    let mut i = 0;
    while i < raw.len() {
        match raw[i].as_str() {
            "serve" => a.mode = Mode::Serve,
            "doctor" => a.mode = Mode::Doctor,
            "update" => a.mode = Mode::Update,
            "login" => a.mode = Mode::Login,
            "logout" => a.mode = Mode::Logout,
            "--no-browser" => a.no_browser = true,
            "--no-update" => a.no_update = true,
            "--yes" => a.yes = true,
            "--port" => {
                i += 1;
                a.port = Some(
                    raw.get(i)
                        .ok_or("Missing port")?
                        .parse::<u16>()
                        .map_err(|_| "Invalid port")?,
                );
            }
            x => return Err(format!("Unknown argument: {x}")),
        };
        i += 1;
    }
    Ok(Some(a))
}
fn bundle_version(root: &Path) -> Option<semver::Version> {
    #[derive(Deserialize)]
    struct V {
        generator_version: String,
    }
    let v: V = serde_json::from_slice(&fs::read(root.join("bundle.json")).ok()?).ok()?;
    semver::Version::parse(&v.generator_version)
}
/// Runs an installed version. Its executable must be the one its own
/// `bundle.json` lists, which was checked against the release on install.
fn delegate(layout: &Layout, here: &Path, dir: &Path, args: &[String], skip_update: bool) -> Result<i32, String> {
    layout.authority().map_err(|e| e.to_string())?;
    if !layout.lease.owns_version_dir(dir) { return Err("The selected version belongs to another folder".into()); }
    let exe = dir.join(helper_name());
    let manifest = fs::read(dir.join("bundle.json")).ok()
        .and_then(|d| serde_json::from_slice::<Manifest>(&d).ok())
        .ok_or("its manifest cannot be read")?;
    if manifest.launcher_protocol != 2 { return Err("its launcher does not support the protected handoff".into()); }
    let listed = manifest.files.get(&helper_name()).cloned();
    if listed.is_none() || listed != file_sha256(&exe).ok() {
        return Err("its files changed".into());
    }
    // Ctrl+C reaches the version that runs: it stops its own children.
    interrupts()?;
    let session = credentials::session();
    let mut command = Command::new(&exe);
    command.args(args)
        .current_dir(dir)
        .env("NFSU2_LAUNCHER_ROOT", &layout.root)
        .env("NFSU2_SKIP_UPDATE", if skip_update { "1" } else { "0" });
    let session = session.map(serde_json::to_value).transpose().map_err(|_| "Cannot pass the session sign-in")?;
    let mut child = layout.lease.spawn(&mut command, true, session, here).map_err(|e| e.to_string())?;
    #[cfg(windows)]
    let _job = match Job::attach(&child) {
        Ok(j) => j,
        Err(e) => {
            let _ = child.kill();
            let _ = child.wait();
            return Err(e);
        }
    };
    let status = child.wait().map_err(|e| e.to_string())?;
    Ok(status.code().unwrap_or(130))
}
fn main_result() -> Result<i32, String> {
    let Some(mut args) = parse_args()? else {
        return Ok(0);
    };
    // After going back from a version that did not start, this launch does
    // not offer it again.
    args.no_update |= env::var("NFSU2_SKIP_UPDATE").is_ok_and(|v| v == "1");
    let here = env::current_exe()
        .map_err(|e| e.to_string())?
        .parent()
        .ok_or("Missing bundle directory")?
        .canonicalize()
        .map_err(|e| e.to_string())?;
    // Installed helpers are entered only through the root's verified handoff.
    // Derive the root from the canonical layout, never from an env assertion.
    let installed_root = here.parent().filter(|p| p.file_name().is_some_and(|n| n == layout::UPDATES))
        .and_then(|p| p.parent()).filter(|r| r.join("bundle.json").is_file())
        .filter(|_| here.file_name().and_then(|n| n.to_str()).is_some_and(|n| semver::Version::parse(n).is_some()));
    if installed_root.is_some() && env::var("NFSU2_ROOT_LEASE_PIPE").as_deref() != Ok("1") {
        return Err("Open nfsu2-generator from the extracted package's root folder.".into());
    }
    let root = installed_root.unwrap_or(&here);
    let (guard, session) = lease::RootLease::enter(root)?;
    credentials::accept_session(session)?;
    let layout = Layout::new(root, guard);
    if installed_root.is_some() { return run_package(&here, &layout, &args, true); }
    // Administrative commands authenticate through the extracted package;
    // they do not boot, count attempts, confirm, clean or roll back a pending
    // installed runtime. Only a real probe/ready interface can confirm it.
    if matches!(args.mode, Mode::Login | Mode::Logout) {
        return run_package(&here, &layout, &args, false);
    }
    let Some(own) = bundle_version(&here) else {
        return run_package(&here, &layout, &args, false);
    };
    for _ in 0..4 {
        let rolled_back = |layout: &Layout, args: &mut Args, why: &str| {
            let _ = layout.roll_back_current(why);
            args.no_update = true;
        };
        let target = layout.resolve(&own).unwrap_or_else(|e| {
            eprintln!("The installed updates could not be read ({e}).");
            Target::Root
        });
        let code = match target {
            Target::Root => run_package(&here, &layout, &args, false)?,
            Target::Version(dir) => {
                let name = dir.file_name().unwrap_or_default().to_string_lossy().into_owned();
                match delegate(&layout, &here, &dir, &args.raw, args.no_update) {
                    Ok(code) => {
                        let pending = layout.pointer().is_some_and(|p| p.version == name && p.state == "pending");
                        if pending && ![RESTART, 130].contains(&code) {
                            rolled_back(&layout, &mut args, "did not open");
                            continue;
                        }
                        code
                    }
                    Err(e) => {
                        rolled_back(&layout, &mut args, &format!("cannot start ({e})"));
                        continue;
                    }
                }
            }
        };
        if code != RESTART {
            return Ok(code);
        }
    }
    Ok(1)
}
fn run_package(here: &Path, layout: &Layout, args: &Args, delegated: bool) -> Result<i32, String> {
    if args.mode == Mode::Logout {
        updater::logout(here, layout)?;
        return Ok(0);
    }
    println!("Checking the included runtime and converters...");
    let m = verify(here)?;
    // The version name this one has among the installed updates.
    let mine = delegated.then(|| m.generator_version.clone());
    let updated_from = layout
        .pointer()
        .filter(|p| Some(&p.version) == mine.as_ref() && p.state == "pending")
        .map(|p| p.from);
    if let Some(from) = &updated_from {
        println!("Updated to v{} (from v{from}).", m.generator_version);
    }
    if args.mode == Mode::Login {
        updater::explicit_login(here, layout)?;
        return Ok(0);
    }
    // A version that was just installed does not check again.
    if updated_from.is_none() && !args.no_update && matches!(args.mode, Mode::Update | Mode::Serve) {
        let opts = updater::Options { interactive: updater::interactive(), yes: args.yes, explicit: args.mode == Mode::Update };
        match updater::check(here, layout, &m.generator_version, &opts) {
            Ok(updater::Outcome::Installed) => {
                // Keep the session alive through the version switch, including
                // when this process itself was delegated by an older launcher.
                let p = layout.pointer().ok_or("The installed update was not selected")?;
                let result = delegate(layout, here, &layout.version_dir(&p.version), &args.raw, true);
                if matches!(result, Ok(130)) || layout.pointer().is_some_and(|selected| selected.version == p.version && selected.state == "confirmed") {
                    return result;
                }
                layout.roll_back_current("did not open").map_err(|e| e.to_string())?;
                let mut retry = args.clone();
                retry.no_update = true;
                return run_package(here, layout, &retry, delegated);
            }
            Ok(updater::Outcome::Current) => {}
            Err(e) => println!("Update check skipped: {e}"),
        }
    }
    if args.mode == Mode::Update {
        if updated_from.is_some() {
            let mut probe = runtime_command(here, &m, layout)?;
            probe.arg(here.join("generator.py")).arg("--probe");
            let status = layout.lease.spawn_runtime(&mut probe).map_err(|e| e.to_string())?.wait().map_err(|e| e.to_string())?;
            if !status.success() {
                return Ok(status.code().unwrap_or(1));
            }
            layout.confirm(mine.as_deref());
        }
        return Ok(0);
    }
    let mut cmd = runtime_command(here, &m, layout)?;
    if let Some(from) = &updated_from {
        cmd.env("NFSU2_UPDATED_FROM", from);
    }
    cmd.arg(here.join("generator.py"));
    if args.mode == Mode::Doctor {
        cmd.arg("--probe");
        let status = layout.lease.spawn_runtime(&mut cmd).map_err(|e| e.to_string())?.wait().map_err(|e| e.to_string())?;
        if status.success() {
            layout.confirm(mine.as_deref());
        }
        return Ok(status.code().unwrap_or(1));
    }
    cmd.arg("--no-browser");
    if let Some(p) = args.port {
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
    let interrupted = interrupts()?;
    let mut child = layout.lease.spawn_runtime(&mut cmd)
        .map_err(|e| format!("Included Python could not start: {e}"))?;
    #[cfg(windows)]
    let _job = match Job::attach(&child) {
        Ok(j) => j,
        Err(e) => {
            let _ = child.kill();
            let _ = child.wait();
            return Err(e);
        }
    };
    let stdout = child.stdout.take().ok_or("No helper output")?;
    let no_browser = args.no_browser;
    let opened = Layout::new(&layout.root, layout.lease.clone());
    let output = thread::spawn(move || {
        for line in io::BufReader::new(stdout).lines().map_while(Result::ok) {
            if let Some(url) = line.strip_prefix("NFSU2_GENERATOR_URL=") {
                if interface_ready(url) {
                    println!("Local interface: {url}");
                    // This version opened: the previous one is no longer needed.
                    opened.confirm(mine.as_deref());
                    if !no_browser {
                        if let Err(e) = browser(url) {
                            eprintln!("Open the local URL above in your browser ({e}).")
                        }
                    }
                } else {
                    eprintln!("The local interface did not answer; this update is not confirmed.");
                }
            } else {
                println!("{line}")
            }
        }
    });
    let mut stopping = None;
    loop {
        if let Some(status) = child.try_wait().map_err(|e| e.to_string())? {
            let _ = output.join();
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
            process::exit(if e.starts_with("BUSY:") { lease::BUSY } else { 1 })
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
