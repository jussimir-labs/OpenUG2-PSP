//! Update settings shipped inside the verified bundle (`native/launcher.json`).
use serde::Deserialize;
use std::{env, fs, path::Path};

pub const API: &str = "https://api.github.com";
pub const WEB: &str = "https://github.com";
pub const CONFIG: &str = "native/launcher.json";

#[derive(Deserialize)]
struct File {
    schema: u32,
    repository: String,
    publisher: String,
    #[serde(default)]
    client_id: String,
    #[serde(default)]
    scope: String,
    /// Release files are `<asset_prefix>-<platform>.zip` and its `.sha256`.
    #[serde(default = "default_prefix")]
    asset_prefix: String,
}

fn default_prefix() -> String {
    "NFSU2-Generator".into()
}

#[derive(Clone, Debug)]
pub struct Config {
    pub repository: String,
    /// The account that uploads the packages (`native/publish.py`).
    pub publisher: String,
    pub client_id: String,
    pub scope: String,
    pub asset_prefix: String,
    pub api: String,
    pub web: String,
    /// A local test server replaces GitHub; its sign-in never reaches the
    /// tester's real credential.
    pub test: bool,
}

fn name_ok(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 100
        && s.bytes()
            .all(|b| b.is_ascii_alphanumeric() || b"-_.".contains(&b))
}

/// `http://127.0.0.1:PORT` only: tests may replace GitHub, nothing else may.
pub fn loopback(s: &str) -> bool {
    s.strip_prefix("http://127.0.0.1:")
        .and_then(|port| port.parse::<u16>().ok())
        .is_some_and(|p| p != 0)
}

pub fn load(root: &Path) -> Result<Option<Config>, String> {
    if env::var("NFSU2_UPDATES").is_ok_and(|v| v == "0") {
        return Ok(None);
    }
    let Ok(data) = fs::read(root.join(CONFIG)) else {
        return Ok(None);
    };
    let f: File = serde_json::from_slice(&data).map_err(|e| format!("Invalid {CONFIG}: {e}"))?;
    let (owner, repo) = f.repository.split_once('/').unwrap_or_default();
    if f.schema != 1 || !name_ok(owner) || !name_ok(repo) || !name_ok(&f.publisher) || !name_ok(&f.asset_prefix) {
        return Err(format!("Invalid {CONFIG}"));
    }
    let mut c = Config {
        repository: f.repository,
        publisher: f.publisher,
        client_id: f.client_id,
        scope: f.scope,
        asset_prefix: f.asset_prefix,
        api: API.into(),
        web: WEB.into(),
        test: false,
    };
    let api = env::var("NFSU2_UPDATE_API").ok();
    let web = env::var("NFSU2_UPDATE_WEB").ok();
    if api.is_some() || web.is_some() {
        match (api, web) {
            (Some(a), Some(w)) if loopback(&a) && loopback(&w) => {
                c.api = a;
                c.web = w;
                c.test = true;
            }
            _ => {
                return Err(
                    "NFSU2_UPDATE_API/NFSU2_UPDATE_WEB only accept a local test server.".into(),
                )
            }
        }
    }
    if let Ok(id) = env::var("NFSU2_GITHUB_CLIENT_ID") {
        c.client_id = id;
    }
    if !c.client_id.is_empty() && !name_ok(&c.client_id) {
        return Err("Invalid GitHub client ID".into());
    }
    if !c.scope.is_empty()
        && !c
            .scope
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b" :_".contains(&b))
    {
        return Err("Invalid GitHub scope".into());
    }
    Ok(Some(c))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn only_loopback_test_servers() {
        assert!(loopback("http://127.0.0.1:8123"));
        for s in [
            "https://127.0.0.1:8123",
            "http://127.0.0.1:0",
            "http://localhost:80",
            "http://127.0.0.1:80/x",
            "http://10.0.0.1:80",
        ] {
            assert!(!loopback(s), "{s}");
        }
    }
    #[test]
    fn shipped_config_is_valid() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
        let f: File = serde_json::from_slice(&fs::read(root.join(CONFIG)).unwrap()).unwrap();
        assert_eq!(f.schema, 1);
        assert_eq!(f.repository, "jussimir-labs/cache-2");
        assert_eq!(f.asset_prefix, "pack");
        assert_eq!(f.publisher, "jussimir");
    }
}
