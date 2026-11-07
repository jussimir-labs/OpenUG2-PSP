#[path = "../src/lease.rs"]
mod lease;
// Test stand-in for the bundled Python (`native/tests/test_launcher.py`):
// reports what the launcher passed, then behaves like the helper's start.
use std::{env, fs, io::{Read, Write}, net::TcpListener, time::{Duration, Instant}};

fn main() {
    // The runtime inherits the folder lock: nobody else can take it now.
    let launcher = env::var("NFSU2_LAUNCHER_ROOT").unwrap();
    let held = lease::RootLease::enter(std::path::Path::new(&launcher))
        .err()
        .is_some_and(|e| e.starts_with("BUSY:"));
    assert!(held, "the generator folder is not locked while the runtime runs");
    let root = env::var("NFSU2_BUNDLE_ROOT").unwrap_or_default();
    let bundle = fs::read_to_string(std::path::Path::new(&root).join("bundle.json")).unwrap_or_default();
    let version = bundle
        .split("\"generator_version\": \"")
        .nth(1)
        .and_then(|s| s.split('"').next())
        .unwrap_or("?")
        .to_string();
    let get = |k: &str| env::var(k).unwrap_or_default();
    println!(
        "STUB version={version} updated_from={} launcher_root={} state={}",
        get("NFSU2_UPDATED_FROM"),
        get("NFSU2_LAUNCHER_ROOT"),
        get("NFSU2_LAUNCHER_STATE")
    );
    assert!(env::var_os("NFSU2_SESSION_STDIN").is_none(), "session handoff leaked to runtime");
    assert!(!env::vars().any(|(_, v)| v.len() == 29 && v.starts_with("test-")), "GitHub token leaked through environment");
    if get("STUB_FAIL_VERSION") == version {
        eprintln!("STUB failing on purpose");
        std::process::exit(3);
    }
    if env::args().any(|a| a == "--probe") {
        return;
    }
    if get("STUB_NO_READY_VERSION") == version {
        println!("NFSU2_GENERATOR_URL=http://127.0.0.1:1/#token={}", "a".repeat(64));
        return; // Even a zero exit must not confirm a UI that never opened.
    }
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    listener.set_nonblocking(true).unwrap();
    println!("NFSU2_GENERATOR_URL=http://{}/#token={}", listener.local_addr().unwrap(), "a".repeat(64));
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline {
        if let Ok((mut stream, _)) = listener.accept() {
            stream.set_read_timeout(Some(Duration::from_secs(1))).unwrap();
            let mut buf = [0; 4096];
            let _ = stream.read(&mut buf);
            let body = "{\"status\":\"idle\",\"busy\":false}";
            write!(stream, "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}", body.len()).unwrap();
            if let Ok(release) = env::var("STUB_HOLD_READY") {
                let release = std::path::PathBuf::from(release);
                let marker = release.with_extension("ready");
                fs::write(marker, serde_json::to_vec(&serde_json::json!({"pid":std::process::id(),"lease_held":held,"version":version})).unwrap()).unwrap();
                let limit = Instant::now() + Duration::from_secs(20);
                while !release.exists() && Instant::now() < limit { std::thread::sleep(Duration::from_millis(20)); }
                assert!(release.exists(), "owned ready barrier timed out");
            }
            return;
        }
        std::thread::sleep(Duration::from_millis(10));
    }
}
