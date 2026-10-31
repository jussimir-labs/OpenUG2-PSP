#!/usr/bin/env python3
"""Extract the user's PS2 disc (ISO 9660) without changing the ISO.

The converters read the PS2's files from game/ps2 (NFSUNDER/ZDIR.BIN, the
ZZDATA archives, the executable SLUS_210.65) when they run with --source
ps2 (tools/n2source.py). Stdlib only: the disc is plain ISO 9660 (a DVD's
UDF bridge carries the same files), read through the primary volume
descriptor at sector 16; a file's extent is one run of 2048-byte sectors.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
SECTOR = 2048


class Iso9660:
    def __init__(self, path):
        self.path = Path(path)
        self.f = self.path.open('rb')
        self.size = self.path.stat().st_size
        try:
            pvd = self.read(16, SECTOR)
            if len(pvd) != SECTOR or pvd[:7] != b'\x01CD001\x01':
                raise ValueError('not an ISO 9660 image (no primary descriptor in sector 16)')
            self.root = self.record(pvd, 156)
        except BaseException:
            self.f.close()
            raise

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def read(self, sector, size):
        if sector < 0 or size < 0 or sector * SECTOR + size > self.size:
            raise ValueError('the image ends before a file does: incomplete ISO')
        self.f.seek(sector * SECTOR)
        data = self.f.read(size)
        if len(data) != size:
            raise ValueError('short ISO read')
        return data

    @staticmethod
    def record(d, at):
        """A directory record: (name, extent, size, is_dir)."""
        if at < 0 or at + 34 > len(d):
            raise ValueError('registro ISO truncado')
        n = d[at]
        if n < 34 or at + n > len(d) or at // SECTOR != (at + n - 1) // SECTOR:
            raise ValueError('invalid ISO record length')
        if d[at + 32] > n - 33 or d[at + 26] or d[at + 27] or d[at + 25] & 128:
            raise ValueError('interleaved or multi-extent ISO records are not supported')
        extent, size = struct.unpack_from('<I', d, at + 2)[0], struct.unpack_from('<I', d, at + 10)[0]
        flags, name_len = d[at + 25], d[at + 32]
        name = d[at + 33:at + 33 + name_len].decode('ascii')
        return name, extent, size, bool(flags & 2), n

    def listdir(self, extent, size):
        if size > 4 << 20:
            raise ValueError('ISO directory exceeds the limit')
        d = self.read(extent, size)
        at = 0
        names = set()
        while at < len(d):
            n = d[at]
            if n == 0:                       # records never cross a sector: the rest is padding
                at = (at // SECTOR + 1) * SECTOR
                continue
            name, ext, sz, is_dir, _ = self.record(d, at)
            at += n
            if name in ('\0', '\1'):
                continue
            name = name.split(';')[0]
            if not name or name in ('.', '..') or '/' in name or '\\' in name or ':' in name or '\0' in name or name.casefold() in names:
                raise ValueError('nome duplicado ou inseguro na ISO')
            names.add(name.casefold())
            yield name, ext, sz, is_dir

    def walk(self, extent=None, size=None, prefix=Path(), ancestors=()):
        if extent is None:
            _, extent, size, _, _ = self.root
        if extent in ancestors or len(ancestors) >= 16:
            raise ValueError('ciclo ou profundidade excessiva na ISO')
        ancestors = ancestors + (extent,)
        for name, ext, sz, is_dir in self.listdir(extent, size):
            rel = prefix / name
            if rel.is_absolute() or '..' in rel.parts or not name:
                raise ValueError(f'unsafe ISO path: {rel}')
            if is_dir:
                yield from self.walk(ext, sz, rel, ancestors)
            else:
                yield rel, ext, sz

    def extract(self, extent, size, dest):
        # Empty ISO files have no data extent; mastering tools may use a
        # sentinel LBA (e.g. 0xfffffff0). Never seek it or relax nonempty bounds.
        if extent < 0 or size < 0 or (size and extent * SECTOR + size > self.size):
            raise ValueError(f'{dest.name} extends past the end of the ISO: incomplete image')
        if size:
            self.f.seek(extent * SECTOR)
        sha = hashlib.sha256()
        with dest.open('wb') as out:
            left = size
            while left:
                chunk = self.f.read(min(left, 1 << 22))
                if not chunk:
                    raise ValueError(f'{dest.name}: short read')
                out.write(chunk)
                sha.update(chunk)
                left -= len(chunk)
        return sha.hexdigest()


def boot_file(iso):
    """The executable SYSTEM.CNF names (SLUS_210.65 on the USA disc)."""
    for rel, ext, size in iso.walk():
        if rel.as_posix().upper() == 'SYSTEM.CNF':
            text = iso.read(ext, size).decode('ascii', 'replace')
            for line in text.splitlines():
                if line.replace(' ', '').upper().startswith('BOOT2='):
                    return line.split('\\')[-1].split(';')[0].strip()
    return None


def extract(iso_path, out, say=print):
    """Every file of the disc into out; returns the report's file list."""
    files = []
    with Iso9660(iso_path) as iso:
        boot = boot_file(iso)
        if not boot:
            raise ValueError('no SYSTEM.CNF with BOOT2: not a PS2 disc')
        entries = list(iso.walk())
        if not any(r.as_posix().upper() == 'NFSUNDER/ZDIR.BIN' for r, _, _ in entries):
            raise ValueError('the disc has no NFSUNDER/ZDIR.BIN: not Need for Speed Underground 2')
        total, done = sum(s for _, _, s in entries) or 1, 0
        for rel, ext, size in entries:
            if out.is_symlink() or any((out / Path(*rel.parts[:i])).is_symlink() for i in range(1, len(rel.parts) + 1)):
                raise ValueError('extraction does not accept a destination containing symbolic links')
            dest = out / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            sha = iso.extract(ext, size, dest)
            files.append({'path': rel.as_posix(), 'size': size, 'sha256': sha})
            done += size
            if size > 64 << 20:
                say(f'  {rel.as_posix()} ({done * 100 // total}%)')
    return boot, files


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('iso', type=Path)
    ap.add_argument('--out', type=Path, default=ROOT / 'game/ps2')
    args = ap.parse_args()
    if args.out.exists():
        ap.error('destination already exists; choose another directory to preserve the extraction')
    args.out.mkdir(parents=True)
    boot, files = extract(args.iso, args.out)
    report = {'iso': str(args.iso.resolve()), 'boot': boot, 'total_bytes': sum(f['size'] for f in files), 'files': files}
    (ROOT / 'reports').mkdir(exist_ok=True)
    (ROOT / 'reports/ps2-extraction.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(files)} files extracted and verified in {args.out} (executable {boot})')


if __name__ == '__main__':
    main()
