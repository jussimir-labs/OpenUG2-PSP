#!/usr/bin/env python3
"""Mipmaps for the city's textures, beside the archive: city.n2cy -> city.n2mp.

The archive's textures (T8: a 256-colour 8888 palette, swizzled) have no
mip levels, and on a PSP the GE reading a far wall's texture across it for
a few pixels misses its 8 KB texture cache (~4x the cost a pixel,
tools/psp-bench). psp-viewer/world.c reads these levels with each texture
when the file matches the archive; without it the city draws as before.

Each level halves the last, down to 16 x 8 (the smallest swizzled T8
block). A 2 x 2 block keeps its cover for the alpha test (alpha > 96, the
street's): two or more of its texels over it, the level's texel is too,
the palette colour (alpha over 96) nearest the average of those texels;
fewer, the palette colour (alpha 96 or under) nearest the average of the
others. No colour the palette does not have; foliage keeps its density.

File (little endian): "N2MP", version 1, the archive's size, its texture
count; per texture (its index in the archive) key, offset, size, levels
(u32 each; levels 0: none); then the levels, each swizzled and 16-aligned.

convert_city.py writes it after each archive; by hand, with the venv's
Python (numpy): .tools/venv/bin/python tools/convert_mips.py [archives...]
--if-stale: only the archives whose .n2mp is missing, older, or made for
another archive (its size or texture count): tools/build.sh. The bounds
tables (n2cy_bounds.py) grow the archive and keep its date; a city and five
tracks went to the PSP with every .n2mp refused that way (2026-10-14).
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
ALPHA_REF = 96      # sceGuAlphaFunc(GU_GREATER, 96, ...) on the street
MIP_MAX = 7


def levels_for(w, h):
    n = 0
    while n < MIP_MAX and (w >> (n + 1)) >= 16 and (h >> (n + 1)) >= 8:
        n += 1
    return n


def swizzle_map(w, h):
    """Linear (y, x) -> swizzled byte offset of a T8 texture (16 x 8 blocks)."""
    y, x = np.mgrid[0:h, 0:w]
    return ((y >> 3) * (w >> 4) + (x >> 4)) * 128 + ((y & 7) << 4) + (x & 15)


def unswizzle(data, w, h):
    return np.frombuffer(data, np.uint8)[swizzle_map(w, h)]


def swizzle(img):
    h, w = img.shape
    out = np.empty(w * h, np.uint8)
    out[swizzle_map(w, h)] = img
    return out.tobytes()


def halve(img, pal):
    """One level down from img (palette indices)."""
    h, w = img.shape
    blk = np.stack([img[0::2, 0::2], img[0::2, 1::2], img[1::2, 0::2], img[1::2, 1::2]], -1)   # (h/2, w/2, 4)
    col = pal[blk].astype(np.float32)                         # (h/2, w/2, 4 texels, RGBA)
    opaque = col[..., 3] > ALPHA_REF
    cover = opaque.sum(-1) >= 2
    sel = np.where(cover[..., None], opaque, ~opaque)          # the texels the result follows
    n = sel.sum(-1, keepdims=True)
    target = (col * sel[..., None]).sum(-2) / np.maximum(n, 1)
    # nearest palette colour on the same side of the alpha test
    side = pal[:, 3] > ALPHA_REF
    d = ((target[..., None, :] - pal[None, None].astype(np.float32)) ** 2).sum(-1)   # (h/2, w/2, 256)
    d = np.where(cover[..., None] == side[None, None], d, np.inf)
    best = d.argmin(-1)
    # a side the palette lacks (all of its colours opaque, or none): the
    # block's own texel nearest the target
    lacking = ~np.isfinite(d.min(-1))
    if lacking.any():
        dk = ((col - target[..., None, :]) ** 2).sum(-1)
        own = np.take_along_axis(blk, dk.argmin(-1)[..., None], -1)[..., 0]
        best = np.where(lacking, own, best)
    return best.astype(np.uint8)


def stale(src):
    """src's .n2mp missing, older than it, or not world.c's match for it."""
    src = Path(src)
    dst = src.with_suffix('.n2mp')
    if not dst.exists() or src.stat().st_mtime > dst.stat().st_mtime:
        return True
    with open(src, 'rb') as f:
        head = f.read(32)
    with open(dst, 'rb') as f:
        mh = f.read(16)
    return len(mh) < 16 or mh[:4] != b'N2MP' or struct.unpack_from('<3I', mh, 4) != (1, src.stat().st_size, struct.unpack_from('<I', head, 28)[0])


def write_mips(src):
    """src (an .n2cy) -> its .n2mp beside it; returns the line to print."""
    src = Path(src)
    data = src.read_bytes()
    if data[:4] != b'N2CY':
        raise ValueError(f'{src}: is not an N2CY file')
    nt, = struct.unpack_from('<I', data, 28)
    to, = struct.unpack_from('<I', data, 52)
    table, blobs, at = [], [], 16 + nt * 16
    made = 0
    for t in range(nt):
        key, off, size = struct.unpack_from('<III', data, to + t * 12)
        w, h = struct.unpack_from('<II', data, off)
        n = levels_for(w, h) if size == 16 + 1024 + w * h else 0
        if not n:
            table.append((key, 0, 0, 0))
            continue
        pal = np.frombuffer(data, np.uint8, 1024, off + 16).reshape(256, 4)   # R, G, B, A (ABGR words)
        img = unswizzle(data[off + 1040:off + 1040 + w * h], w, h)
        out = b''
        for _ in range(n):
            img = halve(img, pal)
            lvl = swizzle(img)
            out += lvl + b'\0' * (-len(lvl) % 16)
        table.append((key, at, len(out), n))
        blobs.append(out)
        at += len(out)
        made += 1
    dst = src.with_suffix('.n2mp')
    tmp = dst.with_name(dst.name + '.tmp')   # a new file: a hard link elsewhere keeps its own
    with open(tmp, 'wb') as f:
        f.write(b'N2MP' + struct.pack('<III', 1, len(data), nt))
        for e in table:
            f.write(struct.pack('<IIII', *e))
        for b in blobs:
            f.write(b)
    tmp.replace(dst)
    return f'{dst}: {made} de {nt} texturas com mipmaps, {at / 2**20:.1f} MB'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('archive', nargs='*', default=[str(ROOT / 'dist/PSP/GAME/NFSU2VIEW/city.n2cy')])
    ap.add_argument('--if-stale', action='store_true', help='only archives whose .n2mp does not match them')
    a = ap.parse_args()
    for src in a.archive:
        if a.if_stale and not stale(src):
            continue
        try:
            print(write_mips(src))
        except ValueError as e:
            sys.exit(str(e))


if __name__ == '__main__':
    main()
