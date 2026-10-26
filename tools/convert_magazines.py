#!/usr/bin/env python3
"""The magazines' covers (the photo shoots' reward): magazines.n2mg.

FRONTEND/MAGAZINES_STREAMING_HIGH.BIN is a streamed TPK ("MAGAZINES"):
chunk 0x33310003 lists its 124 textures (24 bytes: u32 key, u32 offset in
the file, u32 bytes, u32 size unpacked, ...), each a JDLZ block that
unpacks to its pixels followed by the texture's 0xB8-byte record (its name
at +0x0C, the key at +0x24, as a flat TPK's). The textures are the covers'
layers, 512x512 with the cover in the top left (335 x 438): per magazine
<MAG>_BG_nn (the background), <MAG>_MASTHEAD_nn (the title and the
headlines), the girl (<MAG>_ALEX_01, _ANNA_01, ...) and <MAG>_XT_nn (more
text); the player's car goes between the background and the rest.
16 magazines: Car Audio & Electronics (CAE), Racing Street Unleashed (RSU)...
MAGAZINES_STREAMING_LOW.BIN, alike, has 14 magazines' two mastheads alone
(AM, H4, SCNORWAY, TUR...; CAF_MASHEAD_01, the game's spelling): the photo
shoots' covers, the photo taken under its masthead.

Which magazine each cover is (tools/convert_career.py show_cameras and
studio_covers: MAGAZINES_SHOWCASE.BIN and MAGAZINES_FRONTEND.BIN by the
cover's +0x28) goes with the photo shoot (career.n2cr); the game draws one
of the setup's choices of each layer at random (0x11ced0), the port picks
one of each by the shoot (psp-viewer/main.c cover_open).

magazines.n2mg: 'N2MG', u32 version 1, u32 magazines; magazines x (char
name[8], u8 layers, u8 pad[3], then layers x (u8 kind (0 background, 1
masthead, 2 girl, 3 text), u8 pad[3], u32 offset of its image in the
file)); images: 256x256 (the cover scaled into its top left, 168 x 220),
8-bit indices (PSP order, row by row) then 256 x RGBA palette.
"""
import argparse
from pathlib import Path
import struct

import n2formats
from n2formats import archived, decode_texture, decompress
import n2source

ROOT = Path(__file__).resolve().parents[1]
COVER = (0, 0, 336, 440)     # the cover in a 512x512 layer
SIZE = (168, 220)            # on the PSP, in a 256x256 texture
KINDS = ('BG', 'MASTHEAD', 'GIRL', 'XT')


FILES = ('FRONTEND/MAGAZINES_STREAMING_HIGH.BIN',   # the studio covers' magazines: their layers
         'FRONTEND/MAGAZINES_STREAMING_LOW.BIN')    # the photo shoots' 14: two mastheads each


def blocks(game):
    """(key, name, the unpacked block, its record's key offset) of each
    texture of the streamed TPKs. The record sits at the block's end less
    the table's header size, + 0x6c (the PS2's + 0x54, a slot TPK's)."""
    for f in FILES:
        s = archived(game, f)
        at = s.index(struct.pack('<I', 0x33310003)) + 8
        n = struct.unpack_from('<I', s, at - 4)[0] // 24
        for i in range(n):
            key, off, size, unpacked, hfe = struct.unpack_from('<5I', s, at + 24 * i)
            d = decompress(s[off:off + size], unpacked)
            p = unpacked - hfe + (0x54 if n2formats.PLATFORM == 'ps2' else 0x6c)
            if struct.unpack_from('<I', d, p)[0] != key:
                raise ValueError(f'{f}: texture {key:08x} without its record')
            yield key, d[p - 0x18:p].split(b'\0')[0].decode(), d, p


def names(game):
    """{hash: name} of the streamed TPK's textures (BB_MASTHEAD_01...), by
    their key and by their name's bin hash."""
    from n2feng import bin_hash
    out = {}
    for key, name, *_ in blocks(game):
        out[key] = out[bin_hash(name)] = name
    return out


def textures(game):
    """{name: RGBA Image} of the streamed TPK."""
    from PIL import Image   # (here: tools/convert_career.py reads the names without it)
    out = {}
    for key, name, d, p in blocks(game):
        if name in out:
            continue   # (BB's mastheads in both files: the first's)
        info = d[p:]
        img_off, pal_off = struct.unpack_from('<2I', info, 12)
        pal = d[pal_off - img_off:pal_off - img_off + 1024] if struct.unpack_from('<I', info, 24)[0] else b''   # a P8's palette after its pixels
        try:
            t = decode_texture(name, key, info, d[:p], pal)
        except ValueError:
            continue
        im = Image.frombytes('RGBA', (t.w, t.h), bytes(t.pixels)) if not t.palette else None
        if im is None:   # a P8: indices and its palette
            p = t.palette
            im = Image.frombytes('P', (t.w, t.h), bytes(t.pixels))
            im.putpalette(bytes(c for i in range(256) for c in p[4 * i:4 * i + 3]))
            im = im.convert('RGBA')
            alpha = bytes(p[4 * v + 3] for v in t.pixels)
            im.putalpha(Image.frombytes('L', (t.w, t.h), alpha))
        out[name] = im
    return out


def layers(tex):
    """{magazine: [(kind, name)]}: every background, masthead, girl and text
    of each magazine, by kind (a photo shoot's: its two mastheads); the
    game draws one of each (0x11ced0), the port picks by the shoot."""
    mags = {}
    for name in sorted(tex):
        mag = 'GTI_FIN' if name.startswith('GTI_FIN_') else name.split('_')[0]
        rest = name[len(mag) + 1:]
        kind = 0 if rest.startswith('BG_') else 1 if rest.startswith(('MASTHEAD_', 'MASHEAD_')) else 3 if rest.startswith('XT_') else 2   # (CAF_MASHEAD_01: the game's spelling)
        mags.setdefault(mag, []).append((kind, name))
    return {m: sorted(v) for m, v in mags.items() if any(k == 1 for k, _ in v)}


def image(im):
    """The cover's part of a layer as 256x256 8-bit indices and an RGBA palette."""
    from PIL import Image
    cover = im.crop(COVER).resize(SIZE, Image.LANCZOS)
    canvas = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    canvas.paste(cover, (0, 0))
    q = canvas.quantize(256, method=Image.Quantize.FASTOCTREE)
    pal = q.getpalette('RGBA') if hasattr(q, 'getpalette') else None
    pal = (pal or [])[:1024] + [0] * (1024 - len(pal or []))
    return q.tobytes() + bytes(pal)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    a = ap.parse_args()
    tex = textures(a.game)
    mags = layers(tex)
    head = struct.pack('<4sII', b'N2MG', 1, len(mags))
    size_table = sum(12 + 8 * len(v) for v in mags.values())
    at, entries, blobs = len(head) + size_table, b'', b''
    for mag, ls in mags.items():
        entries += mag.encode()[:7].ljust(8, b'\0') + struct.pack('<B3x', len(ls))
        for kind, name in ls:
            blob = image(tex[name])
            entries += struct.pack('<B3xI', kind, at + len(blobs))
            blobs += blob
    (a.out / 'magazines.n2mg').write_bytes(head + entries + blobs)
    print(f'magazines.n2mg: {len(mags)} magazines, {len(tex)} textures read, {len(blobs) // 1024} KB:',
          ', '.join(f"{m} ({'/'.join(KINDS[k] for k, _ in v)})" for m, v in mags.items()))


if __name__ == '__main__':
    main()
