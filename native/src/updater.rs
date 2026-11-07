//! At each start: is a newer beta published? If the tester agrees, download
//! it, check it against the published checksum and install it beside this one.
use crate::{
    config,
    credentials::{Token, Vault},
    github::{self, Client, Error, Offer},
    install,
    layout::{Layout, Pointer},
    semver::Version,
};
use serde::{Deserialize, Serialize};
use std::{
    fs,
    io::{self, BufRead, IsTerminal, Write},
    path::Path,
    time::Duration,
};

pub struct Options {
    /// Ask in this terminal.
    pub interactive: bool,
    /// Accept without asking (`--yes`).
    pub yes: bool,
    /// Started as `update`: report every outcome.
    pub explicit: bool,
}

pub enum Outcome {
    Installed,
    Current,
}

#[derive(Default, Serialize, Deserialize)]
struct State {
    #[serde(default)]
    login_declined: bool,
}

const STATE: &str = "launcher-state.json";

fn load_state(layout: &Layout) -> State {
    fs::read(layout.updates.join(STATE))
        .ok()
        .and_then(|d| serde_json::from_slice(&d).ok())
        .unwrap_or_default()
}

fn save_state(layout: &Layout, s: &State) {
    if layout.authority().is_err() { return; }
    let _ = fs::create_dir_all(&layout.updates);
    let _ = fs::write(layout.updates.join(STATE), serde_json::to_vec(s).unwrap_or_default());
}

pub fn ask(question: &str, opts: &Options) -> bool {
    if opts.yes {
        println!("{question} [Y/n] yes");
        return true;
    }
    if !opts.interactive {
        return false;
    }
    print!("{question} [Y/n] ");
    let _ = io::stdout().flush();
    let mut line = String::new();
    if io::stdin().lock().read_line(&mut line).unwrap_or(0) == 0 {
        println!();
        return false;
    }
    !matches!(line.trim().to_ascii_lowercase().as_str(), "n" | "no")
}

pub fn interactive() -> bool {
    io::stdin().is_terminal() && io::stdout().is_terminal()
}

fn pause(d: Duration) -> bool {
    let step = Duration::from_millis(200);
    let mut left = d;
    while !left.is_zero() {
        if crate::interrupted() {
            return false;
        }
        let s = left.min(step);
        std::thread::sleep(s);
        left -= s;
    }
    !crate::interrupted()
}

pub fn login(client: &Client, vault: &Vault) -> Result<Token, String> {
    let code = client.device_start()?;
    println!(
        "\nSign in to GitHub with the account that has access to the beta:\n  1. Open {}\n  2. Enter the code: {}\n\nWaiting for GitHub (Ctrl+C cancels)...",
        code.verification_uri, code.user_code
    );
    if !client.config.test && std::env::var_os("NFSU2_NO_BROWSER").is_none() {
        let _ = crate::open_web(&code.verification_uri);
    }
    let token = client.device_poll(&code, &pause)?;
    match vault.save(&token) {
        Ok(()) => println!("Signed in. The sign-in is kept in this computer's password store."),
        Err(e) => println!("Signed in for this session only: the password store is unavailable ({e})."),
    }
    Ok(token)
}

pub fn logout(here: &Path, layout: &Layout) -> Result<(), String> {
    layout.authority().map_err(|e| e.to_string())?;
    let Some(cfg) = config::load(here)? else {
        println!("This package has no update settings.");
        return Ok(());
    };
    let vault = Vault::new(&cfg.repository, cfg.test);
    match vault.delete() {
        Ok(true) => println!("The GitHub sign-in was removed from this computer."),
        Ok(false) => println!("No GitHub sign-in is stored on this computer."),
        Err(e) => println!("No GitHub sign-in is stored on this computer: the password store is unavailable ({e})."),
    }
    Ok(())
}

/// A usable sign-in, asking for one when needed.
fn sign_in(client: &Client, vault: &Vault, layout: &Layout, opts: &Options, force: bool) -> Result<Option<Token>, String> {
    match vault.load() {
        Ok(Some(t)) if !force => {
            if t.expires_at.is_none_or(|at| at > github::now() + 60) {
                return Ok(Some(t));
            }
            if let Some(r) = &t.refresh_token {
                match client.refresh(r) {
                    Ok(t) => { let _ = vault.save(&t); return Ok(Some(t)); }
                    Err(github::RefreshError::Rejected) => {}
                    Err(github::RefreshError::Unavailable(why)) => {
                        println!("Update check skipped: {why}. The saved GitHub sign-in was kept.");
                        return Ok(None);
                    }
                }
            }
            let _ = vault.delete();
        }
        Ok(_) => {}
        Err(e) => println!("The system password store is unavailable ({e})."),
    }
    let mut state = load_state(layout);
    if state.login_declined && !opts.explicit && !force {
        println!("Update checks are off. Run `nfsu2-generator login` to turn them on.");
        return Ok(None);
    }
    if !opts.yes && !opts.interactive {
        println!("Sign in from a terminal (`nfsu2-generator login`) to receive updates automatically.");
        return Ok(None);
    }
    if !force && !ask("Updates come from the beta's GitHub page. Sign in with GitHub to receive them?", opts) {
        state.login_declined = true;
        save_state(layout, &state);
        println!("Run `nfsu2-generator login` whenever you want to turn update checks on.");
        return Ok(None);
    }
    let token = login(client, vault)?;
    state.login_declined = false;
    save_state(layout, &state);
    Ok(Some(token))
}

pub fn explicit_login(here: &Path, layout: &Layout) -> Result<(), String> {
    layout.authority().map_err(|e| e.to_string())?;
    let cfg = configured(here)?.ok_or("Update checks are not set up in this package yet.")?;
    let client = Client::new(cfg);
    let vault = Vault::new(&client.config.repository, client.config.test);
    let opts = Options { interactive: true, yes: false, explicit: true };
    let token = sign_in(&client, &vault, layout, &opts, true)?.ok_or("Not signed in.")?;
    match client.releases(&token.access_token) {
        Ok(_) => println!("This account can read the beta releases."),
        Err(e) => return Err(format!("Signed in, but {e}.")),
    }
    Ok(())
}

fn configured(here: &Path) -> Result<Option<config::Config>, String> {
    Ok(config::load(here)?.filter(|c| !c.client_id.is_empty()))
}

pub fn check(here: &Path, layout: &Layout, installed: &str, opts: &Options) -> Result<Outcome, String> {
    layout.authority().map_err(|e| e.to_string())?;
    let Some(cfg) = config::load(here)? else {
        if opts.explicit {
            println!("Update checks are not set up in this package yet.");
        }
        return Ok(Outcome::Current);
    };
    let installed = Version::parse(installed).ok_or("This package has no valid version")?;
    let client = Client::new(cfg);
    let vault = Vault::new(&client.config.repository, client.config.test);
    println!("Checking for a newer beta...");
    // Public release pages need no sign-in. Only a private one asks for it.
    let (mut token, releases) = match client.releases("") {
        Ok(r) => (Token { access_token: String::new(), refresh_token: None, expires_at: None }, Ok(r)),
        Err(Error::NoAccess | Error::Unauthorized) if !client.config.client_id.is_empty() => {
            let Some(t) = sign_in(&client, &vault, layout, opts, false)? else {
                return Ok(Outcome::Current);
            };
            let r = client.releases(&t.access_token);
            (t, r)
        }
        Err(Error::NoAccess | Error::Unauthorized) => {
            println!("The update page cannot be read; keeping v{installed}.");
            return Ok(Outcome::Current);
        }
        Err(e) => return Err(e.to_string()),
    };
    let releases = match releases {
        Err(Error::Unauthorized) => {
            let _ = vault.delete();
            println!("The GitHub sign-in expired.");
            let Some(t) = sign_in(&client, &vault, layout, opts, true)? else {
                return Ok(Outcome::Current);
            };
            token = t;
            client.releases(&token.access_token)
        }
        r => r,
    };
    let releases = match releases {
        Ok(r) => r,
        Err(Error::NoAccess) => {
            println!("This GitHub account cannot read the beta. Run `nfsu2-generator logout`, then sign in with the account invited to the beta.");
            return Ok(Outcome::Current);
        }
        Err(e) => return Err(e.to_string()),
    };
    let Some(offer) = github::pick(
        &releases,
        &installed,
        crate::platform(),
        &client.config.publisher,
        &client.config.asset_prefix,
    ) else {
        println!("You have the newest version (v{installed}).");
        return Ok(Outcome::Current);
    };
    println!(
        "\n{} is available{} (you have v{installed}).\n  {}",
        offer.title,
        if offer.prerelease { " as a pre-release" } else { "" },
        offer.url
    );
    if !opts.yes && !opts.interactive {
        println!("Start the generator from a terminal to update.");
        return Ok(Outcome::Current);
    }
    if !ask(&format!("{} available: update?", offer.tag), opts) {
        println!("Keeping v{installed}.");
        return Ok(Outcome::Current);
    }
    install_offer(&client, &token, &offer, layout, &installed)?;
    Ok(Outcome::Installed)
}

fn megabytes(n: u64) -> String {
    format!("{:.0} MB", n as f64 / 1e6)
}

fn install_offer(client: &Client, token: &Token, offer: &Offer, layout: &Layout, installed: &Version) -> Result<(), String> {
    layout.authority().map_err(|e| e.to_string())?;
    let downloads = layout.updates.join("downloads");
    fs::create_dir_all(&downloads).map_err(|e| format!("Cannot write to {} ({e})", layout.updates.display()))?;
    let needed = offer.zip.size.saturating_mul(4);
    if install::free_space(&downloads).is_some_and(|free| free < needed) {
        return Err(format!("The update needs {} free next to the generator.", megabytes(needed)));
    }
    let checksum = client
        .download_text(&token.access_token, &offer.sha, &downloads.join(&offer.sha.name))
        .map_err(|e| e.to_string())?;
    let expected = github::parse_checksum(&checksum, &offer.zip.name).ok_or("The published checksum is not valid. Nothing was installed.")?;
    let zip = downloads.join(format!("{}.partial", offer.zip.name));
    let terminal = io::stdout().is_terminal();
    let mut shown = u64::MAX;
    let digest = client.download(&token.access_token, &offer.zip, &zip, &mut |done, total| {
        let pct = done * 100 / total.max(1);
        let step = if terminal { 1 } else { 25 };
        if pct / step != shown {
            shown = pct / step;
            let line = format!("Downloading v{}: {pct}% ({} of {})", offer.version, megabytes(done), megabytes(total));
            if terminal {
                print!("\r{line}  ");
                let _ = io::stdout().flush();
            } else {
                println!("{line}");
            }
        }
        !crate::interrupted()
    });
    if terminal {
        println!();
    }
    let result = digest.map_err(|e| e.to_string()).and_then(|digest| {
        if digest != expected {
            return Err("The download does not match the published checksum. Nothing was installed.".to_string());
        }
        println!("Checksum verified. Checking every file of the new version...");
        install::stage(layout, &offer.version.to_string(), &zip)
            .map_err(|e| format!("v{} was not installed: {e}", offer.version))
    });
    let _ = fs::remove_file(&zip);
    result?;
    let previous = layout.pointer().filter(|p| layout.version_dir(&p.version).is_dir()).map(|p| p.version);
    layout
        .set_pointer(Some(&Pointer {
            schema: 1,
            version: offer.version.to_string(),
            state: "pending".into(),
            previous,
            from: installed.to_string(),
            attempts: 0,
        }))
        .map_err(|e| format!("The new version could not be selected ({e})"))?;
    println!("v{} installed. Starting it; v{installed} is kept until it opens.", offer.version);
    Ok(())
}
