//! Unpacks a downloaded package into a staging folder, checks it like the
//! extracted package is checked at each start, then moves it into place.
use crate::layout::{helper_name, remove_package, Layout};
use std::{
    collections::BTreeSet,
    fs,
    io::{self, Read},
    path::{Component, Path, PathBuf},
};

const MAX_ENTRIES: usize = 50_000;
const MAX_BYTES: u64 = 8 << 30;

fn zip_error(e: zip::result::ZipError) -> String {
    format!("The downloaded package cannot be read ({e})")
}

/// Extracts the entries under `prefix` (the package's own folder). Links,
/// absolute paths, `..` and duplicate names are refused.
pub fn extract(zip_path: &Path, dest: &Path, prefix: &str) -> Result<(), String> {
    let mut archive = zip::ZipArchive::new(fs::File::open(zip_path).map_err(|e| e.to_string())?).map_err(zip_error)?;
    if archive.len() > MAX_ENTRIES {
        return Err("The package has too many files".into());
    }
    fs::create_dir_all(dest).map_err(|e| e.to_string())?;
    let mut total = 0u64;
    for i in 0..archive.len() {
        let mut entry = archive.by_index(i).map_err(zip_error)?;
        let name = entry.name().to_string();
        let bad = || format!("Unexpected file in the package: {name}");
        let Some(rest) = name.strip_prefix(prefix) else {
            return Err(bad());
        };
        if rest.is_empty() {
            continue;
        }
        let rel = Path::new(rest.trim_end_matches('/'));
        if rest.contains('\\')
            || rest.contains(':')
            || !rel.components().all(|c| matches!(c, Component::Normal(_)))
            || entry.enclosed_name().is_none()
        {
            return Err(bad());
        }
        let mode = entry.unix_mode().unwrap_or(0);
        if mode & 0o170000 == 0o120000 {
            return Err(format!("The package contains a link: {name}"));
        }
        let target = dest.join(rel);
        if entry.is_dir() {
            fs::create_dir_all(&target).map_err(|e| e.to_string())?;
            continue;
        }
        total += entry.size();
        if total > MAX_BYTES {
            return Err("The package is larger than expected".into());
        }
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let mut out = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&target)
            .map_err(|e| format!("{}: {e}", target.display()))?;
        let limit = entry.size();
        let copied = io::copy(&mut (&mut entry).take(limit), &mut out).map_err(|e| format!("{name}: {e}"))?;
        if copied != limit {
            return Err(format!("{name} is incomplete in the package"));
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let exec = mode & 0o111 != 0 || rest == helper_name() || rest.starts_with("runtime/bin/") || rest.starts_with("runtime/python/bin/");
            fs::set_permissions(&target, fs::Permissions::from_mode(if exec { 0o755 } else { 0o644 })).map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}

fn files_under(root: &Path, dir: &Path, out: &mut BTreeSet<String>) -> io::Result<()> {
    for e in fs::read_dir(dir)? {
        let e = e?;
        let t = e.file_type()?;
        // verify() has already checked EVERY listed entry. Finder may add
        // this unlisted regular metadata file while extraction is in flight.
        // Links, directories and all other extra names keep the strict rule.
        if t.is_file() && e.file_name() == ".DS_Store" {
            continue;
        }
        if t.is_dir() {
            files_under(root, &e.path(), out)?;
        } else {
            let rel = e.path().strip_prefix(root).map_err(io::Error::other)?.to_path_buf();
            out.insert(rel.to_string_lossy().replace('\\', "/"));
        }
    }
    Ok(())
}

/// Unpacks `zip_path` as version `version` and returns its folder.
pub fn stage(layout: &Layout, version: &str, zip_path: &Path) -> Result<PathBuf, String> {
    layout.authority().map_err(|e| e.to_string())?;
    let staging = layout.updates.join(format!(".staging-{version}"));
    if staging.exists() {
        fs::remove_dir_all(&staging).map_err(|e| e.to_string())?;
    }
    let result = (|| {
        extract(zip_path, &staging, &format!("NFSU2-Generator-{}/", crate::platform()))?;
        let staging = staging.canonicalize().map_err(|e| e.to_string())?;
        let m = crate::verify(&staging)?;
        if m.launcher_protocol != 2 {
            return Err("This update does not support the protected launcher handoff. The current version was kept.".into());
        }
        if m.generator_version != version {
            return Err(format!("The package says version {}, the release says {version}", m.generator_version));
        }
        if !m.files.contains_key(&helper_name()) || !m.files.contains_key(crate::config::CONFIG) {
            return Err("The package lacks its launcher".to_string());
        }
        let mut found = BTreeSet::new();
        files_under(&staging, &staging, &mut found).map_err(|e| e.to_string())?;
        if let Some(extra) = found.iter().find(|f| *f != "bundle.json" && !m.files.contains_key(*f)) {
            return Err(format!("The package contains an unlisted file: {extra}"));
        }
        Ok(())
    })();
    if let Err(e) = result {
        let _ = fs::remove_dir_all(&staging);
        return Err(e);
    }
    let dest = layout.version_dir(version);
    if layout.lease.protects(&dest) {
        let _ = fs::remove_dir_all(&staging);
        return Err("That version is still running. Close the generator and retry.".into());
    }
    if dest.exists() && !remove_package(&dest, &layout.lease) {
        let _ = fs::remove_dir_all(&staging);
        return Err(format!("Move your files out of {} and try again", dest.display()));
    }
    fs::rename(&staging, &dest).map_err(|e| format!("The new version could not be moved into place ({e})"))?;
    Ok(dest)
}

/// Free bytes on the volume holding `path`.
pub fn free_space(path: &Path) -> Option<u64> {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        let c = std::ffi::CString::new(path.as_os_str().as_bytes()).ok()?;
        let mut s: libc::statvfs = unsafe { std::mem::zeroed() };
        if unsafe { libc::statvfs(c.as_ptr(), &mut s) } != 0 {
            return None;
        }
        Some(s.f_bavail as u64 * s.f_frsize as u64)
    }
    #[cfg(windows)]
    {
        use std::os::windows::ffi::OsStrExt;
        let wide: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        let mut free = 0u64;
        let ok = unsafe {
            windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW(wide.as_ptr(), &mut free, std::ptr::null_mut(), std::ptr::null_mut())
        };
        (ok != 0).then_some(free)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    fn zip_with(path: &Path, entries: &[(&str, &[u8], u32)]) {
        let mut w = zip::ZipWriter::new(fs::File::create(path).unwrap());
        for (name, data, mode) in entries {
            let opts = zip::write::SimpleFileOptions::default().unix_permissions(*mode);
            if *mode & 0o170000 == 0o120000 {
                w.add_symlink(*name, "../../outside", opts).unwrap();
            } else {
                w.start_file(*name, opts).unwrap();
                w.write_all(data).unwrap();
            }
        }
        w.finish().unwrap();
    }
    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("nfsu2-install-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&d);
        fs::create_dir_all(&d).unwrap();
        d
    }
    #[test]
    fn extracts_only_the_package_folder() {
        let d = temp("ok");
        let z = d.join("p.zip");
        zip_with(&z, &[("P/a.txt", b"a", 0o644), ("P/runtime/bin/tool", b"t", 0o755)]);
        extract(&z, &d.join("out"), "P/").unwrap();
        assert_eq!(fs::read(d.join("out/a.txt")).unwrap(), b"a");
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(fs::metadata(d.join("out/runtime/bin/tool")).unwrap().permissions().mode() & 0o777, 0o755);
            assert_eq!(fs::metadata(d.join("out/a.txt")).unwrap().permissions().mode() & 0o777, 0o644);
        }
    }
    #[test]
    fn refuses_unsafe_entries() {
        let d = temp("bad");
        for (i, entries) in [
            vec![("P/../evil", &b"x"[..], 0o644)],
            vec![("Other/a", &b"x"[..], 0o644)],
            vec![("P/a\\b", &b"x"[..], 0o644)],
            vec![("P/C:x", &b"x"[..], 0o644)],
            vec![("P/link", &b""[..], 0o120777)],
            vec![("P/a", &b"x"[..], 0o644), ("P/a", &b"y"[..], 0o644)],
        ]
        .into_iter()
        .enumerate()
        {
            let z = d.join(format!("{i}.zip"));
            // ZipWriter itself refuses a duplicate name: that case is covered by it.
            let mut w = zip::ZipWriter::new(fs::File::create(&z).unwrap());
            let mut dup = false;
            for (name, data, mode) in &entries {
                let opts = zip::write::SimpleFileOptions::default().unix_permissions(*mode);
                if *mode & 0o170000 == 0o120000 {
                    w.add_symlink(*name, "../../outside", opts).unwrap();
                } else if w.start_file(*name, opts).is_ok() {
                    w.write_all(data).unwrap();
                } else {
                    dup = true;
                }
            }
            w.finish().unwrap();
            if dup {
                continue;
            }
            let out = d.join(format!("out{i}"));
            assert!(extract(&z, &out, "P/").is_err(), "{entries:?}");
            assert!(!d.join("evil").exists());
        }
    }
}
