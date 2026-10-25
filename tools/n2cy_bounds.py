#!/usr/bin/env python3
"""Each geometry blob of a city archive (N2CY v2) with its bounds table:
what the PSP's loader used to find by reading every vertex of a part.

The loader needs, per batch, the box of its vertices (s16, as stored: the
GE's box test, the part's frustum box) and its triangles (a strip's, less
the joins'). It scanned the vertices for them: on the 4006 circuit (PPSSPP
with the PSP-3000's timing) 5.1 s of the loader's 11.2 s of CPU in 5 min, a
near cell's ~165 KB at ~28 us/KB, mostly in the frames that came out late.

The table goes at the blob's end, 16-aligned, its offset in the blob's
header (u32 at +4, 0 before: no table): per batch s16 lo[3], s16 hi[3],
u32 triangles. Blobs without it load as before (psp-viewer/world.c).

  n2cy_bounds.py ARCHIVE [OUT]   an archive's blobs given their tables: the
      same archive at OUT (default: in place, through a temporary file),
      with the dates of the archive and of its mipmaps (.n2mp, whose header
      records the archive's size, set again) kept, so that tools/build.sh
      does not take it for a new city and convert the races again.
  n2cy_bounds.py --if-missing ARCHIVE...   the same, in place, for those
      with a blob still without its table (tools/build.sh: a city converted
      before the tables, kept by the build, loaded its near cells at ~36
      us/KB on a PSP-3000 instead of ~1; the sprint 4101 at 97.5% of its
      frames on time instead of 99.2%)
convert_city.py writes the tables itself (with_bounds).
"""
import os
from pathlib import Path
import struct
import sys

try:
    import numpy as np
except ImportError:      # the system's Python: the same, slower
    np = None


def _al(x):
    return (x + 15) & ~15


def batches(blob):
    """(header offset, nv, ni, vertices offset) per batch, and the end of
    the last (where a table starts)."""
    n, = struct.unpack_from('<I', blob, 0)
    at, out = 16, []
    for _ in range(n):
        nv, ni = struct.unpack_from('<2I', blob, at + 8)
        out.append((at, nv, ni, at + 48))
        at = _al(at + 48 + nv * 12)
        if ni:
            at = _al(at + ni * 2)
    return out, at


def bounds(blob, vat, nv, ni):
    """(lo[3], hi[3], triangles) of a batch, as world.c's scan found them."""
    raw = blob[vat:vat + nv * 12]
    if np is not None:
        v = np.frombuffer(raw, dtype='<i2').reshape(nv, 6)[:, 3:6]
        lo, hi = v.min(axis=0).tolist(), v.max(axis=0).tolist()
        if ni:
            return lo, hi, ni // 3
        w = np.frombuffer(raw, dtype='<u4').reshape(nv, 3)
        same = lambda a, b: (w[a] == w[b]).all(axis=1)
        k = np.arange(max(0, nv - 2))
        return lo, hi, int((~same(k, k + 1) & ~same(k + 1, k + 2) & ~same(k, k + 2)).sum())
    pos = [struct.unpack_from('<3h', raw, 12 * j + 6) for j in range(nv)]
    lo = [min(p[c] for p in pos) for c in range(3)]
    hi = [max(p[c] for p in pos) for c in range(3)]
    if ni:
        return lo, hi, ni // 3
    vs = [raw[12 * j:12 * j + 12] for j in range(nv)]
    tris = sum(1 for j in range(nv - 2) if vs[j] != vs[j + 1] and vs[j + 1] != vs[j + 2] and vs[j] != vs[j + 2])
    return lo, hi, tris


def with_bounds(blob):
    """The geometry blob with its bounds table (the same blob when empty or
    when it already has one)."""
    if not blob or struct.unpack_from('<I', blob, 4)[0]:
        return blob
    found, end = batches(blob)
    if end != len(blob):
        raise ValueError('geometry blob layout mismatch')
    table = b''.join(struct.pack('<6hI', *lo, *hi, t) for lo, hi, t in (bounds(blob, vat, nv, ni) for _, nv, ni, vat in found))
    out = bytearray(blob) + table
    out.extend(bytes((-len(out)) % 16))
    struct.pack_into('<I', out, 4, end)
    return bytes(out)


def missing(path):
    """How many near and far blobs of the archive have no table yet."""
    with Path(path).open('rb') as f:
        head = f.read(64)
        if head[:4] != b'N2CY' or struct.unpack_from('<I', head, 4)[0] != 2:
            raise ValueError(f'{path}: not an N2CY v2 archive')
        w, h, _ = struct.unpack_from('<3I', head, 20)
        io, = struct.unpack_from('<I', head, 48)
        f.seek(io)
        index = f.read(24 * w * h)
        n = 0
        for c in range(w * h):
            e = struct.unpack_from('<6I', index, 24 * c)
            for k in range(2):
                if e[2 * k + 1]:
                    f.seek(e[2 * k])
                    n += struct.unpack('<2I', f.read(8))[1] == 0
    return n


def upgrade(src, dst=None):
    """src's near and far blobs with their tables, into dst (default src)."""
    src = Path(src)
    dst = Path(dst) if dst else src
    with src.open('rb') as f:
        data = f.read()
    head = bytearray(data[:64])
    if head[:4] != b'N2CY' or struct.unpack_from('<I', head, 4)[0] != 2:
        raise ValueError(f'{src}: not an N2CY v2 archive')
    w, h, nt = struct.unpack_from('<3I', head, 20)
    io, to = struct.unpack_from('<2I', head, 48)
    cindex = [list(struct.unpack_from('<6I', data, io + 24 * c)) for c in range(w * h)]
    tindex = [list(struct.unpack_from('<3I', data, to + 12 * t)) for t in range(nt)]
    # every blob in the file's order (the cells' locality, the textures'),
    # geometry given its table, written again one after the other
    blobs = [(c[2 * k], ('cell', i, k)) for i, c in enumerate(cindex) for k in range(3) if c[2 * k + 1]]
    blobs += [(t[1], ('tex', i, 0)) for i, t in enumerate(tindex) if t[2]]
    blobs.sort()
    first = min(o for o, _ in blobs) if blobs else _al(to + 12 * nt)
    out = bytearray(data[:first])
    added = 0
    for off, (kind, i, k) in blobs:
        size = cindex[i][2 * k + 1] if kind == 'cell' else tindex[i][2]
        blob = data[off:off + size]
        if kind == 'cell' and k < 2:
            new = with_bounds(blob)
            added += len(new) - len(blob)
            blob = new
        out.extend(bytes((-len(out)) % 16))
        if kind == 'cell':
            cindex[i][2 * k], cindex[i][2 * k + 1] = len(out), len(blob)
        else:
            tindex[i][1], tindex[i][2] = len(out), len(blob)
        out.extend(blob)
    for c, e in enumerate(cindex):
        struct.pack_into('<6I', out, io + 24 * c, *e)
    for t, e in enumerate(tindex):
        struct.pack_into('<3I', out, to + 12 * t, *e)
    times = os.stat(src)
    tmp = dst.with_name(dst.name + '.bounds-tmp')
    tmp.write_bytes(out)
    os.utime(tmp, ns=(times.st_atime_ns, times.st_mtime_ns))
    if dst.is_symlink():
        dst.unlink()
    tmp.replace(dst)
    # the mipmaps beside it record the archive's size (world.c mips_open):
    # the destination's only, written as a new file (a hard link elsewhere,
    # another dist's, keeps the old size with the old archive)
    mips = dst.with_suffix('.n2mp')
    if mips.exists() and not mips.is_symlink():
        m = bytearray(mips.read_bytes())
        if m[:4] != b'N2MP' or struct.unpack_from('<I', m, 8)[0] != len(data):
            # made for another archive already: world.c refuses it, the
            # city draws without mipmaps (2026-10-14); build.sh redoes it
            print(f'{mips}: mipmaps de outro arquivo, a refazer (convert_mips.py --if-stale)', file=sys.stderr)
        else:
            mt = os.stat(mips)
            struct.pack_into('<I', m, 8, len(out))
            tmp = mips.with_name(mips.name + '.bounds-tmp')
            tmp.write_bytes(m)
            os.utime(tmp, ns=(mt.st_atime_ns, mt.st_mtime_ns))
            tmp.replace(mips)
    return len(data), len(out), added


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--if-missing':
        for arc in sys.argv[2:]:
            if missing(arc):
                before, after, added = upgrade(arc)
                print(f'{arc}: {before} -> {after} bytes ({added} of tables)')
        sys.exit(0)
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    before, after, added = upgrade(*sys.argv[1:])
    print(f'{sys.argv[1]}: {before} -> {after} bytes ({added} of tables)')
