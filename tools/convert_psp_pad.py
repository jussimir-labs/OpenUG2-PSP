#!/usr/bin/env python3
"""The PSP on the controls screens (Options > Controls, a race's load): the
project's own picture of a PSP-3000 (art/reference/psp-controls/
psp-isolated-v1.png, made for the port, transparent around it) where the
PS2 shows its DualShock, cut to the console, shrunk to the size the screens
draw it at, in a 256-colour palette (alpha in it).

psppad.n2tx: 'N2PP', u16 width, u16 height (the console's pixels), u16
texture width, u16 texture height (powers of two, the console at the top
left), u16 anchors, then per anchor u16 x, u16 y (the console's pixels, in
ANCHORS' order: where each button's leader line ends), the palette (256 x
RGBA8888) and the pixels (texture width x height, an index each, linear).

    convert_psp_pad.py [--out DIR] [--preview PNG]
"""
import argparse
from pathlib import Path
import struct

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'art/reference/psp-controls/psp-isolated-v1.png'
WIDTH = 208            # the console's width on the PSP's screen (pixels, drawn 1:1)
TEX = (256, 128)
# the buttons in the source picture (its pixels): the leader lines' ends
ANCHORS = (('L', 230, 105), ('R', 1545, 105), ('UP', 205, 298), ('DOWN', 205, 460), ('LEFT', 123, 380),
           ('RIGHT', 285, 380), ('ANALOG', 212, 598), ('TRIANGLE', 1577, 292), ('CIRCLE', 1665, 383),
           ('CROSS', 1577, 475), ('SQUARE', 1487, 383), ('SELECT', 1275, 727), ('START', 1391, 727))


def convert(source=SOURCE):
    """(picture, anchors): the console cut out and shrunk, its anchors there."""
    im = Image.open(source).convert('RGBA')
    box = im.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()   # the body, not its faint rim
    im = im.crop(box)
    k = WIDTH / im.width
    h = round(im.height * k)
    small = im.resize((WIDTH, h), Image.LANCZOS)
    anchors = [(n, round((x - box[0]) * k), round((y - box[1]) * k)) for n, x, y in ANCHORS]
    return small, anchors


def palette(small, edge_colours=12, edge_alphas=(64, 128, 192)):
    """(indices, RGBA palette): the console's solid pixels in their own
    colours (median cut, no dither: its dark gradients smooth), the
    antialiased rim in a few colours by a few alphas, one clear entry."""
    b = small.tobytes()
    px = [tuple(b[i:i + 4]) for i in range(0, len(b), 4)]
    solid = [i for i, p in enumerate(px) if p[3] >= 248]
    rim = [i for i, p in enumerate(px) if 8 <= p[3] < 248]
    n_rim = edge_colours * len(edge_alphas)

    def quant(idx, n):
        strip = Image.new('RGB', (max(1, len(idx)), 1))
        strip.putdata([px[i][:3] for i in idx] or [(0, 0, 0)])
        q = strip.quantize(n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = q.getpalette()[:3 * n]
        return list(q.tobytes()), [tuple(pal[3 * k:3 * k + 3]) for k in range(len(pal) // 3)]
    s_idx, s_pal = quant(solid, 255 - n_rim)
    r_idx, r_pal = quant(rim, edge_colours)
    pal = [(0, 0, 0, 0)] + [c + (255,) for c in s_pal]
    rim_base = len(pal)
    for c in r_pal:
        pal += [c + (a,) for a in edge_alphas]
    out = [0] * len(px)
    for i, k in zip(solid, s_idx):
        out[i] = 1 + k
    for i, k in zip(rim, r_idx):
        a = px[i][3]
        j = min(range(len(edge_alphas)), key=lambda j: abs(edge_alphas[j] - a))
        out[i] = rim_base + k * len(edge_alphas) + j
    pal += [(0, 0, 0, 0)] * (256 - len(pal))
    return out, pal[:256]


def write(small, anchors, path):
    idx, pal = palette(small)
    tex = bytearray(TEX[0] * TEX[1])   # index 0, clear, around the console
    for y in range(small.height):
        tex[y * TEX[0]:y * TEX[0] + small.width] = bytes(idx[y * small.width:(y + 1) * small.width])
    out = b'N2PP' + struct.pack('<5H', small.width, small.height, TEX[0], TEX[1], len(anchors))
    out += b''.join(struct.pack('<2H', x, y) for _, x, y in anchors)
    out += b''.join(bytes(c) for c in pal) + bytes(tex)
    path.write_bytes(out)
    return len(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--preview', type=Path, help='a PNG of the result over grey, its anchors marked')
    args = ap.parse_args()
    small, anchors = convert()
    n = write(small, anchors, args.out / 'psppad.n2tx')
    print('psppad.n2tx', f'{small.width}x{small.height}', n, 'bytes')
    if args.preview:
        from PIL import ImageDraw
        k = 4
        idx, pal = palette(small)
        shown = Image.new('RGBA', small.size)
        shown.putdata([pal[i] for i in idx])
        bg = Image.new('RGBA', small.size, (40, 40, 40, 255))
        bg.alpha_composite(shown)
        bg = bg.resize((small.width * k, small.height * k), Image.NEAREST)
        d = ImageDraw.Draw(bg)
        for _, x, y in anchors:
            d.ellipse((x * k - 5, y * k - 5, x * k + 5, y * k + 5), outline=(150, 220, 80, 255), width=2)
        bg.convert('RGB').save(args.preview)


if __name__ == '__main__':
    main()
