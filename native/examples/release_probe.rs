//! Maintainer transport proof only: real Client/Vault, explicit published tag,
//! metadata first, then a separately admitted size-capped ZIP/checksum transfer.
//! Never installs, stages, changes a package version or reads gh credentials.
#[path = "../src/config.rs"]
mod config;
#[path = "../src/credentials.rs"]
mod credentials;
#[path = "../src/github.rs"]
mod github;
#[path = "../src/lease.rs"]
mod lease;
#[path = "../src/semver.rs"]
mod semver;
use std::{
    env, fs,
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::Duration,
};

fn run() -> Result<(), String> {
    let a: Vec<String> = env::args().skip(1).collect();
    if a.len() != 6 {
        return Err(
            "Usage: release_probe CONFIG_ROOT QA_ROOT PLATFORM TAG metadata|download MAX_BYTES"
                .into(),
        );
    }
    let mode = a[4].as_str();
    if !matches!(mode, "metadata" | "download") {
        return Err("Expected metadata or download".into());
    }
    let max: u64 = a[5].parse().map_err(|_| "Invalid byte limit")?;
    if !matches!(
        a[2].as_str(),
        "macos-arm64" | "macos-x64" | "windows-x64" | "linux-x64"
    ) {
        return Err("Unsupported platform".into());
    }
    let root = PathBuf::from(&a[0]);
    let qa = PathBuf::from(&a[1]);
    let cfg = config::load(&root)?.ok_or("Missing update configuration")?;
    if cfg.test
        || cfg.client_id != "Iv23liOsr0uvsTwiZDZG"
        || cfg.repository != "jussimir-labs/OpenUG2-PSP"
    {
        return Err("The real proof requires the reviewed App/repository configuration".into());
    }
    fs::create_dir_all(&qa).map_err(|e| e.to_string())?;
    let (_guard, _) = lease::RootLease::enter(&qa)?;
    let cancelled = Arc::new(AtomicBool::new(false));
    let signal = cancelled.clone();
    ctrlc::set_handler(move || signal.store(true, Ordering::SeqCst)).map_err(|e| e.to_string())?;
    let client = github::Client::new(cfg);
    let vault = credentials::Vault::new(&client.config.repository, false);
    // Metadata mode deliberately proves this App's Device Flow. Do not tee
    // stdout: only its user_code is displayed; device_code/token stay in RAM.
    let saved = if mode == "metadata" {
        None
    } else {
        vault
            .load()
            .map_err(|_| "Cannot read the system password store")?
    };
    let token = if let Some(t) = saved {
        if t.expires_at
            .is_some_and(|at| at <= github::now().saturating_add(60))
        {
            let refresh = t
                .refresh_token
                .as_deref()
                .ok_or("Run the metadata sign-in again")?;
            match client.refresh(refresh) {
                Ok(t) => {
                    vault
                        .save(&t)
                        .map_err(|_| "Cannot retain the renewed sign-in")?;
                    t
                }
                Err(github::RefreshError::Rejected) => {
                    return Err("The saved sign-in was rejected; run metadata sign-in again".into())
                }
                Err(github::RefreshError::Unavailable(_)) => {
                    return Err("Renewal unavailable; saved sign-in preserved".into())
                }
            }
        } else {
            t
        }
    } else {
        let code = client.device_start()?;
        println!(
            "Open {} and enter user code {}. Waiting for human authorization.",
            code.verification_uri, code.user_code
        );
        let t = client.device_poll(&code, &|duration| {
            let mut left = duration;
            while !left.is_zero() && !cancelled.load(Ordering::SeqCst) {
                let step = left.min(Duration::from_millis(200));
                std::thread::sleep(step);
                left -= step;
            }
            !cancelled.load(Ordering::SeqCst)
        })?;
        vault
            .save(&t)
            .map_err(|_| "Password store unavailable; no persistence qualification")?;
        t
    };
    let releases = client
        .releases(&token.access_token)
        .map_err(|_| "The authenticated release list could not be read")?;
    let release = releases
        .iter()
        .find(|r| !r.draft && r.tag_name == a[3])
        .ok_or("Published tag not found")?;
    let zip_name = github::package_name(&client.config.asset_prefix, &a[2]);
    let sha_name = format!("{zip_name}.sha256");
    let asset = |name: &str| {
        release.assets.iter().find(|asset| {
            asset.name == name
                && asset.state == "uploaded"
                && asset
                    .uploader
                    .as_ref()
                    .is_some_and(|u| u.login == client.config.publisher)
        })
    };
    let zip = asset(&zip_name).ok_or("Reviewed publisher's platform ZIP not found")?;
    let sha = asset(&sha_name).ok_or("Reviewed publisher's checksum not found")?;
    println!(
        "{}",
        serde_json::json!({"scope":"published release transport only; no installation","tag":release.tag_name,"zip":zip.name,"zip_bytes":zip.size,"checksum_bytes":sha.size,"publisher":client.config.publisher})
    );
    if mode == "metadata" {
        return Ok(());
    }
    let total = zip
        .size
        .checked_add(sha.size)
        .ok_or("Release size overflow")?;
    if max == 0 || total > max || sha.size > 4096 {
        return Err(
            "Transfer exceeds the separately admitted byte limit; no download started".into(),
        );
    }
    let checksum = client
        .download_text(&token.access_token, sha, &qa.join(&sha.name))
        .map_err(|_| "Checksum transfer failed; saved sign-in preserved")?;
    let expected =
        github::parse_checksum(&checksum, &zip.name).ok_or("Invalid published checksum")?;
    let dest = qa.join(format!("{}.partial", zip.name));
    if dest.exists() {
        return Err("Preserve the existing proof; choose a new QA directory".into());
    }
    let actual = client
        .download(&token.access_token, zip, &dest, &mut |_, _| {
            !cancelled.load(Ordering::SeqCst)
        })
        .map_err(|_| {
            "Release transfer failed; partial proof retained and saved sign-in preserved"
        })?;
    if actual != expected {
        return Err("Release checksum mismatch; proof retained, nothing installed".into());
    }
    println!(
        "{}",
        serde_json::json!({"status":"verified","tag":release.tag_name,"bytes":zip.size,"sha256":actual,"file":dest,"installed":false})
    );
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        eprintln!("Release proof: {e}");
        std::process::exit(1);
    }
}
