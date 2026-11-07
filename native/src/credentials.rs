//! The GitHub sign-in lives only in the system vault: Keychain on macOS,
//! Credential Manager on Windows, the Secret Service (libsecret's store) on
//! Linux. Without a vault it is kept in memory for this session only.
use serde::{Deserialize, Serialize};
use std::sync::Mutex;

// Session-only credentials never enter argv, the environment or a file.
static SESSION: Mutex<Option<Token>> = Mutex::new(None);

pub fn session() -> Option<Token> {
    SESSION.lock().ok().and_then(|t| t.clone())
}

pub fn accept_session(value: Option<serde_json::Value>) -> Result<(), String> {
    if let Some(value) = value {
        let token: Token = serde_json::from_value(value).map_err(|_| "Invalid session sign-in")?;
        *SESSION
            .lock()
            .map_err(|_| "Cannot keep the session sign-in")? = Some(token);
        println!("GitHub sign-in continued for this launch.");
    }
    Ok(())
}

#[derive(Clone, Serialize, Deserialize)]
pub struct Token {
    pub access_token: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub refresh_token: Option<String>,
    /// Unix seconds; absent when the token does not expire.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub expires_at: Option<u64>,
}

impl std::fmt::Debug for Token {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        f.write_str("Token(..)")
    }
}

pub struct Vault {
    service: String,
    account: String,
    /// `NFSU2_VAULT=session`: nothing is stored (tests, shared computers).
    session: bool,
}

impl Vault {
    pub fn new(repository: &str, test: bool) -> Self {
        Self {
            service: if test {
                "NFSU2 PSP launcher (test server)"
            } else {
                "NFSU2 PSP launcher"
            }
            .into(),
            account: format!("github:{repository}"),
            session: std::env::var("NFSU2_VAULT").is_ok_and(|v| v == "session"),
        }
    }
    fn entry(&self) -> Result<keyring::Entry, String> {
        keyring::Entry::new(&self.service, &self.account).map_err(|e| e.to_string())
    }
    /// `Ok(None)`: no sign-in stored. `Err`: the vault is unavailable.
    pub fn load(&self) -> Result<Option<Token>, String> {
        if let Some(token) = session() {
            return Ok(Some(token));
        }
        if self.session {
            return Ok(None);
        }
        match self.entry()?.get_password() {
            Ok(s) => Ok(serde_json::from_str(&s).ok()),
            Err(keyring::Error::NoEntry) => Ok(None),
            Err(e) => Err(e.to_string()),
        }
    }
    pub fn save(&self, token: &Token) -> Result<(), String> {
        *SESSION
            .lock()
            .map_err(|_| "Cannot keep the session sign-in")? = Some(token.clone());
        if self.session {
            return Err("NFSU2_VAULT=session".into());
        }
        let s = serde_json::to_string(token).map_err(|e| e.to_string())?;
        self.entry()?.set_password(&s).map_err(|e| e.to_string())
    }
    pub fn delete(&self) -> Result<bool, String> {
        *SESSION
            .lock()
            .map_err(|_| "Cannot clear the session sign-in")? = None;
        if self.session {
            return Ok(false);
        }
        match self.entry()?.delete_credential() {
            Ok(()) => Ok(true),
            Err(keyring::Error::NoEntry) => Ok(false),
            Err(e) => Err(e.to_string()),
        }
    }
}
