#!/usr/bin/env python3
"""The PS2 game's button icons for the PSP's prompts: the DualShock's
cross, circle, square and triangle (the PSP's own), its shoulder buttons,
START and SELECT, from the PS2 disc's GLOBAL\\GLOBALB.LZC (the second TPK:
PS2_CROSS, PS2_CIRCLE, ...), 32 x 32, 4 bits, a 16-colour palette, the
pixels in the GS's PSMT4 order, the alpha 0x80 = 1.0.

The Xbox data the port is built from draws its own pad's buttons; the menus'
"$JOY_EVENT_FENG_...$" tokens become these icons (tools/convert_hud.py), the
PSP's L and R made from the PS2's L1 and R1 without their "1".

    ps2_buttons.py [out_dir]   # PNGs of them, to look at
"""
import os
from pathlib import Path
import struct
import subprocess
import sys

from n2formats import Texture, chunks, archived, decompress

ROOT = Path(__file__).resolve().parents[1]
DISC = 'Need for Speed - Underground 2 (USA) PS2/disc/NFSUNDER'


def main_checkout():
    """The main checkout's folder (a worktree's ROOT lacks the discs, kept
    out of git), or None."""
    try:
        d = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                           capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    return Path(d).parent if d else None


def ps2_game():
    """The PS2 disc's folder: NFSU2_PS2_GAME, else this checkout's, else the
    main checkout's (a worktree's own lacks it)."""
    if os.environ.get('NFSU2_PS2_GAME'):
        return Path(os.environ['NFSU2_PS2_GAME'])
    main = main_checkout()
    for root in (ROOT, main):
        if root and (root / DISC).is_dir():
            return root / DISC
    return ROOT / DISC


PS2_GAME = ps2_game()
NAMES = ('PS2_CROSS', 'PS2_CIRCLE', 'PS2_SQUARE', 'PS2_TRIANGLE', 'PS2_L1', 'PS2_R1', 'PS2_START', 'PS2_SELECT',
         'PS2_DPADUPDOWN', 'PS2_DPADL', 'PS2_DPADR', 'PS2_DPADU', 'PS2_DPADD', 'PS2_LEFTANALOG')


def unswizzle4(buf, w, h):
    """A PSMT4 texture as the GS keeps it -> one palette index a pixel, row by row."""
    pixels = []
    for p in buf:
        pixels += [p & 15, p >> 4]
    interlace, matrix, tile = (0x00, 0x10, 0x02, 0x12, 0x11, 0x01, 0x13, 0x03), (0, 1, -1, 0), (4, -4)
    out = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            odd = y & 1
            n4 = (x // 4) % 4 + (4 if odd else 0)
            i = interlace[n4] + (x * 4) % 16 + (x // 16) * 32 + ((y - 1) * w if odd else y * w)
            j = (y + matrix[y % 4]) * w + x + ((y // 4) & 1) * tile[(x // 4) & 1]
            if 0 <= i < w * h and 0 <= j < w * h:
                out[j] = pixels[i]
    return out


def require(game=PS2_GAME):
    """load(), or the conversion stops: without the PS2 disc the prompts came
    out as text ("L X [] R /\\ START SELECT" after the race, in the replay and
    the menus' footers, 2026-10-14: the HUDs converted in a worktree without
    the disc's folder, each .n2ui ~28 KB smaller, and nothing said so).
    Without any PS2 disc (a player with the Xbox one alone) the port's own
    drawings stand in (own_icons). NFSU2_ALLOW_TEXT_BUTTONS=1 accepts the text."""
    icons = load(game)
    if not icons:
        print(f'ps2_buttons: no PS2 disc at {game}: using the port button icons', file=sys.stderr)
        return own_icons()
    missing = [n for n in NAMES if n not in icons]
    if missing and os.environ.get('NFSU2_ALLOW_TEXT_BUTTONS') != '1':
        sys.exit(f'ps2_buttons: PS2 button icons could not be read from {game} '
                 f'({len(missing)} of {len(NAMES)} missing): prompts would use text. '
                 'Place the US PS2 disc folder in the checkout root, or set NFSU2_PS2_GAME=<.../disc/NFSUNDER>; '
                 'NFSU2_ALLOW_TEXT_BUTTONS=1 aceita o texto.')
    return icons


def load(game=PS2_GAME):
    """{name: Texture (RGBA)} of the button icons, {} without the PS2 disc."""
    try:
        b = archived(game, 'GLOBAL/GLOBALB.LZC')
    except (OSError, ValueError):
        return {}
    if b[:4] == b'JDLZ':
        b = decompress(b, struct.unpack_from('<I', b, 8)[0])
    out = {}
    for t, p, n in chunks(b):
        if t != 0xb3300000:
            continue
        sub = {a: (c, d) for a, c, d in chunks(b, p, p + n)}
        if 0x33310004 not in sub or 0x33320002 not in sub:
            continue
        rc, rd = sub[0x33310004]
        dc, dd = sub[0x33320002]
        data = b[dc:dc + dd]
        lead = len(data) - len(data.lstrip(b'\x11'))   # the pixel block after its 0x11 filler
        for k in range(rd // 124):
            r = b[rc + 124 * k:rc + 124 * (k + 1)]
            name = r[12:36].split(b'\0')[0].decode('latin-1')
            if name not in NAMES:
                continue
            info = r[0x24:]
            img, pal, size, pal_size = struct.unpack_from('<4I', info, 12)
            w, h = struct.unpack_from('<2H', info, 32)
            if size * 2 != w * h or pal_size != 64:
                continue
            p_ = data[lead + pal:lead + pal + 64]
            cols = [bytes((p_[4 * i], p_[4 * i + 1], p_[4 * i + 2], min(255, 2 * p_[4 * i + 3]))) for i in range(16)]
            idx = unswizzle4(data[lead + img:lead + img + size], w, h)
            out[name] = Texture(name, 0, w, h, 0, b''.join(cols[i] for i in idx))
    for one, letter in (('PS2_L1', 'PSP_L'), ('PS2_R1', 'PSP_R')):
        if one in out:
            out[letter] = shoulder(out[one], letter)
    return out


COLOUR = (202, 243, 138)   # the pad icons' green, as the PS2's


def own_icons():
    """The port's own pad icons, drawn here (no art from any disc): the same
    names, size and single green as load()'s, for players without the PS2
    disc. Shapes in a 32-unit square, drawn 8x larger and box-filtered."""
    from PIL import Image, ImageChops, ImageDraw
    k = 8
    n = 32 * k

    def sc(*v):
        return [round(c * k) for c in v]

    def icon(name, draw_full, draw_dim=None):
        full, dim = Image.new('L', (n, n)), Image.new('L', (n, n))
        draw_full(ImageDraw.Draw(full))
        if draw_dim:
            draw_dim(ImageDraw.Draw(dim))
        big = ImageChops.lighter(full, Image.eval(dim, lambda v: v * 45 // 100)).tobytes()
        # an integer box filter: identical bytes on every CPU (a float one may not be)
        alpha = [sum(big[(y * k + j) * n + x * k + i] for j in range(k) for i in range(k)) // (k * k)
                 for y in range(32) for x in range(32)]
        px = b''.join(bytes((*COLOUR, v)) for v in alpha)
        return Texture(name, 0, 32, 32, 0, px)

    W = 2.4   # stroke width, in icon units

    def ring(d):
        d.ellipse(sc(2.2, 2.2, 29.8, 29.8), outline=255, width=round(W * k))

    def outline_box(d, x0, y0, x1, y1, r, w):
        """A rounded box's outline: the filled box less its inside (PIL's own
        thick rounded outline leaves a stray inner edge)."""
        d.rounded_rectangle(sc(x0, y0, x1, y1), radius=round(r * k), fill=255)
        d.rounded_rectangle(sc(x0 + w, y0 + w, x1 - w, y1 - w), radius=round(max(0, r - w) * k), fill=0)

    def frame(d):
        outline_box(d, 2.2, 8.2, 29.8, 23.8, 2, 2.1)

    def line(d, *pts, w=W):
        d.line(sc(*pts), fill=255, width=round(w * k), joint='curve')
        for x, y in zip(pts[::2], pts[1::2]):   # round caps
            r = w * k / 2
            d.ellipse((x * k - r, y * k - r, x * k + r, y * k + r), fill=255)

    def letter(d, ch, x, top=12.7, h=6.6):
        """L, R or 1 as strokes, h units tall from top, its left edge at x."""
        b, w = top + h, 1.9
        if ch == 'L':
            line(d, x, top, x, b, x + 4, b, w=w)
        elif ch == 'R':
            mid = top + h * .52
            line(d, x, b, x, top, x + 2.4, top, w=w)
            d.arc(sc(x + 0.4, top, x + 4.6, mid), 270, 90, fill=255, width=round(w * k))
            line(d, x + 2.4, mid, x, mid, w=w)
            line(d, x + 2, mid, x + 4.3, b, w=w)
        else:
            line(d, x + 1.6, top, x + 1.6, b, w=w)
            line(d, x + 1.6, top, x, top + 1.5, w=w)

    def arm(direction):
        """One arm of the cross pad pointing to direction, its point inwards."""
        base = [(11, 1.5), (21, 1.5), (21, 10.5), (16, 14.2), (11, 10.5)]
        rot = {'U': lambda x, y: (x, y), 'D': lambda x, y: (x, 32 - y),
               'L': lambda x, y: (y, x), 'R': lambda x, y: (32 - y, x)}[direction]
        return [tuple(c * k for c in rot(x, y)) for x, y in base]

    def pad(lit):
        def full(d):
            for a in lit:
                d.polygon(arm(a), fill=255)

        def dim(d):
            for a in 'UDLR':
                if a not in lit:
                    d.polygon(arm(a), outline=255, width=round(1.7 * k))
        return full, dim

    out = {
        'PS2_CROSS': icon('PS2_CROSS', lambda d: (ring(d), line(d, 11.2, 11.2, 20.8, 20.8), line(d, 20.8, 11.2, 11.2, 20.8))),
        'PS2_CIRCLE': icon('PS2_CIRCLE', lambda d: (ring(d), d.ellipse(sc(10, 10, 22, 22), outline=255, width=round(W * k)))),
        'PS2_SQUARE': icon('PS2_SQUARE', lambda d: (ring(d), d.rectangle(sc(10.6, 10.6, 21.4, 21.4), outline=255, width=round(W * k)))),
        'PS2_TRIANGLE': icon('PS2_TRIANGLE', lambda d: (ring(d), d.polygon(sc(16, 9.6, 22.6, 20.8, 9.4, 20.8), outline=255, width=round(W * k)))),
        'PS2_START': icon('PS2_START', lambda d: d.polygon(sc(7.5, 7.5, 26.5, 16, 7.5, 24.5), outline=255, width=round(2.2 * k))),
        'PS2_SELECT': icon('PS2_SELECT', lambda d: outline_box(d, 4.5, 11.5, 27.5, 20.5, 1.5, 2.2)),
        'PS2_L1': icon('PS2_L1', lambda d: (frame(d), letter(d, 'L', 10.6), letter(d, '1', 17.6))),
        'PS2_R1': icon('PS2_R1', lambda d: (frame(d), letter(d, 'R', 10.2), letter(d, '1', 17.8))),
        'PSP_L': icon('PSP_L', lambda d: (frame(d), letter(d, 'L', 14))),
        'PSP_R': icon('PSP_R', lambda d: (frame(d), letter(d, 'R', 13.7))),
        'PS2_LEFTANALOG': icon('PS2_LEFTANALOG', lambda d: (ring(d), letter(d, 'L', 13.6, 11, 9.5))),
    }
    for name, lit in (('PS2_DPADU', 'U'), ('PS2_DPADD', 'D'), ('PS2_DPADL', 'L'), ('PS2_DPADR', 'R'), ('PS2_DPADUPDOWN', 'UD')):
        out[name] = icon(name, *pad(lit))
    return out


def shoulder(tex, name):
    """The PSP's L or R: the PS2's L1 or R1, its "1" taken out and the letter
    centred in the frame."""
    w, h = tex.w, tex.h
    px = bytearray(tex.rgba())
    lit = [[px[4 * (y * w + x) + 3] > 64 for x in range(w)] for y in range(h)]
    rows = [y for y in range(h) if any(lit[y])]
    top, bottom = rows[0], rows[-1]
    inner = [y for y in range(top + 2, bottom - 1) if any(lit[y][x] for x in range(3, w - 3))]
    # the glyphs inside the frame: columns lit between its top and bottom edges
    cols = [x for x in range(w) if any(lit[y][x] for y in range(top + 3, bottom - 2))]
    left, right = cols[0], cols[-1]
    inside = [x for x in range(left + 3, right - 2) if any(lit[y][x] for y in range(top + 3, bottom - 2))]
    gaps = [x for x in range(inside[0], inside[-1]) if x not in inside]
    split = gaps[0] if gaps else (inside[0] + inside[-1]) // 2
    letter = [x for x in inside if x < split]
    glyph = {(x, y): px[4 * (y * w + x):4 * (y * w + x) + 4] for x in letter for y in range(top + 3, bottom - 2)}
    for x in inside:
        for y in range(top + 3, bottom - 2):
            px[4 * (y * w + x) + 3] = 0
    shift = (left + right) // 2 - (letter[0] + letter[-1]) // 2
    for (x, y), v in glyph.items():
        if v[3] > 0:
            px[4 * (y * w + x + shift):4 * (y * w + x + shift) + 4] = v
    del inner
    return Texture(name, 0, w, h, 0, bytes(px))


if __name__ == '__main__':
    from PIL import Image
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('.')
    for name, t in load().items():
        Image.frombytes('RGBA', (t.w, t.h), bytes(t.rgba())).save(out / f'{name}.png')
        print(name, t.w, t.h)
