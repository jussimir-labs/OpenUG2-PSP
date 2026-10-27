#!/usr/bin/env python3
"""The converted data's version: a short id of what the game loads.

The screenshots' marker (psp-viewer/main.c repro_marker) says the build
(tools/build_id.sh) and, after a "d", this id of the data folder beside it:
4 hex of the hash of every converted file's content (the cars, the city,
the races, the audio...; not the build, the saves, the test files or the
conversion reports). The game reads it from data-id.txt next to it; this
tool writes that file, and keeps the list of the files' hashes as
nfsu2-builds/data-<id>.json in the repository's git dir (shared by its
worktrees, never committed), so a screenshot's data can be told apart from
the current folder's file by file (--diff). A stale car pack once put the
wheels outside a 350Z's body with the code all right: the marker's d<id>
shows it.

    data_id.py [DIR]               the id of DIR (default the dist), data-id.txt written there
    data_id.py [DIR] --to GAME     the id of DIR written to GAME/data-id.txt (a test's folder)
    data_id.py [DIR] --diff ID     the files that differ between data ID and DIR
    data_id.py GAME --normalize-package-owned
                                 normalize a newly owned final packed/loose package

Files are hashed once: their hashes are cached by size and mtime
(nfsu2-builds/data-cache.json).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / 'dist/PSP/GAME/NFSU2VIEW'
# not the data: the build, the icons, saves, the tests' and the tools' files
SKIP_SUFFIX = {'.json', '.pbp', '.prx', '.sav', '.png', '.ppm', '.csv', '.bin', '.log', '.patch'}
SKIP_NAME = {'marks.txt', 'smoke-test.txt', 'repro.txt', 'data-id.txt', 'paint-tune.txt'}
_lock = threading.Lock()


def store():
    try:
        common = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                capture_output=True, text=True, check=True).stdout.strip()
        d = Path(common) / 'nfsu2-builds'
    except (OSError, subprocess.CalledProcessError):
        d = ROOT / '.tools/nfsu2-builds'
    d.mkdir(parents=True, exist_ok=True)
    return d


def data_files(folder):
    for p in sorted(Path(folder).iterdir()):
        n = p.name
        if n.startswith(('viewer-', '.')) or n in SKIP_NAME or p.suffix.lower() in SKIP_SUFFIX or not p.is_file():
            continue
        yield p


def _write_json(path, obj):
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=path.name + '.')
    with os.fdopen(fd, 'w') as f:
        json.dump(obj, f, indent=0, sort_keys=True)
    os.replace(tmp, path)


def compute(folder=DIST):
    """(id, {file: sha1}) of a data folder; the manifest kept in the store."""
    with _lock:   # tests make their games from threads: one hashing pass
        st = store()
        cache_path = st / 'data-cache.json'
        try:
            cache = json.loads(cache_path.read_text())
        except (OSError, ValueError):
            cache = {}
        files, changed = {}, False
        for p in data_files(folder):
            s = p.stat()   # through the links (NFSU2_DIST farms, the tests' games)
            real = str(p.resolve())
            key = f'{s.st_size}:{s.st_mtime_ns}'
            c = cache.get(real)
            if not c or c[0] != key:
                with open(p, 'rb') as f:
                    c = [key, hashlib.file_digest(f, 'sha1').hexdigest()]
                cache[real] = c
                changed = True
            files[p.name] = c[1]
        if changed:
            _write_json(cache_path, cache)
        ident = hashlib.sha1(''.join(f'{n} {h}\n' for n, h in sorted(files.items())).encode()).hexdigest()[:4]
        manifest = st / f'data-{ident}.json'
        if not manifest.exists():
            _write_json(manifest, dict(id=ident, folder=str(Path(folder).resolve()),
                                       made=time.strftime('%Y-%m-%d %H:%M:%S'), files=files))
        return ident, files


def write(game, folder=DIST):
    """data-id.txt in game (its own file, never through a link into the dist)."""
    ident, _ = compute(folder)
    p = Path(game) / 'data-id.txt'
    p.unlink(missing_ok=True)
    p.write_text(ident + '\n')
    return ident


def diff(old, folder=DIST):
    """[(file, what)] between data old (its manifest) and folder."""
    m = store() / f'data-{old}.json'
    if not m.exists():
        raise SystemExit(f'data {old}: no manifest in {store()} (made on another machine, or before tools/data_id.py)')
    a = json.loads(m.read_text())['files']
    _, b = compute(folder)
    out = [(n, 'only then') for n in sorted(a.keys() - b.keys())] + [(n, 'only now') for n in sorted(b.keys() - a.keys())]
    return out + [(n, 'changed') for n in sorted(a.keys() & b.keys()) if a[n] != b[n]]


# The final package has a different physical layout from the converter's dist:
# barriers may be patched and small files packed. Keep compute() above unchanged.
_PACKAGE_OUTPUTS = {
    'marks.txt', 'smoke-test.txt', 'track-smoke.txt', 'repro.txt', 'repro-replay.txt',
    'paint-tune.txt', 'street-tune.txt', 'data-id.txt', 'pack.n2pk', 'ms-runs.txt',
    'ms-done.txt', 'hostcmd.txt', 'boot.txt', 'probe.txt', 'probe-detail.txt',
    'bench-dir.txt', 'card-bench.txt', 'open-bench.txt', 'intro-force.txt',
    'skipboot.txt', 'language.txt', 'bug-at.txt', 'crash-test.txt', 'brake-test.txt',
    'io-trace.txt', 'ms-stall.txt', 'memory-admission.on', 'savedata-dialog.on',
    'outrun-engage.txt',
}


def _package_data_name(name):
    low = name.lower()
    return (not name.startswith(('.', 'viewer-', 'track-stream-', 'ark-'))
            and low not in _PACKAGE_OUTPUTS
            and Path(low).suffix not in SKIP_SUFFIX | {'.elf', '.o', '.d', '.jsonl', '.n2bug'}
            and not low.endswith(('-boot.txt', '-card-bench.txt', '-open-bench.txt')))


def _plain_package_folder(folder):
    """Reject link traversal before any owned-package mutation (reads too)."""
    folder = Path(os.path.abspath(folder))
    for part in (*reversed(folder.parents), folder):
        if part.is_symlink():
            raise ValueError(f'package path contains a symlink: {part}')
    if not folder.is_dir():
        raise ValueError(f'package folder missing: {folder}')
    return folder


def _pack_entries(path):
    """Validated N2PK1 index: name -> (absolute offset, bytes)."""
    import struct
    total = path.stat().st_size
    with path.open('rb') as stream:
        header = stream.read(16)
        if len(header) != 16:
            raise ValueError('truncated N2PK header')
        magic, version, count, index_bytes = struct.unpack('<4sIII', header)
        if (magic != b'N2PK' or version != 1 or count > 100000
                or index_bytes > 32 << 20 or index_bytes > total - 16):
            raise ValueError('invalid N2PK1 header')
        index = stream.read(index_bytes)
    entries, ranges, at = {}, [], 0
    for _ in range(count):
        if at + 10 > len(index):
            raise ValueError('truncated N2PK index')
        offset, size, length = struct.unpack_from('<IIH', index, at)
        end = at + 10 + length
        padded = (end + 3) & ~3
        if not length or padded > len(index):
            raise ValueError('invalid N2PK name length')
        try:
            name = index[at + 10:end].decode('utf-8')
        except UnicodeError as error:
            raise ValueError('invalid N2PK name encoding') from error
        if (name in entries or name in ('.', '..') or '/' in name or '\\' in name
                or '\x00' in name or any(index[end:padded])):
            raise ValueError('invalid or duplicate N2PK name')
        if offset % 16 or offset < 16 + index_bytes or offset + size > total:
            raise ValueError('N2PK payload outside file')
        entries[name] = (offset, size)
        if size:
            ranges.append((offset, offset + size))
        at = padded
    if at != len(index):
        raise ValueError('unconsumed N2PK index bytes')
    ranges.sort()
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError('overlapping N2PK payloads')
    return entries


def _range_sha1(stream, offset, size):
    stream.seek(offset)
    digest = hashlib.sha1()
    while size:
        block = stream.read(min(size, 4 << 20))
        if not block:
            raise ValueError('truncated package payload')
        digest.update(block)
        size -= len(block)
    return digest.hexdigest()


def _package_view(folder):
    folder = _plain_package_folder(folder)
    files = {}
    for path in sorted(folder.iterdir()):
        if path.is_symlink():
            raise ValueError(f'package contains a symlink: {path.name}')
        if path.is_file():
            files[path.name] = path
    pack = files.get('pack.n2pk')
    entries = _pack_entries(pack) if pack else {}
    if entries.keys() & files.keys():
        raise ValueError('ambiguous packed and loose package names')
    hashes = {}
    if pack:
        with pack.open('rb') as stream:
            for name, (offset, size) in entries.items():
                if _package_data_name(name):
                    hashes[name] = _range_sha1(stream, offset, size)
    for name, path in files.items():
        if _package_data_name(name):
            with path.open('rb') as stream:
                hashes[name] = _range_sha1(stream, 0, path.stat().st_size)
    ident = hashlib.sha1(''.join(f'{name} {digest}\n' for name, digest in sorted(hashes.items())).encode()).hexdigest()[:4]
    return folder, ident, hashes, entries


def logical_package(folder):
    """Read-only (id, {logical name: sha1}) after packing/barrier patching.

    No cache or historical ledger is consulted or changed. Packed and loose
    layouts hash identically; data-id.txt never participates in its own ID.
    """
    _, ident, hashes, _ = _package_view(folder)
    return ident, hashes


def normalize_package(folder, *, owned=False):
    """Normalize a newly owned final package, never an original input folder.

    The explicit owned=True opt-in is required. Link paths are refused and
    replacement is atomic, including for hardlinked loose markers/packs.
    All pack bytes except its existing five-byte data-id payload stay intact.
    Returns the same (id, hashes) as logical_package; no ledger is modified.
    """
    import shutil
    if owned is not True:
        raise ValueError('normalization requires an explicitly owned output package')
    folder, ident, hashes, entries = _package_view(folder)
    packed = entries.get('data-id.txt')
    target = folder / ('pack.n2pk' if packed else 'data-id.txt')
    if packed and packed[1] != 5:
        raise ValueError('packed data-id.txt must occupy exactly five bytes')
    payload = (ident + '\n').encode('ascii')
    fd, temporary = tempfile.mkstemp(dir=folder, prefix='.data-id-')
    try:
        with os.fdopen(fd, 'wb') as dest:
            if packed:
                with target.open('rb') as source:
                    shutil.copyfileobj(source, dest, 4 << 20)
                dest.seek(packed[0])
            dest.write(payload)
            dest.flush()
            os.fsync(dest.fileno())
        if target.exists():
            os.chmod(temporary, target.stat().st_mode & 0o777)
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return ident, hashes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder', nargs='?', type=Path, default=DIST)
    ap.add_argument('--to', type=Path, help='write data-id.txt here instead of in the folder')
    ap.add_argument('--diff', metavar='ID')
    ap.add_argument('--normalize-package-owned', action='store_true',
                    help='normalize an explicitly owned final package, after packing')
    a = ap.parse_args()
    if a.normalize_package_owned:
        if a.to or a.diff:
            ap.error('--normalize-package-owned cannot be combined with --to or --diff')
        print(normalize_package(a.folder, owned=True)[0])
        return
    if a.diff:
        d = diff(a.diff, a.folder)
        for n, what in d:
            print(f'{what:9s} {n}')
        print(f'{len(d)} files differ' if d else 'the same data')
        return
    print(write(a.to or a.folder, a.folder))


if __name__ == '__main__':
    main()
