//! The beta releases on GitHub: newest package for this computer, its
//! download, and the device-code sign-in (no secret inside the program).
use crate::{config::Config, credentials::Token, semver::Version};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::{
    fs,
    io::{Read, Write},
    path::Path,
    time::{Duration, SystemTime, UNIX_EPOCH},
};

#[derive(Debug)]
pub enum Error {
    /// The sign-in expired or was revoked.
    Unauthorized,
    /// This account cannot read the beta repository.
    NoAccess,
    Other(String),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Error::Unauthorized => f.write_str("the GitHub sign-in expired"),
            Error::NoAccess => f.write_str("this GitHub account cannot read the beta repository"),
            Error::Other(s) => f.write_str(s),
        }
    }
}

#[derive(Clone, Debug, Deserialize)]
pub struct User {
    pub login: String,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Asset {
    pub id: u64,
    pub name: String,
    pub size: u64,
    #[serde(default)]
    pub state: String,
    pub uploader: Option<User>,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Release {
    pub tag_name: String,
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default)]
    pub draft: bool,
    #[serde(default)]
    pub prerelease: bool,
    #[serde(default)]
    pub html_url: String,
    #[serde(default)]
    pub assets: Vec<Asset>,
}

#[derive(Clone, Debug)]
pub struct Offer {
    pub version: Version,
    pub tag: String,
    pub title: String,
    pub url: String,
    pub prerelease: bool,
    pub zip: Asset,
    pub sha: Asset,
}

pub fn package_name(prefix: &str, platform: &str) -> String {
    format!("{prefix}-{platform}.zip")
}

/// The newest published release above `installed` with this platform's ZIP
/// and checksum, both uploaded by the publishing account.
pub fn pick(
    releases: &[Release],
    installed: &Version,
    platform: &str,
    publisher: &str,
    prefix: &str,
) -> Option<Offer> {
    let zip_name = package_name(prefix, platform);
    let sha_name = format!("{zip_name}.sha256");
    let published = |name: &str, r: &Release| {
        r.assets
            .iter()
            .find(|a| {
                a.name == name
                    && a.state == "uploaded"
                    && a.uploader.as_ref().is_some_and(|u| u.login == publisher)
            })
            .cloned()
    };
    releases
        .iter()
        .filter(|r| !r.draft)
        .filter_map(|r| {
            let version = Version::from_tag(&r.tag_name)?;
            if version <= *installed {
                return None;
            }
            Some(Offer {
                zip: published(&zip_name, r)?,
                sha: published(&sha_name, r)?,
                version,
                tag: r.tag_name.clone(),
                title: r
                    .name
                    .clone()
                    .filter(|n| !n.trim().is_empty())
                    .unwrap_or_else(|| r.tag_name.clone()),
                url: r.html_url.clone(),
                prerelease: r.prerelease,
            })
        })
        .max_by(|a, b| a.version.cmp(&b.version))
}

/// The digest in a `.sha256` file written by `test_bundle.py`:
/// `<64 hex>  <zip name>`.
pub fn parse_checksum(text: &str, zip_name: &str) -> Option<String> {
    let mut lines = text.lines().filter(|l| !l.trim().is_empty());
    let line = lines.next()?;
    if lines.next().is_some() {
        return None;
    }
    let (digest, name) = line.split_once(char::is_whitespace)?;
    let name = name.trim().trim_start_matches('*');
    (digest.len() == 64 && digest.bytes().all(|b| b.is_ascii_hexdigit()) && name == zip_name)
        .then(|| digest.to_ascii_lowercase())
}

pub struct DeviceCode {
    pub device_code: String,
    pub user_code: String,
    pub verification_uri: String,
    pub expires_in: u64,
    pub interval: u64,
    started_at: std::time::Instant,
}

#[derive(Deserialize)]
struct TokenReply {
    access_token: Option<String>,
    refresh_token: Option<String>,
    expires_in: Option<u64>,
    error: Option<String>,
    error_description: Option<String>,
    interval: Option<u64>,
}

pub fn now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs())
}

pub struct Client {
    agent: ureq::Agent,
    pub config: Config,
}

pub enum RefreshError {
    /// OAuth explicitly rejected the refresh credential, rather than the
    /// network/server/configuration merely being unavailable.
    Rejected,
    Unavailable(String),
}

impl Client {
    pub fn new(config: Config) -> Self {
        let agent = ureq::AgentBuilder::new()
            .timeout_connect(Duration::from_secs(10))
            .timeout_read(Duration::from_secs(60))
            .timeout_write(Duration::from_secs(30))
            // Redirects are followed by hand so the sign-in never leaves GitHub.
            .redirects(0)
            .user_agent(concat!("nfsu2-psp-launcher/", env!("CARGO_PKG_VERSION")))
            .build();
        Self { agent, config }
    }

    fn api(&self, path: &str, token: &str, accept: &str) -> Result<ureq::Response, Error> {
        let url = format!(
            "{}/repos/{}/{}",
            self.config.api, self.config.repository, path
        );
        let mut request = self
            .agent
            .get(&url)
            .set("Accept", accept)
            .set("X-GitHub-Api-Version", "2022-11-28");
        // An empty token reads a public repository without any sign-in.
        if !token.is_empty() {
            request = request.set("Authorization", &format!("Bearer {token}"));
        }
        match request.call() {
            Ok(r) => Ok(r),
            Err(ureq::Error::Status(code, r)) if (300..400).contains(&code) => Ok(r),
            Err(ureq::Error::Status(401, _)) => Err(Error::Unauthorized),
            Err(ureq::Error::Status(404, _)) => Err(Error::NoAccess),
            Err(ureq::Error::Status(403, r)) if r.header("x-ratelimit-remaining") == Some("0") => {
                Err(Error::Other(
                    "GitHub's request limit was reached; try again in an hour".into(),
                ))
            }
            Err(ureq::Error::Status(code, _)) => {
                Err(Error::Other(format!("GitHub answered {code}")))
            }
            Err(e) => Err(Error::Other(format!(
                "GitHub could not be reached ({})",
                e.kind()
            ))),
        }
    }

    pub fn releases(&self, token: &str) -> Result<Vec<Release>, Error> {
        let mut releases = Vec::new();
        // Construct every URL ourselves: never forward credentials to a Link URL.
        for page in 1..=100 {
            let r = self.api(
                &format!("releases?per_page=100&page={page}"),
                token,
                "application/vnd.github+json",
            )?;
            let more = r.header("Link").is_some_and(|v| v.contains("rel=\"next\""));
            let body = r.into_string().map_err(|e| Error::Other(e.to_string()))?;
            let batch: Vec<Release> = serde_json::from_str(&body)
                .map_err(|e| Error::Other(format!("Unexpected release list: {e}")))?;
            releases.extend(batch);
            if !more {
                return Ok(releases);
            }
        }
        Err(Error::Other(
            "The release list is too long; no update was selected".into(),
        ))
    }

    /// Streams an asset to `dest`, returning its SHA-256. The size must be
    /// exactly the published one.
    pub fn download(
        &self,
        token: &str,
        asset: &Asset,
        dest: &Path,
        progress: &mut dyn FnMut(u64, u64) -> bool,
    ) -> Result<String, Error> {
        let other = |e: std::io::Error| Error::Other(format!("{}: {e}", dest.display()));
        let first = self.api(
            &format!("releases/assets/{}", asset.id),
            token,
            "application/octet-stream",
        )?;
        let response = match first.status() {
            200 => first,
            301 | 302 | 303 | 307 | 308 => {
                let location = first.header("Location").unwrap_or_default().to_string();
                let same_test_server =
                    self.config.test && location.starts_with(&format!("{}/", self.config.api));
                if !location.starts_with("https://") && !same_test_server {
                    return Err(Error::Other(
                        "The download was redirected to an insecure address".into(),
                    ));
                }
                // The storage link is signed: the GitHub sign-in is not sent there.
                self.agent
                    .get(&location)
                    .set("Accept", "application/octet-stream")
                    .call()
                    .map_err(|e| {
                        Error::Other(match e {
                            ureq::Error::Status(status, _) => {
                                format!("The download failed (HTTP {status})")
                            }
                            ureq::Error::Transport(t) => {
                                format!("The download failed ({})", t.kind())
                            }
                        })
                    })?
            }
            code => return Err(Error::Other(format!("Unexpected download answer {code}"))),
        };
        let _ = fs::remove_file(dest);
        let mut file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(dest)
            .map_err(other)?;
        let mut reader = response.into_reader().take(asset.size + 1);
        let mut hash = Sha256::new();
        let mut buf = vec![0u8; 1 << 16];
        let mut total = 0u64;
        loop {
            let n = reader
                .read(&mut buf)
                .map_err(|e| Error::Other(format!("The download stopped ({:?})", e.kind())))?;
            if n == 0 {
                break;
            }
            total += n as u64;
            if total > asset.size {
                return Err(Error::Other(
                    "The download is larger than the published file".into(),
                ));
            }
            hash.update(&buf[..n]);
            file.write_all(&buf[..n]).map_err(other)?;
            if !progress(total, asset.size) {
                return Err(Error::Other("The download was cancelled".into()));
            }
        }
        if total != asset.size {
            return Err(Error::Other("The download ended early".into()));
        }
        file.sync_all().map_err(other)?;
        Ok(format!("{:x}", hash.finalize()))
    }

    /// The small checksum file, read into memory.
    pub fn download_text(&self, token: &str, asset: &Asset, dest: &Path) -> Result<String, Error> {
        if asset.size > 4096 {
            return Err(Error::Other("The checksum file is too large".into()));
        }
        self.download(token, asset, dest, &mut |_, _| true)?;
        let text = fs::read_to_string(dest).map_err(|e| Error::Other(e.to_string()));
        let _ = fs::remove_file(dest);
        text
    }

    fn token_request(&self, form: &[(&str, &str)]) -> Result<TokenReply, String> {
        let url = format!("{}/login/oauth/access_token", self.config.web);
        let body = match self
            .agent
            .post(&url)
            .set("Accept", "application/json")
            .send_form(form)
        {
            Ok(r) => r.into_string().map_err(|e| e.to_string())?,
            Err(ureq::Error::Status(_, r)) => r.into_string().map_err(|e| e.to_string())?,
            Err(e) => return Err(format!("GitHub could not be reached ({})", e.kind())),
        };
        serde_json::from_str(&body).map_err(|_| "Unexpected sign-in answer from GitHub".to_string())
    }

    fn token_from(reply: TokenReply) -> Option<Token> {
        Some(Token {
            access_token: reply.access_token.filter(|t| !t.is_empty())?,
            refresh_token: reply.refresh_token,
            expires_at: reply.expires_in.map(|s| now().saturating_add(s)),
        })
    }

    pub fn device_start(&self) -> Result<DeviceCode, String> {
        #[derive(Deserialize)]
        struct Reply {
            device_code: String,
            user_code: String,
            verification_uri: String,
            expires_in: u64,
            interval: Option<u64>,
        }
        let url = format!("{}/login/device/code", self.config.web);
        let mut form = vec![("client_id", self.config.client_id.as_str())];
        if !self.config.scope.is_empty() {
            form.push(("scope", self.config.scope.as_str()));
        }
        // Count the initial request and browser-opening time conservatively
        // against the code lifetime, not just time spent in the polling loop.
        let started_at = std::time::Instant::now();
        let r = match self
            .agent
            .post(&url)
            .set("Accept", "application/json")
            .send_form(&form)
        {
            Ok(r) => r,
            Err(ureq::Error::Status(code, _)) => {
                return Err(format!(
                    "GitHub refused the sign-in request ({code}); check the client ID in {}",
                    crate::config::CONFIG
                ))
            }
            Err(e) => return Err(format!("GitHub could not be reached ({})", e.kind())),
        };
        let reply: Reply = serde_json::from_str(&r.into_string().map_err(|e| e.to_string())?)
            .map_err(|_| {
                "Unexpected sign-in answer from GitHub. Device sign-in must be enabled for the app."
                    .to_string()
            })?;
        if reply.verification_uri != format!("{}/login/device", self.config.web) {
            return Err("Unexpected sign-in address from GitHub".into());
        }
        if reply.user_code.len() > 16
            || !reply
                .user_code
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b == b'-')
        {
            return Err("Unexpected sign-in code from GitHub".into());
        }
        Ok(DeviceCode {
            device_code: reply.device_code,
            user_code: reply.user_code,
            verification_uri: reply.verification_uri,
            expires_in: reply.expires_in,
            interval: reply.interval.unwrap_or(5).max(1),
            started_at,
        })
    }

    pub fn device_poll(
        &self,
        code: &DeviceCode,
        sleep: &dyn Fn(Duration) -> bool,
    ) -> Result<Token, String> {
        Self::poll_device(code, sleep, &|| code.started_at.elapsed(), &mut || {
            self.token_request(&[
                ("client_id", self.config.client_id.as_str()),
                ("device_code", code.device_code.as_str()),
                ("grant_type", "urn:ietf:params:oauth:grant-type:device_code"),
            ])
        })
    }

    // A monotonic elapsed duration avoids wall-clock jumps and deadline-add
    // overflow. The clock/request seams let tests exercise expiration without
    // waiting or contacting GitHub; production uses this same polling loop.
    fn poll_device(
        code: &DeviceCode,
        sleep: &dyn Fn(Duration) -> bool,
        elapsed: &dyn Fn() -> Duration,
        request: &mut dyn FnMut() -> Result<TokenReply, String>,
    ) -> Result<Token, String> {
        let lifetime = Duration::from_secs(code.expires_in);
        let mut interval = code.interval.max(1);
        loop {
            let remaining = lifetime.saturating_sub(elapsed());
            if remaining.is_zero() {
                break;
            }
            if !sleep(Duration::from_secs(interval).min(remaining)) {
                return Err("The sign-in was cancelled".into());
            }
            // Sleeping may consume the entire code lifetime. In that case
            // there must be no last POST after expiration.
            if elapsed() >= lifetime {
                break;
            }
            let reply = request()?;
            match reply.error.as_deref() {
                None => {
                    return Self::token_from(reply)
                        .ok_or_else(|| "GitHub sent no sign-in token".to_string())
                }
                Some("authorization_pending") => {}
                Some("slow_down") => {
                    interval = interval.saturating_add(5).max(reply.interval.unwrap_or(0));
                }
                Some("expired_token") => break,
                Some("access_denied") => return Err("The sign-in was cancelled on GitHub".into()),
                Some(e) => {
                    return Err(format!(
                        "GitHub refused the sign-in ({})",
                        reply.error_description.unwrap_or_else(|| e.into())
                    ))
                }
            }
        }
        Err("The sign-in code expired. Start again.".into())
    }

    /// Renews an expiring token (GitHub Apps with token expiration on).
    pub fn refresh(&self, refresh_token: &str) -> Result<Token, RefreshError> {
        let url = format!("{}/login/oauth/access_token", self.config.web);
        let response = match self
            .agent
            .post(&url)
            .set("Accept", "application/json")
            .send_form(&[
                ("client_id", self.config.client_id.as_str()),
                ("grant_type", "refresh_token"),
                ("refresh_token", refresh_token),
            ]) {
            Ok(r) => r,
            Err(ureq::Error::Status(_, r)) => r,
            Err(e) => {
                return Err(RefreshError::Unavailable(format!(
                    "GitHub could not be reached ({})",
                    e.kind()
                )))
            }
        };
        let status = response.status();
        let body = response.into_string().map_err(|_| {
            RefreshError::Unavailable("GitHub's refresh response could not be read".into())
        })?;
        let reply: TokenReply = serde_json::from_str(&body).map_err(|_| {
            RefreshError::Unavailable("Unexpected refresh response from GitHub".into())
        })?;
        // A 5xx/429 or malformed answer must never erase a valid refresh token,
        // even if its body happens to resemble an OAuth error.
        if ![200, 400].contains(&status) {
            return Err(RefreshError::Unavailable(format!(
                "GitHub answered {status}"
            )));
        }
        if matches!(
            reply.error.as_deref(),
            Some("bad_refresh_token" | "invalid_grant")
        ) {
            return Err(RefreshError::Rejected);
        }
        Self::token_from(reply).ok_or_else(|| {
            RefreshError::Unavailable(
                "GitHub did not renew the sign-in; the saved sign-in was kept".into(),
            )
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::{Cell, RefCell};

    fn poll_case(
        interval: u64,
        lifetime: u64,
        replies: Vec<(Option<&str>, Option<u64>, u64)>,
    ) -> (Result<Token, String>, Vec<u64>, usize) {
        let elapsed = Cell::new(Duration::ZERO);
        let waits = RefCell::new(Vec::new());
        let calls = Cell::new(0);
        let code = DeviceCode {
            device_code: "unit-test-code".into(),
            user_code: "TEST".into(),
            verification_uri: String::new(),
            expires_in: lifetime,
            interval,
            started_at: std::time::Instant::now(),
        };
        let sleep = |duration: Duration| {
            waits.borrow_mut().push(duration.as_secs());
            elapsed.set(elapsed.get().saturating_add(duration));
            true
        };
        let mut replies = replies.into_iter();
        let result = Client::poll_device(&code, &sleep, &|| elapsed.get(), &mut || {
            assert!(
                elapsed.get() < Duration::from_secs(lifetime),
                "POST after expiration"
            );
            calls.set(calls.get() + 1);
            let (error, interval, spent) = replies.next().expect("unexpected POST");
            elapsed.set(elapsed.get().saturating_add(Duration::from_secs(spent)));
            Ok(TokenReply {
                access_token: error.is_none().then(|| "unit-test-token".into()),
                refresh_token: None,
                expires_in: None,
                error: error.map(Into::into),
                error_description: None,
                interval,
            })
        });
        (result, waits.into_inner(), calls.get())
    }

    #[test]
    fn device_poll_slow_down_never_reduces_server_interval() {
        let (result, waits, calls) = poll_case(
            120,
            1000,
            vec![
                (Some("slow_down"), None, 0),
                (Some("slow_down"), Some(240), 0),
                (Some("slow_down"), Some(1), 0),
                (None, None, 0),
            ],
        );
        assert!(result.is_ok());
        assert_eq!(waits, [120, 125, 240, 245]);
        assert_eq!(calls, 4);
    }

    #[test]
    fn device_poll_wait_is_bounded_by_lifetime_and_never_posts_at_deadline() {
        for (interval, lifetime, expected) in [(300, 2, vec![2]), (5, 0, vec![])] {
            let (result, waits, calls) = poll_case(interval, lifetime, vec![]);
            assert!(result.unwrap_err().contains("code expired"));
            assert_eq!(waits, expected);
            assert_eq!(calls, 0);
        }
        let (result, waits, calls) = poll_case(120, 200, vec![(Some("slow_down"), Some(240), 0)]);
        assert!(result.unwrap_err().contains("code expired"));
        assert_eq!(waits, [120, 80]);
        assert_eq!(calls, 1);
    }

    #[test]
    fn device_poll_stops_when_request_consumes_remaining_lifetime() {
        let (result, waits, calls) =
            poll_case(5, 12, vec![(Some("authorization_pending"), None, 7)]);
        assert!(result.unwrap_err().contains("code expired"));
        assert_eq!(waits, [5]);
        assert_eq!(calls, 1);
    }

    #[test]
    fn device_poll_slow_down_and_lifetime_do_not_overflow() {
        let (result, waits, calls) = poll_case(
            u64::MAX - 1,
            u64::MAX,
            vec![(Some("slow_down"), Some(u64::MAX), 0)],
        );
        assert!(result.unwrap_err().contains("code expired"));
        assert_eq!(waits, [u64::MAX - 1, 1]);
        assert_eq!(calls, 1);
    }

    fn asset(id: u64, name: &str, who: &str) -> Asset {
        Asset {
            id,
            name: name.into(),
            size: 10,
            state: "uploaded".into(),
            uploader: Some(User { login: who.into() }),
        }
    }
    fn release(tag: &str, assets: Vec<Asset>) -> Release {
        Release {
            tag_name: tag.into(),
            name: None,
            draft: false,
            prerelease: true,
            html_url: String::new(),
            assets,
        }
    }
    fn full(tag: &str, who: &str) -> Release {
        release(
            tag,
            vec![
                asset(1, "NFSU2-Generator-macos-arm64.zip", who),
                asset(2, "NFSU2-Generator-macos-arm64.zip.sha256", who),
            ],
        )
    }
    #[test]
    fn picks_the_newest_complete_release() {
        let installed = Version::parse("0.2.6").unwrap();
        let mut draft = full("v0.9.0", "jussimir");
        draft.draft = true;
        let list = vec![
            full("v0.2.5", "jussimir"),
            full("v0.2.7", "jussimir"),
            full("v0.3.0-rc.1", "jussimir"),
            release(
                "v0.4.0",
                vec![asset(1, "NFSU2-Generator-macos-arm64.zip", "jussimir")],
            ),
            full("v0.5.0", "someone-else"),
            full("beta-1-rc21-native", "jussimir"),
            draft,
        ];
        let offer = pick(&list, &installed, "macos-arm64", "jussimir", "NFSU2-Generator").unwrap();
        assert_eq!(offer.tag, "v0.3.0-rc.1");
        assert!(pick(&list, &installed, "windows-x64", "jussimir", "NFSU2-Generator").is_none());
        assert!(pick(&list[..1], &installed, "macos-arm64", "jussimir", "NFSU2-Generator").is_none());
    }
    /// `NFSU2_RELEASES_JSON=<saved release list> cargo test -- --ignored`
    #[test]
    #[ignore]
    fn reads_a_saved_github_release_list() {
        let list: Vec<Release> = serde_json::from_slice(
            &std::fs::read(std::env::var("NFSU2_RELEASES_JSON").unwrap()).unwrap(),
        )
        .unwrap();
        for platform in ["macos-arm64", "macos-x64", "windows-x64", "linux-x64"] {
            let offer = pick(
                &list,
                &Version::parse("0.1.0").unwrap(),
                platform,
                "jussimir",
                "NFSU2-Generator",
            )
            .unwrap();
            assert!(
                offer.zip.size > 1 << 20 && offer.sha.size < 200,
                "{platform}"
            );
            println!(
                "{platform}: {} {} ({} bytes)",
                offer.tag, offer.zip.name, offer.zip.size
            );
        }
    }
    #[test]
    fn checksum_must_name_the_zip() {
        let d = "ab".repeat(32);
        assert_eq!(
            parse_checksum(
                &format!("{d}  NFSU2-Generator-linux-x64.zip\n"),
                "NFSU2-Generator-linux-x64.zip"
            ),
            Some(d.clone())
        );
        assert_eq!(
            parse_checksum(&format!("{}  x.zip\n", d.to_uppercase()), "x.zip"),
            Some(d.clone())
        );
        for bad in [
            format!("{d}  other.zip"),
            format!("{}  x.zip", &d[..62]),
            format!("{d}  x.zip\n{d}  x.zip"),
            String::new(),
        ] {
            assert!(parse_checksum(&bad, "x.zip").is_none(), "{bad}");
        }
    }
}
