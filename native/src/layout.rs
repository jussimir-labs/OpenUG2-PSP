//! Installed versions beside the extracted package.
//!
//! The folder the tester extracted stays as it is. Updates go to
//! `.updates/<version>/`, and `.updates/current.json` names the one to run.
//! Replacing that small file (write, then rename) is the atomic switch; the
//! previous version stays until the new one has opened, and goes back into
//! use if the new one cannot start.
use crate::{semver::Version, lease::RootLease};
use std::sync::Arc;
use serde::{Deserialize, Serialize};
use std::{
    fs, io,
    path::{Path, PathBuf},
};

pub const UPDATES: &str = ".updates";
const POINTER: &str = "current.json";
/// Exit code of a version that installed an update: the launcher starts the new one.
pub const RESTART: i32 = 75;
/// Launches of a new version that may end before it opens.
const ATTEMPTS: u32 = 2;

pub fn helper_name() -> String {
    format!("nfsu2-generator{}", std::env::consts::EXE_SUFFIX)
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Pointer {
    pub schema: u32,
    pub version: String,
    /// `pending` until this version has opened, then `confirmed`.
    pub state: String,
    /// The version to go back to; none means the extracted package.
    #[serde(default)]
    pub previous: Option<String>,
    /// The version that installed this one.
    #[serde(default)]
    pub from: String,
    #[serde(default)]
    pub attempts: u32,
}

pub enum Target {
    Root,
    Version(PathBuf),
}

pub struct Layout {
    pub lease: Arc<RootLease>,
    pub root: PathBuf,
    pub updates: PathBuf,
}

fn version_name(s: &str) -> bool {
    Version::parse(s).is_some()
}

impl Layout {
    pub fn new(root: &Path, lease: Arc<RootLease>) -> Self {
        assert!(lease.owns(root), "Layout requires authority for its canonical root");
        Self { lease, root: root.to_path_buf(), updates: root.join(UPDATES) }
    }
    pub fn version_dir(&self, version: &str) -> PathBuf {
        self.updates.join(version)
    }
    pub fn authority(&self) -> io::Result<()> {
        if self.lease.owns(&self.root) && self.updates == self.root.join(UPDATES) { Ok(()) }
        else { Err(io::Error::other("Root lease belongs to another folder")) }
    }
    pub fn pointer(&self) -> Option<Pointer> {
        let p: Pointer = serde_json::from_slice(&fs::read(self.updates.join(POINTER)).ok()?).ok()?;
        (p.schema == 1 && version_name(&p.version) && p.previous.as_deref().is_none_or(version_name)).then_some(p)
    }
    pub fn set_pointer(&self, p: Option<&Pointer>) -> io::Result<()> {
        self.authority()?;
        let path = self.updates.join(POINTER);
        let Some(p) = p else {
            return match fs::remove_file(&path) {
                Err(e) if e.kind() != io::ErrorKind::NotFound => Err(e),
                _ => Ok(()),
            };
        };
        fs::create_dir_all(&self.updates)?;
        let tmp = self.updates.join(format!("{POINTER}.{}.tmp", std::process::id()));
        let mut data = serde_json::to_vec_pretty(p).map_err(io::Error::other)?;
        data.push(b'\n');
        {
            // Flushed through a writable handle: Windows refuses a read-only one.
            let mut f = fs::File::create(&tmp)?;
            io::Write::write_all(&mut f, &data)?;
            f.sync_all()?;
        }
        let r = fs::rename(&tmp, &path);
        if r.is_err() {
            let _ = fs::remove_file(&tmp);
        }
        r
    }

    /// Back to the version before `p` (or to the extracted package).
    fn roll_back(&self, p: &Pointer, why: &str) -> io::Result<()> {
        eprintln!("Version {} {why}; going back to the previous version.", p.version);
        let previous = p.previous.as_ref().map(|v| Pointer {
            schema: 1,
            version: v.clone(),
            state: "confirmed".into(),
            previous: None,
            from: String::new(),
            attempts: 0,
        });
        self.set_pointer(previous.as_ref())
    }

    pub fn roll_back_current(&self, why: &str) -> io::Result<()> {
        match self.pointer() {
            Some(p) => self.roll_back(&p, why),
            None => Ok(()),
        }
    }

    /// Which version this launch runs. `root_version` is the extracted package's.
    pub fn resolve(&self, root_version: &Version) -> io::Result<Target> {
        self.authority()?;
        for _ in 0..3 {
            let Some(mut p) = self.pointer() else {
                return Ok(Target::Root);
            };
            let version = Version::parse(&p.version).expect("validated");
            if version <= *root_version {
                // A newer package was extracted over this folder: it wins.
                self.set_pointer(None)?;
                return Ok(Target::Root);
            }
            let dir = self.version_dir(&p.version);
            if !dir.join(helper_name()).is_file() || !dir.join("bundle.json").is_file() {
                self.roll_back(&p, "is missing")?;
                continue;
            }
            if p.state == "pending" {
                if p.attempts >= ATTEMPTS {
                    self.roll_back(&p, "did not start")?;
                    continue;
                }
                p.attempts += 1;
                self.set_pointer(Some(&p))?;
            }
            return Ok(Target::Version(dir));
        }
        Ok(Target::Root)
    }

    /// This version opened: keep it and remove the others.
    pub fn confirm(&self, mine: Option<&str>) {
        if self.authority().is_err() { return; }
        let current = self.pointer();
        if let (Some(mut p), Some(mine)) = (current.clone(), mine) {
            if p.version == mine && p.state == "pending" {
                p.state = "confirmed".into();
                p.attempts = 0;
                p.previous = None;
                if let Err(e) = self.set_pointer(Some(&p)) {
                    eprintln!("Could not record the update ({e}).");
                    return;
                }
            }
        }
        self.clean(self.pointer().map(|p| p.version).as_deref());
    }

    /// Removes versions other than `keep` and unfinished downloads.
    /// A folder in use (Windows) is retried on a later start.
    pub fn clean(&self, keep: Option<&str>) {
        if self.authority().is_err() { return; }
        let Ok(entries) = fs::read_dir(&self.updates) else {
            return;
        };
        let keep_previous = self.pointer().and_then(|p| p.previous);
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().into_owned();
            let path = entry.path();
            if !entry.file_type().is_ok_and(|t| t.is_dir()) || self.lease.protects(&path) {
                continue;
            }
            if name.starts_with(".staging-") {
                let _ = fs::remove_dir_all(&path);
            } else if name == "downloads" {
                remove_downloads(&path);
            } else if version_name(&name) && Some(name.as_str()) != keep && Some(&name) != keep_previous.as_ref() {
                remove_package(&path, &self.lease);
            }
        }
    }
}

fn remove_downloads(dir: &Path) {
    for entry in fs::read_dir(dir).into_iter().flatten().flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if name.ends_with(".zip.partial") || name.ends_with(".sha256") {
            let _ = fs::remove_file(entry.path());
        }
    }
    let _ = fs::remove_dir(dir);
}

/// Deletes only the files a package lists in its `bundle.json` (and its own
/// log folder): anything else someone put there stays.
pub fn remove_package(dir: &Path, lease: &RootLease) -> bool {
    if !lease.owns_version_dir(dir) { return false; }
    if lease.protects(dir) || fs::symlink_metadata(dir).is_ok_and(|m| m.file_type().is_symlink()) { return false; }
    let Ok(canonical) = dir.canonicalize() else { return false; };
    #[derive(Deserialize)]
    struct Listed {
        files: std::collections::BTreeMap<String, String>,
    }
    let listed = fs::read(dir.join("bundle.json"))
        .ok()
        .and_then(|d| serde_json::from_slice::<Listed>(&d).ok());
    if let Some(listed) = listed {
        let mut retained_listed = false;
        for name in listed.files.keys() {
            let p = Path::new(name);
            if p.components().all(|c| matches!(c, std::path::Component::Normal(_))) {
                if dir.join(p).parent().and_then(|p| p.canonicalize().ok()).is_some_and(|p| !p.starts_with(&canonical)) {
                    retained_listed = true;
                    continue;
                }
                if let Err(e) = fs::remove_file(dir.join(p)) {
                    if e.kind() != io::ErrorKind::NotFound { retained_listed = true; }
                }
            }
        }
        // Only our known logs/lease belong to the helper. An ISO, save or
        // other unlisted file placed here still belongs to the user.
        for name in ["browser-generator.log", "browser-generator.lock"] {
            let _ = fs::remove_file(dir.join(".tools").join(name));
        }
        // A Windows executable can still be open. Preserve the removal list
        // so a later launch can retry; never strand a locked exe unlisted.
        if !retained_listed { let _ = fs::remove_file(dir.join("bundle.json")); }
    }
    remove_empty(dir);
    if dir.exists() {
        eprintln!("Kept {}: it holds files that are not part of the package.", dir.display());
        return false;
    }
    true
}

fn remove_empty(dir: &Path) {
    if let Ok(entries) = fs::read_dir(dir) {
        for e in entries.flatten() {
            if e.file_type().is_ok_and(|t| t.is_dir()) {
                remove_empty(&e.path());
            }
        }
    }
    let _ = fs::remove_dir(dir);
}

#[cfg(test)]
mod tests {
    use super::*;
    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("nfsu2-layout-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&d);
        fs::create_dir_all(&d).unwrap();
        d.canonicalize().unwrap()
    }
    fn install(l: &Layout, v: &str) {
        let d = l.version_dir(v);
        fs::create_dir_all(&d).unwrap();
        fs::write(d.join(helper_name()), b"x").unwrap();
        fs::write(d.join("bundle.json"), format!(r#"{{"files":{{"{}":""}}}}"#, helper_name())).unwrap();
    }
    fn pending(v: &str, previous: Option<&str>) -> Pointer {
        Pointer { schema: 1, version: v.into(), state: "pending".into(), previous: previous.map(Into::into), from: "0.2.6".into(), attempts: 0 }
    }
    fn target(l: &Layout, root: &str) -> Option<PathBuf> {
        match l.resolve(&Version::parse(root).unwrap()).unwrap() {
            Target::Root => None,
            Target::Version(d) => Some(d),
        }
    }
    #[test]
    fn a_version_that_never_opens_goes_back() {
        let root = temp("rollback");
        let l = Layout::new(&root, RootLease::enter(&root).unwrap().0);
        install(&l, "0.2.7");
        install(&l, "0.2.8");
        l.set_pointer(Some(&pending("0.2.8", Some("0.2.7")))).unwrap();
        for _ in 0..ATTEMPTS {
            assert_eq!(target(&l, "0.2.6"), Some(l.version_dir("0.2.8")));
        }
        assert_eq!(target(&l, "0.2.6"), Some(l.version_dir("0.2.7")));
        assert_eq!(l.pointer().unwrap().state, "confirmed");
        l.set_pointer(Some(&pending("0.2.8", None))).unwrap();
        fs::remove_file(l.version_dir("0.2.8").join(helper_name())).unwrap();
        assert_eq!(target(&l, "0.2.6"), None);
        assert!(l.pointer().is_none());
    }
    #[test]
    fn opening_confirms_and_cleans() {
        let root = temp("confirm");
        let l = Layout::new(&root, RootLease::enter(&root).unwrap().0);
        install(&l, "0.2.7");
        install(&l, "0.2.8");
        fs::create_dir_all(l.updates.join(".staging-0.2.9")).unwrap();
        fs::write(l.version_dir("0.2.7").join("tester-file.txt"), b"keep").unwrap();
        fs::create_dir_all(l.version_dir("0.2.7").join(".tools")).unwrap();
        fs::write(l.version_dir("0.2.7").join(".tools/player.sav"), b"keep save").unwrap();
        l.set_pointer(Some(&pending("0.2.8", Some("0.2.7")))).unwrap();
        l.clean(Some("0.2.8"));
        assert!(l.version_dir("0.2.7").exists(), "kept until the new one opens");
        l.confirm(Some("0.2.8"));
        let p = l.pointer().unwrap();
        assert_eq!((p.state.as_str(), p.previous), ("confirmed", None));
        assert!(l.version_dir("0.2.8").join(helper_name()).is_file());
        assert!(!l.version_dir("0.2.7").join(helper_name()).exists());
        assert!(l.version_dir("0.2.7").join("tester-file.txt").is_file(), "unlisted files stay");
        assert_eq!(fs::read(l.version_dir("0.2.7").join(".tools/player.sav")).unwrap(), b"keep save");
        assert!(!l.updates.join(".staging-0.2.9").exists());
    }
    #[test]
    fn a_newer_extracted_package_wins() {
        let root = temp("newer");
        let l = Layout::new(&root, RootLease::enter(&root).unwrap().0);
        install(&l, "0.2.7");
        l.set_pointer(Some(&pending("0.2.7", None))).unwrap();
        assert_eq!(target(&l, "0.2.7"), None);
        assert!(l.pointer().is_none());
    }
    #[test]
    #[cfg(windows)]
    fn a_locked_executable_keeps_its_removal_manifest() {
        use std::os::windows::fs::OpenOptionsExt;
        let root = temp("locked-exe");
        let l = Layout::new(&root, RootLease::enter(&root).unwrap().0);
        install(&l, "0.2.7");
        let d = l.version_dir("0.2.7");
        let locked = fs::OpenOptions::new().read(true).share_mode(0).open(d.join(helper_name())).unwrap();
        assert!(!remove_package(&d, &l.lease));
        assert!(d.join("bundle.json").is_file(), "retry must still know which file it owns");
        drop(locked);
        assert!(remove_package(&d, &l.lease));
    }
}
