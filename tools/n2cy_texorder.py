#!/usr/bin/env python3
"""A city archive's (N2CY v2) textures laid out in the order its parts first
use them, so that a part's textures not resident yet sit side by side.

On the Memory Stick each read is a command of ~3.5 ms whatever its size
(the card-bench: 4 KB anywhere in the archive), the loader's CPU while it
waits. The 4127 read ~2,150 textures of ~3 KB each in a race, one command
each, and a part's 16.6 textures lay across the archive's 6.4 MB of them
(13.7 reads a part, those 32 KB apart joined). The parts are in the file in
the cells' order (their locality); each texture goes where the first part
that draws with it is, so the ones a part brings in are neighbours
(psp-viewer/world.c race_join_textures reads them together).

Only the textures move: each still 16-aligned, so the archive keeps its
size, its cells' blobs their offsets and its mipmaps (.n2mp, by texture
index) their match. The archive's and the mipmaps' dates are kept, as
tools/n2cy_bounds.py does, so tools/build.sh does not take it for a new
city.

  n2cy_texorder.py ARCHIVE [OUT]   ARCHIVE's textures reordered, into OUT
      (default: in place, through a temporary file)
  n2cy_texorder.py --if-unordered ARCHIVE  in place, unless laid out so
      already (tools/build.sh: a city converted again is reordered once)
  n2cy_texorder.py --check ARCHIVE  reads a part (near, far) for its
      textures, joined 32 KB apart: as laid out, and reordered
"""
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import n2cy_bounds  # noqa: E402

JOIN_GAP = 32 << 10
JOIN_SPAN = 512 << 10


def _al(x):
    return (x + 15) & ~15


def read_archive(path):
    data = Path(path).read_bytes()
    head = data[:64]
    if head[:4] != b'N2CY' or struct.unpack_from('<I', head, 4)[0] != 2:
        raise ValueError(f'{path}: not an N2CY v2 archive')
    w, h, nt = struct.unpack_from('<3I', head, 20)
    io, to = struct.unpack_from('<2I', head, 48)
    cells = [struct.unpack_from('<6I', data, io + 24 * c) for c in range(w * h)]
    tex = [list(struct.unpack_from('<3I', data, to + 12 * t)) for t in range(nt)]
    return data, cells, tex, to


def parts_in_order(data, cells, nt):
    """(offset, textures) of each near and far part, in the file's order."""
    out = []
    for e in cells:
        for k in (0, 1):
            off, size = e[2 * k], e[2 * k + 1]
            if not size:
                continue
            blob = data[off:off + size]
            found, _ = n2cy_bounds.batches(blob)
            out.append((off, [t for t in (struct.unpack_from('<I', blob, at)[0] for at, _, _, _ in found) if t < nt]))
    out.sort()
    return out


def new_order(data, cells, tex):
    """The textures (with a blob) by the first part that draws with them."""
    first = {}
    for i, (_, ts) in enumerate(parts_in_order(data, cells, len(tex))):
        for t in ts:
            if tex[t][2] and t not in first:
                first[t] = i
    return sorted((t for t in range(len(tex)) if tex[t][2]), key=lambda t: (first.get(t, 1 << 30), tex[t][1]))


def reads_per_part(data, cells, tex, offset_of):
    """Reads a part's textures take, neighbours JOIN_GAP apart joined."""
    parts = parts_in_order(data, cells, len(tex))
    total = 0
    for _, ts in parts:
        spans = sorted({(offset_of[t], tex[t][2]) for t in ts if tex[t][2]})
        cur = None
        for o, s in spans:
            if cur and o <= cur[1] + JOIN_GAP and o + s - cur[0] <= JOIN_SPAN:
                cur[1] = max(cur[1], o + s)
            else:
                total += 1
                cur = [o, o + s]
    return total / max(1, len(parts))


def in_order(tex, order):
    """Laid out as reorder would lay it: each next texture after the one before."""
    return all(tex[a][1] < tex[b][1] for a, b in zip(order, order[1:]))


def reorder(src, dst=None, if_unordered=False):
    src = Path(src)
    dst = Path(dst) if dst else src
    data, cells, tex, to = read_archive(src)
    order = new_order(data, cells, tex)
    if if_unordered and dst == src and in_order(tex, order):
        return 0   # (done before: a build does nothing)
    start = min(tex[t][1] for t in order)
    end = max(tex[t][1] + tex[t][2] for t in order)
    cell_end = max((e[2 * k] + e[2 * k + 1] for e in cells for k in range(3) if e[2 * k + 1]), default=0)
    if cell_end > start:
        raise ValueError(f'{src}: a cell blob after the textures; not the layout this expects')
    out = bytearray(data[:start])
    region = bytearray()
    for t in order:
        region.extend(bytes((-len(region)) % 16))
        off, size = start + len(region), tex[t][2]
        region.extend(data[tex[t][1]:tex[t][1] + size])
        tex[t][1] = off
    if start + len(region) > len(data):
        raise ValueError(f'{src}: the textures no longer fit ({len(region)} bytes from {start})')
    out.extend(region)
    out.extend(data[start + len(region):])   # (the last texture's padding, as it was)
    if len(out) != len(data):
        raise ValueError(f'{src}: the size changed ({len(data)} -> {len(out)})')
    for t, e in enumerate(tex):
        struct.pack_into('<3I', out, to + 12 * t, *e)
    times = os.stat(src)
    tmp = dst.with_name(dst.name + '.texorder-tmp')
    tmp.write_bytes(out)
    os.utime(tmp, ns=(times.st_atime_ns, times.st_mtime_ns))
    if dst.is_symlink():
        dst.unlink()
    tmp.replace(dst)
    if dst != src:   # the mipmaps go with it (by texture index, the archive's size: unchanged)
        mips = src.with_suffix('.n2mp')
        if mips.exists():
            to_mips = dst.with_suffix('.n2mp')
            if to_mips.is_symlink():
                to_mips.unlink()
            to_mips.write_bytes(mips.read_bytes())
            mt = os.stat(mips)
            os.utime(to_mips, ns=(mt.st_atime_ns, mt.st_mtime_ns))
    return len(order)


def check(path):
    data, cells, tex, _ = read_archive(path)
    order = new_order(data, cells, tex)
    now = {t: tex[t][1] for t in order}
    start, at, new = min(now.values()), 0, {}
    for t in order:
        at = _al(at)
        new[t] = start + at
        at += tex[t][2]
    print(f'{path}: {len(order)} textures; reads a part (joined {JOIN_GAP >> 10} KB apart): '
          f'as laid out {reads_per_part(data, cells, tex, now):.2f}, reordered {reads_per_part(data, cells, tex, new):.2f}')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--check':
        check(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == '--if-unordered':
        n = reorder(sys.argv[2], if_unordered=True)
        print(f'{sys.argv[2]}: ' + (f'{n} textures reordered' if n else 'in order already'))
    elif len(sys.argv) in (2, 3):
        n = reorder(*sys.argv[1:])
        print(f'{sys.argv[1]}: {n} textures reordered')
    else:
        sys.exit(__doc__)
