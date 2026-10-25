#!/usr/bin/env python3
"""An event's barriers (bar-<id>.n2cy) with their collision as a patch on
the world's: the cells the barriers reach held the city's (or the closed
track's) whole collision grid, 37 KB a cell, with the barriers' walls in
it; most of it the world's own, byte for byte (the 107 barrier archives:
171 MB of collision, 31 MB of it what the barriers change).

A patched collision blob (N2CY v3, overlays only):
  'N2CP', u32 entries, then per 1 m cell of the 64x64 grid that changes:
  u16 k (j*64 + i), s16 low, s16 high, u8 wall bits, u8 pad, s16 middle,
  u16 wall shape, s16 second middle (14 bytes): the overlay's values in the
  base cell's height scale (its base and unit), the fields past the base
  blob's layers (no middle, no shapes) left out when applied.
The loader (psp-viewer/world.c) reads the base archive's blob of the same
cell and writes the entries over a copy of it; tools/n2city.py the same.
A cell becomes a patch only when the result is the overlay's grid exactly
(heights to within half a unit) with the same terrain section; otherwise
it stays whole, as do cells the base has no collision for.

  n2cy_patch.py BASE BAR [OUT]       BAR's collision as patches on BASE's (OUT
      default: in place)
  n2cy_patch.py --dist DIR --out DIR  every bar-*.n2cy of a dist, each on the
      world archive (city or track-*) sharing most of its collision, into
      another folder (the dist untouched)
"""
import argparse
import glob
import os
from pathlib import Path
import struct

N = 64
NN = N * N
MAGIC = b'N2CP'
ENTRY = struct.Struct('<HhhBBhHh')   # k, low, high, wall, pad, middle, shape, second middle
NONE = -32768


_cache = {}


def read_archive(path, keep=False):
    """(bytes, {(i, j): (cell number, index entry)}); keep: remembered (the
    world archives, read once for every barrier)."""
    if keep and str(path) in _cache:
        return _cache[str(path)]
    data = Path(path).read_bytes()
    if data[:4] != b'N2CY' or struct.unpack_from('<I', data, 4)[0] not in (2, 3):
        raise ValueError(f'{path}: not an N2CY v2/v3 archive')
    i0, j0, w, h, nt = struct.unpack_from('<2i3I', data, 12)
    io, to = struct.unpack_from('<2I', data, 48)
    cindex = {}
    for c in range(w * h):
        e = list(struct.unpack_from('<6I', data, io + 24 * c))
        cindex[(i0 + c % w, j0 + c // w)] = (c, e)
    if keep:
        _cache[str(path)] = (data, cindex)
    return data, cindex


def _layers_end(blob):
    terrain = struct.unpack_from('<I', blob, 8)[0]
    return terrain if terrain else len(blob)


def _fields(blob, end):
    """The grid's arrays as stored (raw s16/u8/u16), absent ones None."""
    lo = struct.unpack_from(f'<{NN}h', blob, 16)
    hi = struct.unpack_from(f'<{NN}h', blob, 16 + 2 * NN)
    wall = blob[16 + 4 * NN:16 + 5 * NN]
    mid = struct.unpack_from(f'<{NN}h', blob, 16 + 5 * NN) if end >= 16 + 7 * NN else None
    shape = struct.unpack_from(f'<{NN}H', blob, 16 + 7 * NN) if end >= 16 + 9 * NN else None
    mid2 = struct.unpack_from(f'<{NN}h', blob, 16 + 9 * NN) if end >= 16 + 11 * NN else None
    return lo, hi, wall, mid, shape, mid2


def make_patch(over, base):
    """over's collision blob as a patch on base's, or None when it cannot
    be one exactly (or is not smaller)."""
    oe, be = _layers_end(over), _layers_end(base)
    if oe != be or over[oe:] != base[be:] or over[8:12] != base[8:12]:
        return None   # another layout or another terrain section
    ob, ou = struct.unpack_from('<2f', over, 0)
    bb, bu = struct.unpack_from('<2f', base, 0)
    O, B = _fields(over, oe), _fields(base, be)

    def rescale(v):   # an overlay height in the base's scale
        if v == NONE or (ob == bb and ou == bu):
            return v
        h = ob + v * ou
        r = round((h - bb) / bu)
        if not -32767 <= r <= 32767 or abs(bb + r * bu - h) > ou * .5 + 1e-6:
            raise ValueError
        return r
    out = []
    try:
        for k in range(NN):
            vals = []
            for f in (0, 1, 3, 5):   # heights
                if O[f] is None:
                    vals.append(NONE)
                else:
                    vals.append(rescale(O[f][k]))
            wall = O[2][k]
            shape = O[4][k] if O[4] is not None else 0xffff
            same = (vals[0] == B[0][k] and vals[1] == B[1][k] and wall == B[2][k]
                    and (O[3] is None or vals[2] == B[3][k]) and (O[4] is None or shape == B[4][k])
                    and (O[5] is None or vals[3] == B[5][k]))
            if not same:
                out.append(ENTRY.pack(k, vals[0], vals[1], wall, 0, vals[2], shape, vals[3]))
    except ValueError:
        return None
    patch = MAGIC + struct.pack('<I', len(out)) + b''.join(out)
    return patch if len(patch) < len(over) else None


def apply_patch(base, patch):
    """base's collision blob with the patch's cells written over it."""
    if patch[:4] != MAGIC:
        raise ValueError('not a collision patch')
    out = bytearray(base)
    end = _layers_end(base)
    n = struct.unpack_from('<I', patch, 4)[0]
    for e in range(n):
        k, lo, hi, wall, _, mid, shape, mid2 = ENTRY.unpack_from(patch, 8 + ENTRY.size * e)
        struct.pack_into('<h', out, 16 + 2 * k, lo)
        struct.pack_into('<h', out, 16 + 2 * NN + 2 * k, hi)
        out[16 + 4 * NN + k] = wall
        if end >= 16 + 7 * NN:
            struct.pack_into('<h', out, 16 + 5 * NN + 2 * k, mid)
        if end >= 16 + 9 * NN:
            struct.pack_into('<H', out, 16 + 7 * NN + 2 * k, shape)
        if end >= 16 + 11 * NN:
            struct.pack_into('<h', out, 16 + 9 * NN + 2 * k, mid2)
    return bytes(out)


def _col(data, e):
    return data[e[4]:e[4] + e[5]] if e[5] else b''


def patch_archive(bar, base, out=None):
    """bar's collision as patches on base's, written to out (default bar).
    Returns (bytes before, bytes after, cells patched, cells kept whole)."""
    data, cindex = read_archive(bar)
    bdata, bindex = read_archive(base, keep=True)
    if struct.unpack_from('<I', data, 4)[0] == 3:   # already patched: as it is (a dist after the change, its package)
        if out and Path(out).resolve() != Path(bar).resolve():
            dst = Path(out)
            if dst.is_symlink():
                dst.unlink()
            dst.write_bytes(data)
        return len(data), len(data), 0, 0
    io, to = struct.unpack_from('<2I', data, 48)
    nt = struct.unpack_from('<I', data, 28)[0]
    tindex = [list(struct.unpack_from('<3I', data, to + 12 * t)) for t in range(nt)]
    new_col, patched, whole = {}, 0, 0
    for cell, (c, e) in cindex.items():
        if not e[5]:
            continue
        be = bindex.get(cell)
        p = make_patch(_col(data, e), _col(bdata, be[1])) if be and be[1][5] else None
        if p is None:
            whole += 1
        else:
            new_col[c] = p
            patched += 1
    blobs = [(e[2 * k], ('cell', c, k)) for c, e in (v for v in cindex.values()) for k in range(3) if e[2 * k + 1]]
    blobs += [(t[1], ('tex', i, 0)) for i, t in enumerate(tindex) if t[2]]
    blobs.sort()
    first = min(o for o, _ in blobs) if blobs else len(data)
    out_b = bytearray(data[:first])
    entries = {c: e[:] for c, e in cindex.values()}
    for off, (kind, i, k) in blobs:
        if kind == 'cell':
            blob = new_col[i] if k == 2 and i in new_col else data[off:off + entries[i][2 * k + 1]]
        else:
            blob = data[off:off + tindex[i][2]]
        out_b.extend(bytes((-len(out_b)) % 16))
        if kind == 'cell':
            entries[i][2 * k], entries[i][2 * k + 1] = len(out_b), len(blob)
        else:
            tindex[i][1], tindex[i][2] = len(out_b), len(blob)
        out_b.extend(blob)
    for c, e in entries.items():
        struct.pack_into('<6I', out_b, io + 24 * c, *e)
    for t, e in enumerate(tindex):
        struct.pack_into('<3I', out_b, to + 12 * t, *e)
    if patched:
        struct.pack_into('<I', out_b, 4, 3)
    dst = Path(out or bar)
    tmp = dst.with_name(dst.name + '.patch-tmp')
    tmp.write_bytes(out_b)
    if dst.is_symlink():
        dst.unlink()
    tmp.replace(dst)
    return len(data), len(out_b), patched, whole


def base_of(bar, worlds):
    """The world archive sharing most of bar's collision cells (then the
    most of them byte for byte)."""
    data, cindex = read_archive(bar)
    best, score = None, None
    for w in worlds:
        wdata, windex = read_archive(w, keep=True)
        cells = [(windex[cell][1], e) for cell, (_, e) in cindex.items() if e[5] and cell in windex and windex[cell][1][5]]
        s = (len(cells), sum(1 for we, e in cells if _col(data, e) == _col(wdata, we)))
        if score is None or s > score:
            best, score = w, s
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('base', nargs='?')
    ap.add_argument('bar', nargs='?')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--dist')
    ap.add_argument('--out', dest='out_dir')
    a = ap.parse_args()
    if a.dist:
        if not a.out_dir:
            ap.error('--dist pede --out (a dist não é alterada)')
        out = Path(a.out_dir)
        out.mkdir(parents=True, exist_ok=True)
        worlds = [os.path.join(a.dist, 'city.n2cy')] + sorted(glob.glob(os.path.join(a.dist, 'track-*.n2cy')))
        before = after = 0
        for bar in sorted(glob.glob(os.path.join(a.dist, 'bar-*.n2cy'))):
            base = base_of(bar, worlds)
            b, f, p, w = patch_archive(bar, base, out / os.path.basename(bar))
            before += b
            after += f
            print(f'{os.path.basename(bar)} sobre {os.path.basename(base)}: {b} -> {f} bytes, {p} células em remendo, {w} inteiras', flush=True)
        print(f'total {before / 2**20:.1f} -> {after / 2**20:.1f} MB')
    else:
        if not (a.base and a.bar):
            ap.error('BASE BAR [OUT] ou --dist DIR --out DIR')
        print(patch_archive(a.bar, a.base, a.out))


if __name__ == '__main__':
    main()
