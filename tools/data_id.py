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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder', nargs='?', type=Path, default=DIST)
    ap.add_argument('--to', type=Path, help='write data-id.txt here instead of in the folder')
    ap.add_argument('--diff', metavar='ID')
    a = ap.parse_args()
    if a.diff:
        d = diff(a.diff, a.folder)
        for n, what in d:
            print(f'{what:9s} {n}')
        print(f'{len(d)} files differ' if d else 'the same data')
        return
    print(write(a.to or a.folder, a.folder))


if __name__ == '__main__':
    main()
