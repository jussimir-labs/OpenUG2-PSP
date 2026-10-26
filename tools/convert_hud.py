#!/usr/bin/env python3
"""NFSU2's in-race HUD -> hud.n2ui for the PSP viewer.

Source: the FEng package HUD_SingleRace.fng (plain chunk 0x30203 of
Global\\InGameRace.bun, tools/n2feng.py) with its textures and fonts:
  - images: the package's TPK (InGameRace), InGameCommon and GLOBALB
    (texture key = bin hash of the resource name without its extension);
    the tachometer pieces are code-supplied in
    the original (the HUD style picked in the garage), taken here from
    HUD_CustomTextures_00 (style 00, the default);
  - fonts: FNTX chunks (0x30201) of LANGUAGES\\ENGLISH.BIN with their glyph
    pages in LANGUAGES\\LANGUAGETEXTURES.BIN.
FNTX (offsets in the chunk after its 0x200-byte name block): 'FNTX', ?,
u16 ?, u16 glyph count, ..., u32 glyph table offset (0x80), u32 kerning
offset, u32 size, ..., u32 line height @0x24; glyphs 16 bytes: u16 code,
u8 w, u8 h, u16 x, u16 y (texture), u8 ?, s8 x offset, s8 y offset, u8 ?,
u16 0, u16 advance.

The HUD is laid out on FEng's 640x480 screen (origin at the centre, y
down); the PSP draws it at 272/480 scale with the screen-edge groups
anchored to the PSP's wider edges (tools: --preview renders a PNG).
"""
import argparse
import re
import math
from pathlib import Path
import struct

from convert_car import box_downscale, psp_swizzle, quantize, write_png
import n2feng as fe
import convert_physics
from n2formats import FlatTPK, SlotTPK, archived, chunks, decompress
import ps2_buttons
import n2source

ROOT = Path(__file__).resolve().parents[1]
FONT_CHUNKS = {'CONDUITMDITC_TT14I.FFN': 'conduitmditc_tt14i', 'FONT_CONDUITMDITCTT38BI.FFN': 'font_conduitmditctt38bi',
               'CONDUITMDITC_TT21I.FFN': 'conduitmditc_tt21i'}
# code-supplied tachometer pieces -> the default HUD style's textures
CUSTOM = {'TACH_FILL': 'TACH_FILL_CUSTOM_00', '3RDPERSON_RPMDIAL': '3RDPERSON_RPMDIAL_CUSTO',
          '3RDPERSON_10500LINES': '3RDPERSON_10500LINES_CU', '3RDPERSON_TURBOFILL': '3RDPERSON_TURBOFILL_CUS',
          '3RDPERSON_TURBOLINES': '3RDPERSON_TURBOLINES_CU', '3RDPERSON_TURBODIAL': '3RDPERSON_TURBODIAL_CUS'}


def load(game, name):
    d = archived(game, name)
    if d[:4] == b'JDLZ':
        d = decompress(d, struct.unpack_from('<I', d, 8)[0])
    return d


class Sources:
    def __init__(self, game):
        self.flat = [FlatTPK(load(game, n)) for n in
                     ('GLOBAL/INGAMERACE.BUN', 'GLOBAL/INGAMECOMMON.LZC', 'GLOBAL/GLOBALB.LZC', 'GLOBAL/GLOBALA.BUN',
                      'FRONTEND/FRONTB.LZC', 'GLOBAL/INGAMEDRAG.BUN', 'GLOBAL/INGAMEDRIFT.BUN')]   # the drag's and drift's HUDs' own last
        self.custom = SlotTPK(load(game, 'GLOBAL/HUD_CUSTOMTEXTURES_00.BIN'))
        self.custom_names = {self.custom.name(k): k for k in self.custom.slots}
        self.lang = SlotTPK(load(game, 'LANGUAGES/LANGUAGETEXTURES.BIN'))
        self.lang_names = {self.lang.name(k): k for k in self.lang.slots}
        self.buttons = ps2_buttons.require()   # the PS2's pad icons for the prompts (stops without its disc)

    def image(self, rid, name):
        stem = name.rsplit('.', 1)[0].upper()
        if stem in self.buttons:
            return self.buttons[stem]
        for t in self.flat:
            tex = t.get(fe.bin_hash(stem))
            if tex is not None:
                return tex
        if stem in CUSTOM:
            return self.custom.get(self.custom_names[CUSTOM[stem]])
        return None

    def font_page(self, name):
        key = self.lang_names.get(name.upper()[:23])
        if key is not None:
            return self.lang.get(key)
        for t in self.flat:          # the menus' Conduit 21 lives in GLOBALA
            tex = t.by_name(name.upper()[:23])
            if tex is not None:
                return tex
        return None


class Font:
    def __init__(self, raw, page):
        b = raw[0x200:]
        if b[:4] not in (b'FNTX', b'FNTS'):   # the PS2's says FNTS, the rest the same
            raise ValueError('not an FNTX font')
        count = struct.unpack_from('<H', b, 10)[0]
        table = struct.unpack_from('<I', b, 0x14)[0]
        self.line = struct.unpack_from('<I', b, 0x24)[0]
        self.page, self.glyphs = page, {}
        for k in range(count):
            code, w, h, x, y, _, xo, yo, _, _, adv = struct.unpack_from('<H2B2HBbbBHH', b, table + 16 * k)
            self.glyphs[code] = (w, h, x, y, xo, yo, adv)

    def width(self, text):
        return sum(self.glyphs.get(ord(c), self.glyphs.get(32, (0,) * 7))[6] for c in text)


def fonts(game, src):
    out = {}
    for bundle in ('LANGUAGES/ENGLISH.BIN', 'GLOBAL/GLOBALA.BUN'):
        d = load(game, bundle)
        for t, p, n in chunks(d):
            if t == 0x30201:
                name = d[p:p + 64].split(b'\0')[0].decode('latin-1')
                if name in FONT_CHUNKS.values() and name not in out:
                    out[name] = Font(d[p:p + n], src.font_page(name))
    return out


def strings(game, language='ENGLISH'):
    """The language table (chunk 0x39000 of LANGUAGES\\<language>.BIN): u32
    header size 16, u32 count, u32 table offset, u32 text offset; the table
    holds (u32 label hash, u32 offset) pairs, the texts are plain ASCII,
    zero-terminated, '^' for a line break."""
    d = load(game, f'LANGUAGES/{language}.BIN')
    t, p, n = next(c for c in chunks(d) if c[0] == 0x39000)
    _, count, table, base = struct.unpack_from('<4I', d, p)
    out = {}
    for k in range(count):
        h, off = struct.unpack_from('<2I', d, p + table + 8 * k)
        e = d.index(b'\0', p + base + off)
        out[h] = d[p + base + off:e].decode('latin-1').replace('^', '\n')
    return out


def ui_text(t, icons):
    """A text as the PSP draws it: the pad's tokens as its icons' codes, latin-1."""
    for token, button in BUTTONS.items():
        t = t.replace(token, ICON_CODE[BUTTON_ICONS[token]] if icons else button)
    for token, name in BUTTON_ICONS.items() if icons else ():
        t = t.replace(token, ICON_CODE[name])
    return t.encode('latin-1', 'replace')


# The PSP's system languages the disc has (the others play in English)
LANGUAGES = {'en': 'ENGLISH', 'fr': 'FRENCH', 'de': 'GERMAN', 'it': 'ITALIAN', 'es': 'SPANISH', 'nl': 'DUTCH'}


def fnv1a(b):
    h = 0x811c9dc5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xffffffff
    return h


def language_table(game, language, icons, path, english):
    """lang_<xx>.n2lg: 'N2LG', u32 count, u32 English count, (u32 label
    hash, u32 offset) sorted by hash, (u32 FNV-1a of the English text, u32
    offset) sorted by hash (the port's own words: the text the label most
    of the English text's labels agree on), then the texts, zero-terminated,
    as ui_text makes them."""
    table = strings(game, language)
    keys = sorted(table)
    blob, offs, at = bytearray(), [], {}
    for h in keys:
        offs.append(len(blob))
        at[h] = len(blob)
        blob += ui_text(table[h], icons) + b'\0'
    by_english = {}
    for h, t in english.items():
        if h in table and t and table[h].strip() and len(table[h]) <= 4 * len(t) + 8:   # not the speech's fragments ("It": a whole line)
            by_english.setdefault(fnv1a(ui_text(t, icons)), []).append(h)
    pairs = []
    for k, hs in by_english.items():
        votes = {}
        for h in hs:
            votes.setdefault(table[h], []).append(h)
        best = max(votes.values(), key=len)[0]
        pairs.append((k, at[best]))
    pairs.sort()
    path.write_bytes(b'N2LG' + struct.pack('<2I', len(keys), len(pairs))
                     + b''.join(struct.pack('<2I', h, o) for h, o in zip(keys, offs))
                     + b''.join(struct.pack('<2I', h, o) for h, o in pairs) + blob)
    return len(keys)


def localise(pkg, table):
    """Text objects that name a label take the language table's text."""
    for o in pkg['objects']:
        h = o.get('label_hash')
        if h is None and o.get('label'):
            h = fe.bin_hash(o['label'])
        if h is not None and h in table:
            o['text'] = table[h]
            o['label_used'] = (h, table[h])   # the other languages' text for it (write: if the text stays the table's)


def hud_package(game, name='HUD_SingleRace.fng', bundle='GLOBAL/INGAMERACE.BUN'):
    for raw in fe.packages(load(game, bundle)):
        pkg = fe.parse(raw)
        if pkg.get('name') == name:
            return pkg
    raise SystemExit(f'{name} not found')


# The race HUD: the package's groups the PSP draws (object hashes). The
# original's code shows and hides them per mode; the rest of the package
# is online, cop chase, showcase and tutorial furniture.
GROUPS = {
    'tach': 0x5164d4ea,         # tachometer, speed, gear, nitrous
    'position': 0xe8434eb4,     # "1st / 4"
    'laps': 0x70f90bd4,         # "Laps 1/2"
    'time': 0x54be67bc,         # race time box
    'board': 0x5ed609a1,        # four-row standings
    'map_mask': 0x40cdc515,     # minimap backdrop
    'map_border': 0x382d2fc9,   # minimap ring (four quarters)
    'map_north': 0xf2a54430,
    'wrongway': 0xc82fa200,
    'bank': 0x3f8644ca,         # "Bank: $" (free roam)
    'turbo': 0xc5d551b7,        # the boost gauge by the tachometer (cars with a turbo)
    'sms': (0x4b9b8f5a, 0x668c9cd6),  # EventIndicator: envelope and static unread count
    'style': (0x27754c7f, 0x15d0e247),   # a style bonus's word and points ("HEAD START"), right, in green
}
SKIP = {0xc5d551b7}  # under another group: the turbo gauge (its own)
# HUD_Drag.fng (GLOBAL/INGAMEDRAG.BUN) in the race HUD's group numbers, so
# the port drives it as the race HUD (the same objects: speed, gear, needle,
# nitrous, the turbo's needle, the place): the tall tachometer with the
# speed, nitrous and engine heat; the place with its backings; the turbo.
DRAG_GROUPS = {
    'tach': (0x5164d4ea, 0x78634aae, 0xc9ef, 0x5b3e62f5, 0x25cf2f74, 0x66842662),   # ... the shift light and its word (ShiftMessage)
    'position': (0x2ddf58c, 0x411301e3, 0xaa4e9ac1),
    'turbo': (0x84cca1c0,),
    'time': (0x657cb989,),   # the race's time under the place (seconds, as the PS2's "2.88")
    'over': (0xa7806ddb, 0x4e4566e5),   # the race over message ("Blown engine!") and its backing
    'style': (0x27754c7f, 0x15d0e247),   # the style bonuses, as the race HUD's
}
# the modes' own groups, numbered after GROUPS (main.c: FEHUD_DRIFT 14,
# FEHUD_OVER 15, FEHUD_SPLIT 16, FEHUD_LAPTIME 17)
MODE_GROUPS = ('drift', 'over', 'split', 'laptime')
# The race HUD's objects its code shows by turns (ROADMAP L9): the race
# over message ("Your time: 1:23.45") over its backing, which dims the
# picture (RaceOverMessage, also the outrun's result in the free roam);
# the split time, green or red (SplitTime); "LAP TIME" and the lap's time
# for 2 s after the line (LapCounter). The drift's HUD has the first two.
RACE_EXTRA = {'over': (0xa7806ddb, 0x4e4566e5), 'split': (0xf59d909f,), 'laptime': (0x22465e2b, 0x565f28ae)}
# The free roam's outrun, numbered after those (main.c: FEHUD_GETAWAY 18 on):
# the getaway's meter (GetAwayMeter: its group, backing, lines, the bar,
# the distance and its unit), its messages ("Outrun Challenge Accepted",
# "You have the Lead!"), the rivals' minimap icons (AIRACERINDICATOR_n:
# drawn by the port where each rival is) and the engage indicator (the
# D-pad with Up lit and the outrun's icon, ICON_RANDOM_ENCOUNTER); its end
# is the race over message's group ('over')
STREET_ENGAGE_TEXTURES = (
    'MODE_ICON_CIRCUIT', 'MODE_ICON_SPRINT', 'MODE_ICON_SHORT_TRACK',
    'MODE_ICON_U_RACING_LEAGUE', 'MODE_ICON_DRAG', 'MODE_ICON_DRIFT', 'MODE_ICON_SUV',
    'SHOP_ICON_CAR_LOT', 'SHOP_ICON_PERFORMANCE', 'SHOP_ICON_BODY',
    'SHOP_ICON_GRAPHIC', 'SHOP_ICON_INTERIORS', 'SHOP_ICON_CRIB')
SMS_INDICATOR_TEXTURES = ('ENGAGE_CINGULAR_ICON',)

FREE_GROUPS = {
    'getaway': 0x8ca76279,
    'outmsg': 0x407919af,
    'racers': tuple(fe.bin_hash(f'AIRACERINDICATOR_{k}') for k in range(8)) +   # ... the player's, the start line's
              (fe.bin_hash('PLAYERCARINDICATOR_1'),) + tuple(fe.bin_hash(f'STARTLINEINDICATOR_{k}') for k in range(3)),
    'engage': 0x3345911d,
    # The free roam's minimap (main.c FEHUD_GPS_RING on): the GPS's target
    # ring (0x697d2bca, two Minimap_GPS_Selector images pulsing), one icon
    # of each kind of trigger zone (MinimapWrapper's pools: the port stamps
    # it where each zone is) and the neighbourhood's name (Region_Message).
    'gps_ring': 0x697d2bca,
    'zones': tuple(fe.bin_hash(f'{kind}INDICATOR_0') for kind in (
        'CIRCUITEVENT', 'SPRINTEVENT', 'DRIFTEVENT', 'DRAGEVENT', 'STREETEVENT', 'URLEVENT', 'SUVEVENT',
        'CRIB', 'CARLOT', 'PAINTSHOP', 'PERFORMANCESHOP', 'PARTSSHOP', 'ICESHOP', 'SHOWCASE')),
    'region': 0x8eb730a7,
    # MinimapWrapper::GPS_Search_Group: original satellite and search text.
    # Append only: existing converter/runtime group ids stay unchanged.
    'gps_search': 0x06254d8a,
}
# HUD_Drift.fng (GLOBAL/INGAMEDRIFT.BUN): the race HUD's groups under the
# same hashes, the laps and the time's backings at the root, and its own:
# the points (Total, Best, the multiplier and what gains the next), the
# drift's word and points over the car, "Too Slow!"
DRIFT_GROUPS = {
    'tach': (0x5164d4ea, 0xb78d9f),   # ... and the slip angle's backing
    'position': (0xe8434eb4,),
    'laps': (0x8b7e6ea1, 0x57f0e3a5, 0xf2685238, 0xa86c3c9b, 0x411301e3),
    'time': (0x54be67bc, 0xb91e1000),
    'board': (0x5ed609a1,),
    'map_mask': (0x40cdc515,),
    'map_border': (0x382d2fc9,),
    'map_north': (0xf2a54430,),
    'wrongway': (0xc82fa200,),
    'turbo': (0xc5d551b7,),
    'drift': (0x1ac732cd, 0xf90b0ac6, 0x7be44faa, 0xbf26517f, 0x39dc75db, 0x39972fa3, 0xd555b3ab, 0x4a4980e9),
    'over': RACE_EXTRA['over'],
    'split': RACE_EXTRA['split'],
    'racers': tuple(fe.bin_hash(f'AIRACERINDICATOR_{k}') for k in range(8)) + (fe.bin_hash('PLAYERCARINDICATOR_1'),),   # the minimap's icons (no start line in a drift)
}
# the drag's scales the game swaps in by the car's limiter (0 to 7 000 ...
# 10 000 rpm over the same arc) and the red stretch from the limiter up
DRAG_SCALES = tuple(f'DRAG_RPM_{k}_LINES' for k in (7000, 8000, 9000, 10000)) + \
    tuple(f'DRAG_RPM_REDLINES_{k}' for k in range(5500, 9001, 500))


def drawn(pkg, groups=None):
    """guid -> group name of every object the race HUD draws (groups: name
    -> root hash or roots, GROUPS by default)."""
    by_hash = {o['hash']: o for o in pkg['objects']}
    kids = {}
    for o in pkg['objects']:
        kids.setdefault(o.get('parent'), []).append(o)
    out = {}
    def add(o, name, root=False):
        if o['hash'] in SKIP and not root:
            return
        out[o['guid']] = name
        for k in kids.get(o['guid'], []):
            add(k, name)
    for name, h in (groups or {**GROUPS, **FREE_GROUPS}).items():
        for root in (h if isinstance(h, tuple) else (h,)):
            add(by_hash[root], name, True)
    return out


# --------------------------------------------------------------- evaluation

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def track_value(track, t):
    """FEng track at time t (ms): the base key plus the delta keys,
    interpolated linearly (splines are drawn linear here)."""
    keys = track['keys']
    base = keys[0][1]
    deltas = keys[1:]
    if not deltas:
        return base
    nxt = 0
    while nxt < len(deltas) - 1 and deltas[nxt][0] < t:
        nxt += 1
    t1, v1 = deltas[nxt]
    if nxt > 0 and t < t1:
        t0, v0 = deltas[nxt - 1]
        f = (t - t0) / (t1 - t0) if t1 > t0 else 1.0
        d = tuple(a + (b - a) * f for a, b in zip(v0, v1))
    else:
        d = v1
    if len(base) == 4 and track['type'] == 5:
        return qmul(d, base)
    return tuple(a + b for a, b in zip(base, d))


def state(obj, script_id=0x1744b3, t=None):
    """The object's fields under a script at time t (default: its end)."""
    st = dict(obj.get('data', {}))
    sc = next((s for s in obj['scripts'] if s.get('id') == script_id), None)
    if sc:
        tt = sc.get('length', 0) if t is None else t
        for name, tr in sc['tracks'].items():
            if tr['keys']:
                st[name] = track_value(tr, tt)
    return st


def z_angle(q):
    x, y, z, w = q
    return math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))


# ------------------------------------------------------------------ preview

EXTRA_ANGLE = {}     # object hash -> radians added (preview of the game's needle)

def preview(pkg, src, fnts, path, W=640, H=480, everything=False):
    img = bytearray(W * H * 4)
    for i in range(0, len(img), 4):
        img[i:i + 4] = b'\x30\x38\x40\xff'
    objs = {o['guid']: o for o in pkg['objects']}
    res = pkg['resources']
    cache = {}

    def tex(o):
        r = o.get('resource', -1)
        if not (0 <= r < len(res)) or res[r][1] != 'image':
            return None
        if r not in cache:
            t = src.image(res[r][0], res[r][2])
            cache[r] = (t.w, t.h, t.rgba()) if t else None
        return cache[r]

    def world(o):
        """2x3 affine (FEng space) and blended RGBA of an object."""
        st = state(o)
        sx, sy = st.get('size', (1, 1, 1))[:2]
        px, py, pz = st.get('position', (0, 0, 0))
        a = z_angle(st.get('rotation', (0, 0, 0, 1))) + EXTRA_ANGLE.get(o['hash'], 0)
        c, s = math.cos(a), math.sin(a)
        m = (c * sx, -s * sy, px, s * sx, c * sy, py)
        col = [v / 255 for v in st.get('color', (255, 255, 255, 255))]
        z = pz
        if o.get('parent') in objs:
            pm, pc, pzz = world(objs[o['parent']])
            m = (pm[0] * m[0] + pm[1] * m[3], pm[0] * m[1] + pm[1] * m[4], pm[0] * m[2] + pm[1] * m[5] + pm[2],
                 pm[3] * m[0] + pm[4] * m[3], pm[3] * m[1] + pm[4] * m[4], pm[3] * m[2] + pm[4] * m[5] + pm[5])
            col = [a * b for a, b in zip(col, pc)]
            z += pzz
        return m, col, z

    def blend(x, y, r, g, b, a):
        a = min(1.0, a)
        if 0 <= x < W and 0 <= y < H and a > 0:
            i = (y * W + x) * 4
            for k, v in enumerate((r, g, b)):
                img[i + k] = max(0, min(255, int(img[i + k] * (1 - a) + min(255, max(0, v)) * a)))

    def quad(m, col, uv0, uv1, t):
        # corners of the unit quad, mapped to the screen
        pts = [(m[0] * u + m[1] * v + m[2] + W / 2, m[3] * u + m[4] * v + m[5] + H / 2) for u, v in ((-.5, -.5), (.5, -.5), (.5, .5), (-.5, .5))]
        x0, x1 = max(0, int(min(p[0] for p in pts))), min(W - 1, int(max(p[0] for p in pts)) + 1)
        y0, y1 = max(0, int(min(p[1] for p in pts))), min(H - 1, int(max(p[1] for p in pts)) + 1)
        det = m[0] * m[4] - m[1] * m[3]
        if abs(det) < 1e-9:
            return
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                dx, dy = x + .5 - W / 2 - m[2], y + .5 - H / 2 - m[5]
                u = (m[4] * dx - m[1] * dy) / det + .5
                v = (-m[3] * dx + m[0] * dy) / det + .5
                if not (0 <= u < 1 and 0 <= v < 1):
                    continue
                if t:
                    tw, th, px = t
                    tu, tv = uv0[0] + (uv1[0] - uv0[0]) * u, uv0[1] + (uv1[1] - uv0[1]) * v
                    i = ((int(tv * th) % th) * tw + int(tu * tw) % tw) * 4
                    r, g, b, a = px[i] / 255, px[i + 1] / 255, px[i + 2] / 255, px[i + 3] / 255
                else:
                    r = g = b = a = 1
                blend(x, y, r * col[0] * 255, g * col[1] * 255, b * col[2] * 255, a * col[3])

    def text(o, m, col):
        r = o.get('resource', -1)
        f = fnts.get(FONT_CHUNKS.get(res[r][2].upper(), '')) if 0 <= r < len(res) else None
        if not f or not f.page:
            return
        s = o.get('text', '')
        fmt = o.get('format', 0)
        lines = s.split('\n')
        pw, ph, px = f.page.w, f.page.h, f.page.rgba()
        y = 0
        if fmt & 4:
            y = -len(lines) * f.line / 2
        elif fmt & 8:
            y = -len(lines) * f.line
        for line in lines:
            w = f.width(line)
            x = -w / 2 if fmt & 1 else -w if fmt & 2 else 0
            for ch in line:
                g = f.glyphs.get(ord(ch))
                if not g:
                    continue
                gw, gh, gx, gy, xo, yo, adv = g
                for j in range(gh):
                    for i in range(gw):
                        k = ((gy + j) * pw + gx + i) * 4
                        a = px[k + 3] / 255 * col[3]
                        sxp, syp = x + xo + i, y + yo + j
                        X, Y = m[0] * sxp + m[1] * syp + m[2] + W / 2, m[3] * sxp + m[4] * syp + m[5] + H / 2
                        blend(int(X), int(Y), px[k] * col[0], px[k + 1] * col[1], px[k + 2] * col[2], a)
                x += adv
            y += f.line

    items = []
    show = {o['guid']: '' for o in pkg['objects']} if everything else drawn(pkg)
    for o in pkg['objects']:
        if o.get('type') in ('group', None) or (o.get('flags', 0) & 1) or o['guid'] not in show:
            continue
        m, col, z = world(o)
        if col[3] <= 0:
            continue
        items.append((z, o, m, col))
    for z, o, m, col in sorted(items, key=lambda it: -it[0]):
        if o['type'] == 'text':
            # text is not scaled by its object's size (1 x 1)
            text(o, m, col)
        else:
            st = state(o)
            quad(m, col, st.get('uv0', (0, 0)), st.get('uv1', (1, 1)), tex(o))
    write_png(path, W, H, img)


# ------------------------------------------------------------------- output
#
# hud.n2ui (little endian), every section 4-aligned:
#   'N2UI', u32 version 3, u32 counts: textures, fonts, glyphs, objects,
#     scripts, tracks, keys, text bytes; u32 city map texture (0xffffffff
#     none), f32 x 6 its placement (u, v in pixels from game X, Y: MAP_FIT)
#   textures: u16 w, h, u32 offset of 1024-byte CLUT + w*h swizzled T8,
#     u32 bin hash of the texture's name (0 fonts, map)
#   fonts: u16 texture, u16 glyph count, u32 first glyph, f32 line height
#   glyphs (12 B): u16 code, u8 w, h, u16 x, y, s8 x offset, y offset, u8
#     advance, pad
#   objects (88 B): u32 hash, s16 parent, u8 type (1 image, 2 text, 5
#     group), u8 group (GROUPS order + 1; screens 1), s16 texture or font, u16 flags
#     (1 additive, 2..3 anchor: 0 centre, 1 left, 2 right, 8 a panel's
#     piece: widens with a widened screen), u16 first
#     script, u16 scripts, u16 text offset, u16 text length, u16 format,
#     u16 pad, f32 colour RGBA (0-255), pivot xy, position xyz, angle (rad,
#     about the screen axis), size xy, uv u0 v0 u1 v1
#   scripts (20 B): u32 id (bin hash of the name), u32 length (ms), u32
#     flags (1 loop), s16 chained script (index, -1), u16 first track, u16
#     tracks, u16 pad
#   tracks (16 B): u8 field (0 colour 4, 1 pivot 2, 2 position 3, 3 angle
#     1, 4 size 2, 5 uv0 2, 6 uv1 2 components), u8 components, u8
#     interpolation, u8 action (1 wraps), u32 first key, u32 keys; the
#     first key is the base, the others deltas added to it
#   keys (20 B): i32 time (ms, -1 base), f32 x 4
#   text: the objects' default strings
FIELDS = {'color': (0, 4), 'pivot': (1, 2), 'position': (2, 3), 'rotation': (3, 1), 'size': (4, 2), 'uv0': (5, 2), 'uv1': (6, 2)}
# The menus' furniture: boxes, bars and their corners, a few texels drawn
# at any size. On the PSP's wider screen (fe_widen) they stretch across
# with it; icons, arrows, logos and maps keep their shape.
PANEL_PARTS = ('U2_MENU_BOX', 'U2_MENU_CORNER', 'U2_MENU_ROUNDEDCORNER', 'U2_MENU_TEXT_CORNER', 'U2_SCROLLER_CORNER',
               'U2_LOADING_GRADIENT', 'U2_TEMP_SLIDER', 'WHITE16X16', 'WHITE_16X16')

ADDITIVE = {'TACH_NOS'}      # DXT1 white-on-black: drawn added, as the original's masked fill
FULL_RES = {'LS_SPLASH', 'NFSU_LOGO_CHROME', 'U2_LOADING_BG'}   # the title's and the loading screen's art and logo: their own size, the screen's picture
# the drag's tall tachometer: its scale's numbers and ticks need twice the
# HUD's usual 128 (the arc stands over the whole screen)
SHARP = {'DRAG_RPM_BACKING'} | {f'DRAG_RPM_{k}_LINES' for k in (7000, 8000, 9000, 10000)}


def tex_limit(stem):
    if stem.startswith('DECAL_') and not stem.startswith('DECAL_ZONE_'):   # the decal shop's 107 brands, the zones' slots: small, the parts browser's file whole in memory
        return 64
    return 512 if stem in FULL_RES else 256 if stem in SHARP else 128


def texture_blob(tex, limit):
    px = tex.rgba()
    if tex.fmt == 0x20 and max(px[3::4]) <= 128:
        # uncompressed A8R8G8B8 authored the PS2 way, 0x80 = 1.0 (White16x16)
        px = bytes(min(255, 2 * v) for v in px)
    rgba, w, h = box_downscale(px, tex.w, tex.h, limit)
    pal, idx = quantize(rgba)
    while w < 16 or h < 8:
        sx, sy = 2 if w < 16 else 1, 2 if h < 8 else 1
        idx = bytes(idx[(y // sy) * w + x // sx] for y in range(h * sy) for x in range(w * sx))
        w, h = w * sx, h * sy
    return w, h, bytes(pal) + psp_swizzle(idx, w, h)


# The tachometer's 11 styles (the garage's Custom Gauges: STOCK, FLAMES,
# GRIFFIN ... BLING, the parts table's type 64): GLOBAL\HUD_CUSTOMTEXTURES_<nn>.BIN
# hold each style's six textures (CUSTOM), the size of style 00's in the HUD
# screens, so the PSP refills those slots (fe_texture_read).
# hudstyles.n2tx: 'N2HS', u32 version 1, u32 styles, u32 textures, textures x
# u32 the HUD's texture hash, styles x textures x u32 offset; then (older
# readers stop before it) 'N2HX', the tachometer by the car's engine (the
# game's ChooseLoadableTextures): u32 the scale's slot hash
# (3RDPERSON_10500LINES), u32 4, styles x 4 x u32 offset (each style's
# 7 500, 8 500, 9 500 and 10 500 rpm scales), u32 the red mark's slot hash
# (3RDPERSON_REDLINE), u32 8, 8 x u32 offset (3RDPERSON_REDLINE_5500 to
# _9000, GLOBAL\INGAMECOMMON.LZC); the data (each a 1024-byte CLUT and
# swizzled T8, as the screens').
HUD_STYLES = 11
TACH_SCALES = (7500, 8500, 9500, 10500)
TACH_REDLINES = tuple(range(5500, 9001, 500))


FX = (('SFX_SMOKE_B_CS', 2, False), ('SPARK', 1, False), ('SFX_FIREB', 2, True))   # smoke, sparks, flames (psp-viewer/fx.h's order)


def rain_textures(src, path):
    """rain.n2tx: the drops on the screen (GLOBAL\\GLOBALB.LZC): RAINDROP, a
    light disc (the PS2's alpha, 0x80 full), and RAINDROPMASK, its dark rim,
    each 32 x 32. 'N2RN', u32 version 1, u32 textures, u32 size; textures x
    size x size u32 (ABGR)."""
    import numpy as np
    from PIL import Image
    size, blob = 32, bytearray()
    for name in ('RAINDROP', 'RAINDROPMASK'):
        t = src.image(0, name + '.tga')
        a = np.frombuffer(bytes(t.rgba()), np.uint8).reshape(t.h, t.w, 4).astype(float)
        if name == 'RAINDROP':
            a[..., 3] = np.minimum(255, a[..., 3] * 2)
        blob += Image.fromarray(a.clip(0, 255).astype(np.uint8), 'RGBA').resize((size, size), Image.LANCZOS).tobytes()
    path.write_bytes(struct.pack('<4s3I', b'N2RN', 1, 2, size) + blob)
    return 2


def fx_textures(src, path):
    """fx.n2tx: the game's particle art (GLOBAL\\INGAMECOMMON.LZC) for the
    car's smoke, sparks and nitrous flames: a cell of each atlas (2 x 2 for
    the smoke and flames), 32 x 32. 'N2FX', u32 version 1, u32 textures, u32
    size; textures x size x size u32 (the PSP's ABGR). The smoke's alpha is
    the PS2's (0x80 full); the flames' black is their alpha (added)."""
    import numpy as np
    from PIL import Image
    size, blob = 32, bytearray()
    for name, cells, added in FX:
        t = src.image(0, name + '.tga')
        a = np.frombuffer(bytes(t.rgba()), np.uint8).reshape(t.h, t.w, 4).astype(float)
        a = a[:t.h // cells, :t.w // cells]
        if added:   # black is nothing: its brightness the alpha, the colour at full
            m = a[..., :3].max(axis=2)
            a[..., :3] = a[..., :3] * (255 / np.maximum(m, 1))[..., None]
            a[..., 3] = m
        elif name.endswith('_CS'):
            a[..., 3] = np.minimum(255, a[..., 3] * 2)
        im = Image.fromarray(a.clip(0, 255).astype(np.uint8), 'RGBA').resize((size, size), Image.LANCZOS)
        blob += im.tobytes()   # R, G, B, A bytes: the PSP's 8888 as is
    path.write_bytes(struct.pack('<4s3I', b'N2FX', 1, len(FX), size) + blob)
    return len(FX)


def damage_textures(src, path):
    """damage.n2tx: the car's visible damage (GLOBAL\\GLOBALB.LZC): the
    windows' third crack, WINDOW_DAMAGE2 (what CarDamage puts on the six
    windows and the rear defroster at a part's full damage), 64 x 64, and
    a shard of XETT_GLASSB (the XECD emitters' art), its first of 8 x 4
    cells, 32 x 32. 'N2DM', u32 version 1, u32 64, u32 32; the two images
    in u32 (the PSP's ABGR), alpha as is (255 full)."""
    import numpy as np
    from PIL import Image
    blob = bytearray()
    for name, size, cells in (('WINDOW_DAMAGE2', 64, (1, 1)), ('XETT_GLASSB', 32, (8, 4))):
        t = src.image(0, name + '.tga')
        a = np.frombuffer(bytes(t.rgba()), np.uint8).reshape(t.h, t.w, 4)
        a = a[:t.h // cells[1], :t.w // cells[0]]
        blob += Image.fromarray(np.ascontiguousarray(a), 'RGBA').resize((size, size), Image.LANCZOS).tobytes()
    path.write_bytes(struct.pack('<4s3I', b'N2DM', 1, 64, 32) + blob)
    return 2


SKIDS = ('SKID_ROAD', 'SKID_DIRT')   # psp-viewer/terrain.h's SKID_TEX_* (SKID_GRASS is SKID_DIRT's art)


def skid_textures(src, path):
    """skids.n2tx: the tyre marks' art (GLOBAL\\INGAMECOMMON.LZC, the
    TerrainTypeInfoTable's SKID_ROAD and SKID_DIRT), 64 x 32, along the
    mark across, its ragged edges at the top and bottom. 'N2SK', u32
    version 1, u32 textures, u32 width, u32 height; textures x w x h u32
    (the PSP's ABGR). The alpha is the PS2's (0x80 full), doubled."""
    import numpy as np
    blob, w, h = bytearray(), 64, 32
    for name in SKIDS:
        t = src.image(0, name + '.tga')
        if (t.w, t.h) != (w, h):
            raise ValueError(f'{name}: {t.w}x{t.h}')
        a = np.frombuffer(bytes(t.rgba()), np.uint8).reshape(t.h, t.w, 4).astype(float)
        a[..., 3] = np.minimum(255, a[..., 3] * 2)
        blob += a.clip(0, 255).astype(np.uint8).tobytes()
    path.write_bytes(struct.pack('<4s4I', b'N2SK', 1, len(SKIDS), w, h) + blob)
    return len(SKIDS)


def hud_styles(game, src, path):
    stems = list(CUSTOM)
    blobs, size, scales = [], None, []
    for nn in range(HUD_STYLES):
        tpk = SlotTPK(load(game, f'GLOBAL/HUD_CUSTOMTEXTURES_{nn:02d}.BIN'))
        names = {tpk.name(k): k for k in tpk.slots}
        row = []
        for stem in stems:
            name = f'TACH_FILL_CUSTOM_{nn:02d}' if stem == 'TACH_FILL' else CUSTOM[stem]
            w, h, blob = texture_blob(tpk.get(names[name]), tex_limit(stem))
            if nn == 0:
                row.append((w, h))
            elif (w, h) != size[stems.index(stem)]:
                raise SystemExit(f'{name} {w}x{h}: not style 00\'s size')
            blobs.append(blob)
        if nn == 0:
            size = row
        lines = size[stems.index('3RDPERSON_10500LINES')]
        for k in TACH_SCALES:   # the names cut at 23 as the TPK's
            name = f'3RDPERSON_{k}LINES_CUSTOM_{nn:02d}'[:23]
            w, h, blob = texture_blob(tpk.get(names[name]), tex_limit('3RDPERSON_10500LINES'))
            if (w, h) != lines:
                raise SystemExit(f'{name} {w}x{h}: not the scale slot\'s size')
            scales.append(blob)
    reds = []
    for k in TACH_REDLINES:
        w, h, blob = texture_blob(src.image(0, f'3RDPERSON_REDLINE_{k}.tga'), tex_limit('3RDPERSON_REDLINE'))
        reds.append(blob)
    head = struct.pack('<4s3I', b'N2HS', 1, HUD_STYLES, len(stems)) + b''.join(struct.pack('<I', fe.bin_hash(s)) for s in stems)
    ext_len = 4 + 8 + 4 * len(scales) + 8 + 4 * len(reds)
    at = len(head) + 4 * len(blobs) + ext_len
    offsets = []
    for b in blobs + scales + reds:
        offsets.append(at)
        at += len(b)
    n = len(blobs)
    ext = b'N2HX' + struct.pack('<2I', fe.bin_hash('3RDPERSON_10500LINES'), len(TACH_SCALES)) + \
        b''.join(struct.pack('<I', o) for o in offsets[n:n + len(scales)]) + \
        struct.pack('<2I', fe.bin_hash('3RDPERSON_REDLINE'), len(TACH_REDLINES)) + \
        b''.join(struct.pack('<I', o) for o in offsets[n + len(scales):])
    assert len(ext) == ext_len
    path.write_bytes(head + b''.join(struct.pack('<I', o) for o in offsets[:n]) + ext + b''.join(blobs + scales + reds))
    return len(blobs) + len(scales) + len(reds)


def title_art():
    """Optional local widescreen art, packed by convert_title_art.py.

    The supplied crop is stored at 512x256 and mapped to 480x272 by
    splash_layout, with no additional crop or generated extension.
    """
    path = ROOT / 'game/frontend/title-wide.n2ta'
    if not path.exists():
        return None
    data = path.read_bytes()
    if len(data) != 8 + 1024 + 512 * 256 or data[:8] != struct.pack('<4sHH', b'N2TA', 512, 256):
        raise ValueError(f'invalid title art: {path}')
    return 512, 256, data[8:]


# The free-roam map (the minimap's picture): TRACKS\TRACKMAP4000.BIN, a 512 x
# 512 DXT5 page with its alpha unused. Its placement is not in the file:
# fitted once to the road graph (every road node on a bright street pixel),
# u = 0.0910625 X + 311.904, v = -0.0910625 Y + 299.266 (game X/Y, 10.98 m
# a pixel).
MAP_FIT = (0.0910625, 0.0, 311.904, 0.0, -0.0910625, 299.266)


def map_texture(game):
    tpk = FlatTPK(load(game, 'TRACKS/TRACKMAP4000.BIN'))
    tex = tpk.get(next(iter(tpk.records)))
    px = bytearray(tex.rgba())
    px[3::4] = bytes([255]) * (len(px) // 4)
    class T: pass
    t = T()
    t.w, t.h, t.fmt = tex.w, tex.h, tex.fmt
    t.rgba = lambda: px
    return t


# Controller prompts in the screens' strings: the PSP's buttons.
BUTTONS = {'$JOY_EVENT_FENG_SELECT$': 'X', '$JOY_EVENT_FENG_CANCEL$': '/\\', '$JOY_EVENT_FENG_BUTTON1$': '[]',
           '$JOY_EVENT_FENG_BUTTON2$': '/\\', '$JOY_EVENT_FENG_ACCEPT$': 'X',
           '$JOY_EVENT_FENG_LTRIGGER$': 'L', '$JOY_EVENT_FENG_RTRIGGER$': 'R', '$JOY_EVENT_FENG_START$': 'START',
           '$JOY_EVENT_FENG_BUTTON0$': 'SELECT'}   # without the PS2's icons
# The prompts' icons (the PS2's, tools/ps2_buttons.py): a control code in the
# text each (0x11 on, clear of the line break), drawn by psp-viewer/fe.c as
# the icon a line high; every screen carries them, for the port's own lines.
ICONS = ('PS2_CROSS', 'PS2_CIRCLE', 'PS2_SQUARE', 'PS2_TRIANGLE', 'PSP_L', 'PSP_R', 'PS2_START', 'PS2_SELECT',
         'PS2_DPADL', 'PS2_DPADR', 'PS2_DPADU', 'PS2_DPADD', 'PS2_DPADUPDOWN', 'PS2_LEFTANALOG')
ICON_CODE = {name: chr(0x11 + k) for k, name in enumerate(ICONS)}
BUTTON_ICONS = {'$JOY_EVENT_FENG_SELECT$': 'PS2_CROSS', '$JOY_EVENT_FENG_ACCEPT$': 'PS2_CROSS',
                '$JOY_EVENT_FENG_CANCEL$': 'PS2_TRIANGLE', '$JOY_EVENT_FENG_BUTTON1$': 'PS2_SQUARE',   # the PS2's Back: Triangle (O is its Help)
                '$JOY_EVENT_FENG_BUTTON2$': 'PS2_TRIANGLE', '$JOY_EVENT_FENG_LTRIGGER$': 'PSP_L',
                '$JOY_EVENT_FENG_RTRIGGER$': 'PSP_R', '$JOY_EVENT_FENG_PAD_LEFT$': 'PS2_DPADL',
                '$JOY_EVENT_FENG_PAD_RIGHT$': 'PS2_DPADR', '$JOY_EVENT_FENG_PAD_UP$': 'PS2_DPADU',
                '$JOY_EVENT_FENG_PAD_DOWN$': 'PS2_DPADD', '$JOY_EVENT_FENG_START$': 'PS2_START',
                '$JOY_EVENT_FENG_BUTTON0$': 'PS2_SELECT',   # the replay's bar: Start exits, Select hides it
                '$JOY_EVENT_FENG_HELP$': 'PS2_CIRCLE'}   # the footer's Help (Circle, the screens with one: HELP_FNGS)
HELP_FNGS = set()   # the packages with a help (help_texts), lower case
HIDDEN = 0x1 | 0x8 | 0x80000000     # invisible, PC only, hidden in the editor
# The frontend's furniture the PSP has no use for: the help button, EA
# Messenger (online), the designers' debug labels and placeholder strings.
DROP_TEXT = {'Help', '$JOY_EVENT_FENG_HELP$', 'Event Handler', 'Track Number DEBUG', 'Filter type goes here',
             'DEFAULT STRING'}
DROP_GROUP_OF = {'EA Messenger'}
# hidden in their package, shown by the game's code: the port shows them too
# (hash -> its package, None: any)
SHOWN_BY_CODE = {0x25b79b9f: None,   # the dyno chart's Max Torque / Max Power box
                 0x1c7fcf8b: 'UI_PostRaceStats.fng', 0x35e8f0b2: 'UI_PostRaceStats.fng',   # Post Race Stats' Triangle Back
                 0x812a09d4: 'UI_postrace.fng', 0x6a218478: 'UI_postrace.fng'}   # Post Race's Back and Select


def visible(pkg, drop=False):
    """guid -> '' of every object a screen shows (not hidden, not PC only,
    nor under a hidden group); drop: without the DROP_TEXT furniture."""
    objs = {o['guid']: o for o in pkg['objects']}
    gone = set()
    if drop:
        for o in pkg['objects']:
            t = o.get('text')
            if t in DROP_TEXT and not (t in ('Help', '$JOY_EVENT_FENG_HELP$') and pkg.get('name', '').lower() in HELP_FNGS):   # the footer's Help where Circle opens one
                gone.add(o['guid'])
            if t in DROP_GROUP_OF and o.get('parent') is not None:
                gone.add(o['parent'])
    def shown(o):
        while o is not None:
            by_code = o['hash'] in SHOWN_BY_CODE and SHOWN_BY_CODE[o['hash']] in (None, pkg.get('name'))
            if (o.get('flags', 0) & HIDDEN and not by_code) or o['guid'] in gone:
                return False
            o = objs.get(o.get('parent'))
        return True
    return {o['guid']: '' for o in pkg['objects'] if shown(o)}


def write(pkg, src, fnts, path, citymap=None, screen=False, extra=(), drop=False, groups=None, splash=None):
    """screen: draw every visible object (one group, centred), else the
    race HUD's GROUPS anchored to the edges; extra: more textures (by name)
    the game swaps in; drop: leave out the frontend's DROP_TEXT furniture."""
    show = visible(pkg, drop) if screen else drawn(pkg, groups)
    objs = [o for o in pkg['objects'] if o['guid'] in show]
    index = {o['guid']: i for i, o in enumerate(objs)}
    res = pkg['resources']
    group_ids = {name: k + 1 for k, name in enumerate(GROUPS)}
    for k, name in enumerate(MODE_GROUPS):   # the modes' own groups after the race HUD's
        group_ids[name] = len(GROUPS) + 1 + k
    for k, name in enumerate(FREE_GROUPS):   # the free roam's outrun after those
        group_ids[name] = len(GROUPS) + len(MODE_GROUPS) + 1 + k
    group_ids[''] = 1
    textures, tex_index, blobs = [], {}, []
    def texture(key, make, h=0):
        if key not in tex_index:
            w, ht, blob = make()
            tex_index[key] = len(textures)
            textures.append((w, ht, h))
            blobs.append(blob)
        return tex_index[key]
    for name in extra:
        if name in BLANKS:     # a slot the game fills at run time (fe_texture_data)
            w, h = BLANKS[name]
            texture(name, lambda: (w, h, bytes(1024 + w * h)), fe.bin_hash(name))
            continue
        tex = src.image(0, name) or next((t.by_name(name.rsplit('.', 1)[0].upper()[:23]) for t in src.flat
                                          if t.by_name(name.rsplit('.', 1)[0].upper()[:23])), None)
        if tex is None:
            raise SystemExit(f'texture {name} not found')
        stem = name.rsplit('.', 1)[0].upper()
        texture(stem, lambda: texture_blob(tex, tex_limit(stem)), fe.bin_hash(stem))
    icons = bool(src.buttons)
    for name in ICONS if icons else ():
        if name in src.buttons:
            texture(name, lambda name=name: texture_blob(src.buttons[name], 32), fe.bin_hash(name))
    font_list, font_index, glyphs, labels = [], {}, [], []
    def font(name):
        if name not in font_index:
            f = fnts[name]
            ti = texture('font:' + name, lambda: texture_blob(f.page, 1024))
            font_index[name] = len(font_list)
            font_list.append((ti, len(f.glyphs), len(glyphs), f.line))
            for code in sorted(f.glyphs):
                w, h, x, y, xo, yo, adv = f.glyphs[code]
                glyphs.append((code, w, h, x, y, xo, yo, adv))
        return font_index[name]
    scripts, tracks, keys, text = [], [], [], bytearray()
    out_objs = []
    # anchor of each root group: screen-edge groups follow the PSP's edges
    def root(o):
        while o.get('parent') in index:
            o = objs[index[o['parent']]]
        return o
    for o in objs:
        d = o.get('data', {})
        kind = {'image': 1, 'coloured': 1, 'text': 2, 'group': 5}[o['type']]   # coloured: an image with corner colours (drawn in its own)
        ti, flags = -1, 0
        r = o.get('resource', -1)
        name = res[r][2] if 0 <= r < len(res) else ''
        if kind == 1 and res[r][1] == 'image':
            tex = src.image(res[r][0], name)
            if tex is not None:
                stem = name.rsplit('.', 1)[0].upper()
                override = splash if stem in {'LS_SPLASH', 'U2_LOADING_BG'} else None
                ti = texture(stem, lambda: override if override is not None else texture_blob(tex, tex_limit(stem)), fe.bin_hash(stem))
                if stem in ADDITIVE:
                    flags |= 1
                if stem.startswith(PANEL_PARTS):
                    flags |= 8
        elif kind == 2:
            ti = font(FONT_CHUNKS[name.upper()])
        rx = root(o)['data']['position'][0]
        if not screen:
            flags |= (1 if rx < -80 else 2 if rx > 80 else 0) << 1
        first = len(scripts)
        for sc in o['scripts']:
            ft = len(tracks)
            for tname, tr in sc['tracks'].items():
                if tname not in FIELDS or not tr['keys']:
                    continue
                field, n = FIELDS[tname]
                fk = len(keys)
                for t, v in tr['keys']:
                    if tname == 'rotation':
                        v = (z_angle(v),)
                    keys.append((t, tuple(v[:n]) + (0,) * (4 - n)))
                tracks.append((field, n, tr['interp'], tr['action'], tr['length'], fk, len(keys) - fk))
            scripts.append([sc.get('id', 0), sc.get('length', 0), sc.get('flags', 0), sc.get('chain'), ft, len(tracks) - ft])
        # chained ids -> indices within the object's scripts
        own = scripts[first:]
        for s2 in own:
            s2[3] = next((first + k for k, x in enumerate(own) if x[0] == s2[3]), -1) if s2[3] is not None else -1
        t = ui_text(o.get('text', ''), icons)
        lab = o.get('label_used')
        labels.append(lab[0] if lab and lab[1] == o.get('text') else 0)
        to = len(text)
        text += t
        c = d.get('color', (255, 255, 255, 255))
        out_objs.append(struct.pack('<IhBBhHHHHHHH', o['hash'], index.get(o.get('parent'), -1), kind,
                                    group_ids[show[o['guid']]], ti, flags, first, len(o['scripts']), to, len(t),
                                    o.get('format', 0), 0)
                        + struct.pack('<16f', *c, *d.get('pivot', (0, 0, 0))[:2], *d.get('position', (0, 0, 0)),
                                      z_angle(d.get('rotation', (0, 0, 0, 1))), *d.get('size', (1, 1, 1))[:2],
                                      *d.get('uv0', (0, 0)), *d.get('uv1', (1, 1))))
    map_ti = texture('map', lambda: texture_blob(citymap, 512), fe.bin_hash('CITYMAP')) if citymap else 0xffffffff
    head = struct.pack('<4s9I', b'N2UI', 3, len(textures), len(font_list), len(glyphs), len(out_objs),
                       len(scripts), len(tracks), len(keys), len(text))
    head += struct.pack('<I6f', map_ti, *MAP_FIT)
    body = bytearray()
    tex_table_at = len(head)
    body += bytes(12 * len(textures))
    for ti, n, first, line in font_list:
        body += struct.pack('<HHIf', ti, n, first, line)
    for g in glyphs:
        body += struct.pack('<H2B2H2bBx', *g)
    for ob in out_objs:
        body += ob
    for sid, length, fl, chain, ft, nt in scripts:
        body += struct.pack('<3IhHHxx', sid, length, fl, chain, ft, nt)
    for tr in tracks:
        body += struct.pack('<4B3I', *tr)
    for t, v in keys:
        body += struct.pack('<i4f', t, *v)
    body += text
    body += bytes(-len(body) % 4)
    body += b'N2LB' + struct.pack(f'<{len(labels)}I', *labels)   # each object's text label (0 none): the language tables'
    body += bytes(-len(body) % 16)
    out = bytearray(head + body)
    for k, ((w, h, th), blob) in enumerate(zip(textures, blobs)):
        out += bytes(-len(out) % 16)
        struct.pack_into('<HHII', out, tex_table_at + 12 * k, w, h, len(out), th)
        out += blob
    path.write_bytes(out)
    return dict(textures=len(textures), fonts=len(font_list), glyphs=len(glyphs), objects=len(out_objs),
                scripts=len(scripts), tracks=len(tracks), keys=len(keys), bytes=len(out))


# Texture slots the game fills while a screen is up: the selected event's
# track map (maps.n2tx), the city map.
MAP_SIZE = 256
BLANKS = {'EVENTMAP': (MAP_SIZE, MAP_SIZE)}


def quick_race_tracks(game):
    """The events the quick race lists, as the original's track select
    (UIQRTrackSelect::IsAvailable): those with a TrackInfo whose race types
    (+0x94, a bit per type) include one and that are not hidden (+0x88, the
    tuning tracks 4706-4709). The drags 4202 and 4203 and the drift 4341
    have none: no map either, their names only their numbers. Event id ->
    its place in the list: the records' order, as the PS2's (its drags:
    Bayview Bridge, Tunnel Construction, North Freeway, Coastal Express ...
    Central Station; all open, the sort by unlocking keeps it)."""
    out = {}
    for k, r in enumerate(track_info_records(game)):
        if struct.unpack_from('<I', r, 0x94)[0] and not r[0x88]:
            out.setdefault(struct.unpack_from('<h', r, 0x8a)[0], k)
    return out


NAME_LEN = 32   # "Bayview Speedway Track 1" is 24: the old 24 bytes cut its number


def track_lengths(game):
    """The distance the original shows for each event (the quick race's
    track select, UIQRTrackSelect::RefreshHeader, and the career's event
    screen, PlayableEvent): its TrackInfo's +0x98, whole metres. A
    nominal figure, not the course's: the airport's drags 4221-4223 read
    1609 m (a mile) and run their markers' 1609; the rail yard's 4211 and
    4212 read 1711 and run 1609; the freeway drags 4401, 4402 and 4404
    read 2361, 2466 and 3065, more than their catalog's whole outlines."""
    out = {}
    for r in track_info_records(game):
        out.setdefault(struct.unpack_from('<h', r, 0x8a)[0], struct.unpack_from('<i', r, 0x98)[0])
    return out


def track_maps(game, ids, table, path):
    """maps.n2tx: every event's name (TRACK_NAME_<id>), whether the quick
    race lists it (quick_race_tracks) and its track map, if it has one
    (TRACKS\\TRACKMAP<id>.BIN: the city map with the route drawn in, the
    picture the quick race and the event dialogs show), 256 x 256 T8:
    'N2T3', u32 count, count x (u32 event id, u32 offset (0: no map),
    char name[32], u8 route box x0,y0,x1,y1 in map pixels: the white
    line's bounds, which the quick race's track screen frames as the
    PS2's, u8 flags: 1 in the quick race, u8 pad, u16 its place in the
    quick race's list, u32 the distance shown, m (track_lengths; 0 none)),
    then 1024-byte CLUT + 256 x 256 swizzled indices each."""
    quick, lengths = quick_race_tracks(game), track_lengths(game)
    blobs = []
    for eid in ids:
        try:
            tpk = FlatTPK(load(game, f'TRACKS/TRACKMAP{eid}.BIN'))
        except ValueError:
            blobs.append((eid, None, (0, 0, 0, 0)))   # named, no map
            continue
        tex = tpk.get(next(iter(tpk.records)))
        px = bytearray(tex.rgba())
        px[3::4] = bytes([255]) * (len(px) // 4)
        white = [i for i in range(0, len(px), 4) if min(px[i:i+3]) > 200]
        xs, ys = [(i // 4) % tex.w for i in white], [(i // 4) // tex.w for i in white]
        box = (min(xs), min(ys), max(xs), max(ys)) if white else (0, 0, tex.w - 1, tex.h - 1)
        box = tuple(min(255, c * MAP_SIZE // tex.w) for c in box)
        class T: pass
        t = T()
        t.w, t.h, t.fmt, t.rgba = tex.w, tex.h, tex.fmt, (lambda px=px: px)
        w, h, blob = texture_blob(t, MAP_SIZE)
        assert (w, h) == (MAP_SIZE, MAP_SIZE)
        blobs.append((eid, blob, box))
    out = bytearray(struct.pack('<4sI', b'N2T3', len(blobs)))
    at = len(out) + (20 + NAME_LEN) * len(blobs)
    at += -at % 16
    for eid, blob, box in blobs:
        name = table.get(fe.bin_hash(f'TRACK_NAME_{eid}'), str(eid)).encode('latin-1', 'replace')[:NAME_LEN - 1]
        out += struct.pack(f'<2I{NAME_LEN}s4B2BHI', eid, at if blob else 0, name, *box, int(eid in quick), 0, quick.get(eid, 0xffff),
                           max(0, lengths.get(eid, 0)))
        at += len(blob) if blob else 0
    out += bytes(-len(out) % 16)
    for _, blob, _ in blobs:
        out += blob or b''
    path.write_bytes(out)
    return sum(1 for _, b, _ in blobs if b)


def track_info_records(game):
    """TrackInfo (GLOBAL\\GLOBALB.BUN chunk 0x34201): its 0x128-byte records."""
    def walk(d):
        for t, p, n in chunks(d):
            if t == 0x34201:
                return d[p:p + n]
            if t & 0x80000000:
                r = walk(d[p:p + n])
                if r:
                    return r
    b = walk(load(game, 'GLOBAL/GLOBALB.BUN'))
    return [b[k * 0x128:(k + 1) * 0x128] for k in range(len(b) // 0x128)]


def track_infos(game):
    """TrackInfo: track number -> (map origin game x, y, metres the map
    spans, the minimap's zoom, the start line's heading in degrees): +0x8a,
    +0xac, +0xb0, +0xb4, +0xc0, +0xba (u16, 65536 a turn)."""
    out = {}
    for r in track_info_records(game):
        ox, oy, scale = struct.unpack_from('<3f', r, 0xac)
        out.setdefault(struct.unpack_from('<h', r, 0x8a)[0],
                       (ox, oy, scale, struct.unpack_from('<f', r, 0xc0)[0], struct.unpack_from('<H', r, 0xba)[0] * 360 / 65536))
    return out


def minimaps(game, ids, path):
    """minimaps.n2mm: the minimap's picture per event, as the game's
    Minimap::Tick swaps it in (TRACKMAP<id>: the city map with the route
    drawn, or a closed track's), at the HUD map slot's 512 x 512 T8, and
    its TrackInfo (the event's own, else its family's: 4011 -> 4000):
    'N2MM', u32 version 1, u32 count, count x 32 bytes (u32 event id, f32
    origin game x, y, metres across, zoom, start heading in degrees, u32
    offset, zlib size), then the zlib blobs (1024-byte CLUT + 512 x 512
    swizzled indices). The free roam's own (4000) comes first."""
    import zlib
    infos = track_infos(game)
    rows, blobs, at = [], [], 0
    for eid in [4000] + [i for i in ids if i != 4000]:
        info = infos.get(eid) or infos.get(eid // 100 * 100)
        try:
            tpk = FlatTPK(load(game, f'TRACKS/TRACKMAP{eid}.BIN'))
        except ValueError:
            continue
        if info is None or eid in (r[0] for r in rows):
            continue
        tex = tpk.get(next(iter(tpk.records)))
        px = bytearray(tex.rgba())
        px[3::4] = bytes([255]) * (len(px) // 4)
        class T: pass
        t = T()
        t.w, t.h, t.fmt, t.rgba = tex.w, tex.h, tex.fmt, (lambda px=px: px)
        w, h, blob = texture_blob(t, 512)
        assert (w, h) == (512, 512), (eid, w, h)
        z = zlib.compress(blob, 9)
        rows.append((eid, *info, at, len(z)))
        blobs.append(z)
        at += len(z)
    head = struct.pack('<4s2I', b'N2MM', 1, len(rows))
    base = len(head) + 32 * len(rows)
    body = b''.join(struct.pack('<I5f2I', eid, ox, oy, sc, zm, ang, base + off, n) for eid, ox, oy, sc, zm, ang, off, n in rows)
    path.write_bytes(head + body + b''.join(blobs))
    return len(rows), base + at


# The original screens: file, package, bundle, the textures the game swaps
# in (carousel icons, the maps).
PAUSE_ICONS = ('INGAME_ICON_RESUME_RACE.tga', 'INGAME_ICON_RESTART_RAC.tga', 'INGAME_ICON_QUIT_RACE.tga',
               'INGAME_ICON_WORLD_MAP.tga', 'INGAME_ICON_QUIT_TO_CRI.tga', 'INGAME_ICON_OPTIONS.tga',
               'INGAME_ICON_EXPLORE.tga', 'INGAME_ICON_QUIT_TO_FE.tga', 'INGAME_ICON_REPLAY.tga',
               'SHOP_ICON_CRIB.tga')   # Replay; Jump to Garage's (PauseMenu::SetupExplore)
# The vinyl shop's groups, as its carousel numbers them (default.xbe's
# table at 0x142500, tools/convert_vinyls.py): Tear, Tear Set, Stripes...
VINYL_GROUP_ICONS = tuple(('VINYL_GROUP_' + g)[:23] for g in (
    'TEAR', 'TEAR_SET', 'STRIPES', 'STRIPES_SET', 'SPLASH', 'SPLASH_SET', 'MODERN', 'MODERN_SET', 'FLAMES',
    'FLAMES_SET', 'LIGHTNING', 'LIGHTNING_SET', 'RACING', 'RACING_SET', 'FLAG', 'FLAG_SET', 'TRIBAL', 'TRIBAL_SET',
    'WILD', 'WILD_SET', 'HOOD', 'AFTER_MARKET', 'MANUFACTURER', 'BODY', 'UNIQUE', 'TOP_LAYER', 'ART_FACTORY',
    'CONTEST'))
# The garage's icons: the customise categories, then the parts browser's
# option icons (rim brands, vinyl groups, paint and neon swatches tinted at
# run time).
CUSTOMIZE_ICONS = ('VISUAL_PART_PAINT', 'VISUAL_PART_VINYL', 'VINYL_COLOR', 'VISUAL_PART_BODY', 'VISUAL_PART_HOOD',
                   'VISUAL_PART_SPOILER', 'VISUAL_PART_RIMS', 'VISUAL_PART_HEAD_LIGHTS', 'VISUAL_PART_TAIL_LIGHTS',
                   'VISUAL_PART_NEON', 'VISUAL_PART_WINDOW_TINT', 'DECAL_ZONE_HOOD', 'DECAL_ZONE_WINDSHIELD',
                   'DECAL_ZONE_REARWINDOW', 'DECAL_ZONE_LEFTDOOR', 'DECAL_ZONE_RIGHTDOOR', 'DECAL_ZONE_LEFTPANEL',
                   'DECAL_ZONE_RIGHTPANEL',
                   # the Car Shop's own (default.xbe 0x11d690), the Graphics Shop's and the paint parts'
                   'VISUAL_PART_FRONT_BUMPE', 'VISUAL_PART_REAR_BUMPER', 'VISUAL_PART_SIDE_SKIRT', 'VISUAL_PART_ROOF_SCOOP',
                   'VISUAL_PART_MUFFLERTIP', 'VISUAL_PART_CUSTOM_HUDS', 'VISUAL_PART_SPINNERS', 'VISUAL_PART_WIDE_BODY_KIT', 'VISUAL_PART_SIDE_MIRRORS', 'VISUAL_PART_CARBON_FIBRE', 'VISUAL_PART_SCISSOR_DOORS', 'VISUAL_PART_TRUNK_AUDIO', 'NEON_ICON_CABIN', 'NEON_ICON_ENGINE', 'NEON_ICON_TRUNK', 'U2_OL_LOCKICON',
                   'SHOP_ICON_BODY', 'SHOP_ICON_PERFORMANCE', 'SHOP_ICON_GRAPHIC', 'SHOP_ICON_INTERIORS', 'CRIB_ICON_TEST_TRACK',
                   'VISUAL_PART_DECALS', 'PAINT_MOD_BASE', 'PAINT_MOD_PART_SPOILER', 'PAINT_MOD_PART_RIMS', 'PAINT_MOD_PART_BRAKE_CA')
PARTS_ICONS = CUSTOMIZE_ICONS + ('VISUAL_PART_FRONT_BUMPE', 'VINYL_REMOVE', 'NEON_COLOR_GENERIC', 'GENERIC_LOCK_ICON',
               'White16x16_pure_white', 'VISUAL_RIMS_BRAND_BBS', 'VISUAL_RIMS_BRAND_ENKEI', 'VISUAL_RIMS_BRAND_KONIG',
               'VISUAL_RIMS_BRAND_MOMO', 'VISUAL_RIMS_BRAND_OZ', 'VISUAL_RIMS_BRAND_RACIN', 'VISUAL_RIMS_BRAND_STOCK') + \
              ('VISUAL_PART_DECALS',) + VINYL_GROUP_ICONS + ('DECAL_ICONS', 'DECAL_SLOTS')   # every DECAL_ICON_*; the zones' DECAL_*_SLOT<n> and layouts
# The performance shop: its categories and the dyno.
PERF_ICONS = ('PERFORMANCE_ENGINE', 'PERFORMANCE_ECU', 'PERFORMANCE_TURBO', 'PERFORMANCE_TRANSMISSIO',
              'PERFORMANCE_SUSPENSION', 'PERFORMANCE_NITROUS', 'PERFORMANCE_TIRES', 'PERFORMANCE_BRAKES',
              'PERFORMANCE_WEIGHT_REDU', 'PERFORMANCE_DYNO_RUN')
SCREENS = [('pause.n2ui', 'UI_pause.fng', 'GLOBAL/INGAMECOMMON.LZC', PAUSE_ICONS),
           ('results.n2ui', 'UI_PostRaceResults.fng', 'GLOBAL/INGAMERACE.BUN', ()),
           ('reward.n2ui', 'UI_PostRaceReward.fng', 'GLOBAL/INGAMERACE.BUN', ()),
           ('stats.n2ui', 'UI_PostRaceStats.fng', 'GLOBAL/INGAMERACE.BUN', ()),   # Post Race Stats (Square on the results)
           ('tournres.n2ui', 'UI_PostRace_TournResults.fng', 'GLOBAL/INGAMERACE.BUN', ()),   # a tournament's course: its places and points
           ('tournstand.n2ui', 'UI_PostRace_TournStandings.fng', 'GLOBAL/INGAMERACE.BUN', ()),   # the tournament's standings after it
           ('sponsorpop.n2ui', 'UI_SponsorPopup.fng', 'GLOBAL/INGAMERACE.BUN', ()),   # "CHOOSE A SPONSOR!" after the race that ends a stage
           ('replay.n2ui', 'UI_ReplayControl.fng', 'GLOBAL/INGAMECOMMON.LZC', ()),   # the replay's control bar
           ('postrace.n2ui', 'UI_postrace.fng', 'GLOBAL/INGAMERACE.BUN',   # Post Race (the quick race's, after the results)
            ('INGAME_ICON_RESTART_RAC.tga', 'INGAME_ICON_STATS.tga', 'INGAME_ICON_REPLAY.tga', 'INGAME_ICON_QUIT_TO_FE.tga',
             'INGAME_ICON_RESUME_RACE.tga', 'INGAME_ICON_EXPLORE.tga')),   # the career's Next Race and Return to Explore Mode (PostRaceMenu::SetupRace)
           ('main.n2ui', 'UI_Main.fng', 'FRONTEND/FRONTB.LZC',
            ('MAIN_ICON_CAREER', 'MAIN_ICON_QUICK_RACE', 'MAIN_ICON_OPTIONS', 'MAIN_ICON_CUSTOMIZE_CAR')),
           ('qrmode.n2ui', 'UI_QRModeSelect.fng', 'FRONTEND/FRONTB.LZC',
            ('TRACK_ICON_CIRCUIT', 'TRACK_ICON_SPRINT', 'TRACK_ICON_SHORT_TRACK', 'TRACK_ICON_DRAG',
             'TRACK_ICON_DRIFT', 'TRACK_ICON_U_RACING_LEA', 'GENERIC_LOCK_ICON')),
           ('qrtrack.n2ui', 'UI_QRTrackSelect.fng', 'FRONTEND/FRONTB.LZC',
            ('TRACK_ICON_CIRCUIT', 'TRACK_ICON_SPRINT', 'TRACK_ICON_SHORT_TRACK', 'TRACK_ICON_DRAG',
             'TRACK_ICON_DRIFT', 'TRACK_ICON_U_RACING_LEA', 'EVENTMAP')),   # every mode's icon on its tracks
           ('qrcar.n2ui', 'UI_QRCarSelect.fng', 'FRONTEND/FRONTB.LZC', 'LOGOS'),
           ('qropts.n2ui', 'UI_QRTrackOptions.fng', 'FRONTEND/FRONTB.LZC', ()),   # Quick Race Game Options
           ('crib.n2ui', 'UI_CareerCrib.fng', 'FRONTEND/FRONTB.LZC',
            ('CRIB_ICON_RESUME_CAREER', 'CRIB_ICON_CAR_SELECT', 'CRIB_ICON_CHANGE_PARTS', 'CRIB_ICON_SWITCH_PERFOR',
             'CRIB_ICON_SAVE_LOAD', 'CRIB_ICON_MAIN_MENU')),
           ('carlot.n2ui', 'UI_CareerCarLot.fng', 'FRONTEND/FRONTB.LZC', 'LOGOS'),
           ('customize.n2ui', 'UI_ChooseCustomizeCategory.fng', 'FRONTEND/FRONTB.LZC', CUSTOMIZE_ICONS),
           ('perfcat.n2ui', 'UI_ChoosePerformanceCategory.fng', 'FRONTEND/FRONTB.LZC', PERF_ICONS),
           ('dynochart.n2ui', 'UI_PerformanceDyno_Chart.fng', 'GLOBAL/INGAMEPERFORMANCETUNING.BUN', ()),   # the dyno's chart
           ('dynoresults.n2ui', 'UI_PerformanceDyno_Results.fng', 'GLOBAL/INGAMEPERFORMANCETUNING.BUN', ()),   # its results: 0-60, 0-100, top speed, peaks
           ('perfpkg.n2ui', 'UI_ChoosePerformancePackage.fng', 'FRONTEND/FRONTB.LZC',
            ('GENERIC_STOCK', 'GENERIC_LEVEL1', 'GENERIC_LEVEL2', 'GENERIC_LEVEL3', 'GENERIC_LOCK_ICON') + PERF_ICONS),   # the category's own: its parts one by one
           ('parts.n2ui', 'UI_GenericParts_Browser.fng', 'FRONTEND/FRONTB.LZC', PARTS_ICONS),
           ('paint.n2ui', 'UI_Paint.fng', 'FRONTEND/FRONTB.LZC', ('PAINT_MOD_BASE', 'PAINT_MOD_PART_SPOILER', 'PAINT_MOD_PART_RIMS',
                                                               'PAINT_MOD_PART_BRAKE_CA', 'VINYL_COLOR', 'GENERIC_LOCK_ICON')),   # the paint shop's grid (a vinyl's colours too)
           ('hudpick.n2ui', 'UI_ChooseCustomHUD.fng', 'FRONTEND/FRONTB.LZC', ()),   # Custom Gauges: the tachometer and its colours
           ('vlayer.n2ui', 'UI_ChooseVinylLayer.fng', 'FRONTEND/FRONTB.LZC',   # the vinyl shop's four layers
            ('VINYL_LAYER_1', 'VINYL_LAYER_2', 'VINYL_LAYER_3', 'VINYL_LAYER_4', 'GENERIC_LOCK_ICON')),
            ('trax.n2ui', 'EA_TRAX_Jukebox.fng', 'FRONTEND/FRONTB.LZC', ()),   # the EA Trax jukebox (Options)
            ('fopts.n2ui', 'UI_Options.fng', 'FRONTEND/FRONTB.LZC', ()),   # an Options page (Audio), the pause's own in the menus
            ('credits.n2ui', 'Credits.fng', 'FRONTEND/FRONTB.LZC', ()),   # the credits' frame (Options > Credits)
           ('options.n2ui', 'UI_OptionsMain.fng', 'FRONTEND/FRONTB.LZC',
            ('OPTIONS_ICON_AUDIO', 'OPTIONS_ICON_DISPLAY', 'OPTIONS_ICON_GAMEPLAY_S', 'OPTIONS_ICON_PLAYER_SET',
             'OPTIONS_ICON_CONTROLS', 'OPTIONS_ICON_EA_TRAX', 'OPTIONS_ICON_CREDITS', 'GENERIC_ICON_SAVE')),
           ('worldmap.n2ui', 'WorldMapMain.fng', 'FRONTEND/FRONTB.LZC', ('MINIMAP_ICON_TICK',)),
           ('engage.n2ui', 'UI_EngageRaceDialog.fng', 'GLOBAL/INGAMECOMMON.LZC', ('EVENTMAP',)),
           ('showcase.n2ui', 'UI_ShowCase_Preview.fng', 'GLOBAL/INGAMECOMMON.LZC', ()),   # a photo shoot's session: the viewfinder and its options
           ('showdpad.n2ui', 'UI_ShowCase_DPAD.fng', 'GLOBAL/INGAMECOMMON.LZC', ()),   # at a photo shoot: Up for its session
           ('shopdlg.n2ui', 'UI_EngageShopDialog.fng', 'GLOBAL/INGAMECOMMON.LZC', ()),   # stopped at a shop: enter it?
           ('showgo.n2ui', 'UI_EngageEventDialog.fng', 'GLOBAL/INGAMECOMMON.LZC', ()),   # at a showcase's start: Rachel's text, the countdown's time
           ('msgdlg.n2ui', 'UI_EngageMessageDialog.fng', 'GLOBAL/INGAMECOMMON.LZC', ()),   # an engage tip's SMS on the way
           ('poptsm.n2ui', 'UI_PauseOptionsMain.fng', 'GLOBAL/INGAMECOMMON.LZC', ('OPTIONS_ICON_AUDIO', 'OPTIONS_ICON_GAMEPLAY_S', 'OPTIONS_ICON_PLAYER_SET')),   # the pause's Options
           ('popts.n2ui', 'UI_PauseOptions.fng', 'GLOBAL/INGAMECOMMON.LZC', ()),   # its Audio page
           ('statusm.n2ui', 'UI_Status_Master.fng', 'GLOBAL/GLOBALB.LZC', ()),   # Career Status: the frame...
           ('statusc.n2ui', 'UI_Status_Career.fng', 'GLOBAL/GLOBALB.LZC', ()),   # ...and its page (Down on the pad)
           ('statusr.n2ui', 'UI_Status_Region.fng', 'GLOBAL/GLOBALB.LZC', ()),   # the stage's contract, a page further
           ('statusd.n2ui', 'UI_Status_DVD.fng', 'GLOBAL/GLOBALB.LZC', ('MINIMAP_ICON_TICK',)),   # the stage's DVD covers
           # the street's world map (Left on the pad): the categories, the GPS
           ('igmap.n2ui', 'UI_InGame_WorldMap.fng', 'GLOBAL/GLOBALB.LZC',
            ('MINIMAP_ICON_TICK', 'MINIMAP_ICON_CIRCUIT', 'MINIMAP_ICON_SAFE_HOUSE', 'MINIMAP_GPS_SELECTOR')),
           ('sponsor.n2ui', 'UI_Sponsorship_new.fng', 'GLOBAL/INGAMERACE.BUN', ()),
           ('sms.n2ui', 'UI_SMS_Mailbox.fng', 'GLOBAL/INGAMECOMMON.LZC', ()),
           ('title.n2ui', 'UG_LS_Splash.fng', 'FRONTEND/FRONTB.LZC', ()),     # the title screen (its "Press START" text is the game's)
           ('loading.n2ui', 'Loading.fng', 'GLOBAL/GLOBALB.LZC', ()),        # LOADING over the title's art, its blocks lit in turn
           ('loadctl.n2ui', 'Loading_Controller.fng', 'GLOBAL/GLOBALB.LZC', ()),
           ('optctl.n2ui', 'UI_OptionsController.fng', 'FRONTEND/FRONTB.LZC', ()),
           ('keyboard.n2ui', 'UI_VirtualKeyboard.fng', 'FRONTEND/FRONTB.LZC', ()),   # the profile's name (Create User Name)
           ('gdialog.n2ui', 'GenericDialog_LARGE.fng', 'FRONTEND/FRONTB.LZC', ()),   # the profile's Yes / No questions
           ('helps.n2ui', 'HelpDialog_SMALL.fng', 'FRONTEND/FRONTB.LZC', ()),   # Circle's help, by its lines: up to 3, 5, more
           ('helpm.n2ui', 'HelpDialog_MED.fng', 'FRONTEND/FRONTB.LZC', ()),
           ('helpl.n2ui', 'HelpDialog_LARGE.fng', 'FRONTEND/FRONTB.LZC', ()),   # Options > Controls: the PSP's buttons in the pad's place   # a race's load: the controls (the port's own rows over the pad's place)
           ('chyron.n2ui', 'Chyron_FE.fng', 'FRONTEND/FRONTB.LZC', ()),      # the EA Trax song card
           ('chyron_ig.n2ui', 'Chyron_IG.fng', 'GLOBAL/INGAMECOMMON.LZC', ())]   # the same in the street


RACE_SCREENS = {'pause.n2ui', 'results.n2ui', 'reward.n2ui', 'postrace.n2ui', 'replay.n2ui', 'tournres.n2ui', 'tournstand.n2ui', 'sponsorpop.n2ui'}   # as in slice 1: nothing dropped
CITY_MAPS = {'worldmap.n2ui', 'igmap.n2ui'}   # the screens showing the city's map


def style_table(globalb, table, path):
    """The style bonuses (chunk 0x34a07 of GLOBAL\\GLOBALB: after an 8-byte
    head, 48-byte records, each u32 text label (the language table), u32 id
    (low byte; flags above), four parameters, four multipliers, then the
    reputation it gives and a mask, which close the record): "HEAD START"
    100, "CLEAN LAP" 500, "GAINED 1ST" 200... style.n2st: 'N2ST', u32
    count, count x (char text[24], f32 points, f32 param[4], u32 id)."""
    t, p, n = next(c for c in chunks(globalb) if c[0] == 0x34a07)
    out = []
    for o in range(p + 8, p + n - 47, 48):
        label, ident = struct.unpack_from('<2I', globalb, o)
        par = struct.unpack_from('<4f', globalb, o + 8)
        points = struct.unpack_from('<f', globalb, o + 40)[0]
        text = table.get(label)
        if text:
            out.append(struct.pack('<24sf4fI', text.encode('latin-1')[:23], points, *par, ident))
    path.write_bytes(struct.pack('<4sI', b'N2ST', len(out)) + b''.join(out))
    return len(out)


# The help (Circle, HelpDialog_SMALL/MED/LARGE): the game's table of its
# screens in default.xbe, 28-byte records (char *package, its handler,
# u32 the help's text label, flags, ...) from UI_Main.fng on; the text has
# the pad's tokens, one action a line.
HELP_FIRST = 0x393590   # the record of UI_Main.fng in the USA Xbox default.xbe (the table runs on both sides)


def help_first(exe):
    """The help table's UI_Main.fng record: of the words pointing at that
    name, the one with the most .fng packages named every 28 bytes round it
    (HELP_FIRST on the Xbox; the PS2's executable has it elsewhere)."""
    def fng(at):
        return 0 <= at <= len(exe.b) - 4 and exe.cstr(struct.unpack_from('<I', exe.b, at)[0]).lower().endswith('.fng')
    score, best = max((sum(fng(r + 28 * k) for k in range(-10, 11)), r) for r in exe.refs('UI_Main.fng'))
    if score >= 8:
        return best
    raise ValueError(f'{exe.path.name}: the help table (UI_Main.fng) not found')
# the PSP's icons for the tokens: the stick and the pad's (Left/Right: the
# pad's arrows, Up/Down its up-and-down); the right stick has no PSP twin
HELP_ICONS = {'$JOY_EVENT_FENG_PAD_LEFT$': 'PS2_DPADL', '$JOY_EVENT_FENG_PAD_UP$': 'PS2_DPADUPDOWN',
              '$JOY_EVENT_FENG_PAD_LEFT_ALTERNATE$': 'PS2_LEFTANALOG', '$JOY_EVENT_FENG_PAD_UP_ALTERNATE$': 'PS2_LEFTANALOG',
              '$JOY_EVENT_FENG_SELECT$': 'PS2_CROSS', '$JOY_EVENT_FENG_CANCEL$': 'PS2_TRIANGLE',
              '$JOY_EVENT_FENG_BUTTON1$': 'PS2_SQUARE', '$JOY_EVENT_FENG_BUTTON2$': 'PS2_TRIANGLE',
              '$JOY_EVENT_FENG_LTRIGGER$': 'PSP_L', '$JOY_EVENT_FENG_RTRIGGER$': 'PSP_R'}
# what the port's screens do not do: the right stick turning the car, and
# on the quick race's car the filter and the customizing
HELP_DROP = ('$JOY_EVENT_FENG_PAD_RIGHT_ALTERNATE$', 'Toggle Car Filter', 'Customize / Edit Car', 'Switch Players')
HELP_ONLY = {'optctl.n2ui': ('Change Selection', 'Back to Previous Menu')}   # the PSP's Controls: one row (Control Type), no rows to move through


def help_texts(game, table, path, language=None):
    """help.txt: a line a screen with help, its .n2ui, a tab, then its
    actions split by '|', the tokens as the menus' icon codes (ICON_CODE).
    language: another language's table (help_<xx>.txt), the lines the
    English keeps, by their place in the text."""
    import n2exe, n2source
    exe = n2exe.Exe(n2source.executable(game))
    x, cstr = exe.b, exe.cstr
    first = help_first(exe)
    helps = {}
    for p in range(max(0, first - 28 * 80), min(len(x) - 12, first + 28 * 200), 28):   # the table runs on both sides of UI_Main's
        name, _, label = struct.unpack_from('<3I', x, p)
        fng = cstr(name)
        if not fng.lower().endswith('.fng'):
            continue
        if label in table:
            helps[fng.lower()] = (table[label], (language or table).get(label, table[label]))
    lines = []
    for out, fng, _, _ in SCREENS:
        text, said = helps.get(fng.lower(), ('', ''))
        if not text:
            continue
        acts = []
        theirs = said.split('\n')
        if len(theirs) != len(text.split('\n')):
            theirs = text.split('\n')   # another shape: the English
        for k, line in enumerate(text.split('\n')):
            if not line.strip() or any(d in line for d in HELP_DROP) or (out in HELP_ONLY and not any(k in line for k in HELP_ONLY[out])):
                continue
            line = theirs[k]
            line = line.replace('$ICON_SPACER$', '')
            for token, icon in HELP_ICONS.items():
                line = line.replace(token, ICON_CODE[icon] + (ICON_CODE['PS2_DPADR'] if icon == 'PS2_DPADL' else ''))
            acts.append(re.sub(' +', ' ', line).strip(' '))   # not split(): it takes the icons' codes 0x1c-0x1f for spaces
        if acts:
            lines.append(out + '\t' + '|'.join(acts))
            HELP_FNGS.add(fng.lower())
    path.write_bytes(('\n'.join(lines) + '\n').encode('latin-1', 'replace'))
    return len(lines)


CREDITS_KEY = 0xaa787e56   # the English credits (UTF-16; CREDITS\\%s%s.TXT by language: FRENCH, GERMAN... beside it)


def credits_text(game, path):
    """The credits as the game rolls them (Options > Credits): credits.txt,
    a line each, Latin-1."""
    z = (game / 'ZDIR.BIN').read_bytes()
    e = next(e for e in struct.iter_unpack('<6I', z) if e[0] == CREDITS_KEY)
    with (game / f'ZZDATA{e[1]}.BIN').open('rb') as f:
        f.seek(e[2] * 2048)
        text = f.read(e[4]).decode('utf-16')
    lines = [l.rstrip() for l in text.splitlines()]
    path.write_bytes('\n'.join(lines).encode('latin-1', 'replace') + b'\n')
    return len(lines)


def gps_arrow(game, src, path):
    """The GPS's 3D arrow (GLOBAL\\INGAMECOMMON.LZC, the solid MARKER_GPS_AID:
    24-byte vertices, position, ARGB colour, UV): gps.n2gp, "N2GP", version
    2, submesh count, then each submesh's texture size (w, h), triangle
    vertex count, draw mode, the texture (CLUT8 as the screens' textures)
    and its triangles unrolled (u, v, ABGR, x, y, z: the GU's vertex order).
    Two submeshes: the arrow (ARROW_MARKERA, opaque) and its neon rim
    (SFX_NEONBARA, two bevelled bands above and below the outline), whose
    texture record asks for additive blending (usage 2, blend 2), as the
    game draws it (eSetBlendMode: source alpha + one, no depth write). The
    mode: 0 opaque, 1 alpha blended, 2 additive. Model axes: x across, y
    the plate's normal, the tip toward -z (Player::RenderArrow turns z
    from the target to the arrow, y up)."""
    from n2formats import chunks as ck, unpad
    d = load(game, 'GLOBAL/INGAMECOMMON.LZC')
    for tag, p, n in ck(d):
        if tag != 0x80134010:
            continue
        leaves = {t: d[o:o + s] for t, o, s in ck(d, p, p + n) if not t & 0x80000000}
        if unpad(leaves[0x134011])[164:].split(b'\0')[0] != b'MARKER_GPS_AID':
            continue
        keys = [a for a, _ in struct.iter_unpack('<II', leaves[0x134012])]
        if 0x134a02 in leaves:      # the PS2's VIF strips
            from n2formats import ps2_world
            verts, psubs = ps2_world(leaves)
            ids, draws = [], []
            for sub, ti in psubs:
                draws.append((len(sub), ti, len(ids)))
                ids += sub
        else:
            vb, ib, sb = (unpad(leaves[t]) for t in (0x134b01, 0x134b03, 0x134b02))
            verts = [struct.unpack_from('<3fI2f', vb, 24 * i) for i in range(len(vb) // 24)]
            ids = struct.unpack(f'<{len(ib) // 2}H', ib)
            draws = [(w[6], w[7], w[9]) for w in (struct.unpack_from('<16I', sb, at) for at in range(0, len(sb), 64))]
        out = bytearray(b'N2GP' + struct.pack('<II', 2, len(draws)))
        for count, ti, first in draws:
            tpk = next(t for t in src.flat if keys[ti] in t.records)
            blend = tpk.records[keys[ti]][1][0x32]   # the record's AlphaBlendType (convert_world.draw_mode)
            mode = 2 if blend == 2 else 1 if blend else 0   # eSetBlendMode: 2 SrcAlpha+One, 1 SrcAlpha+InvSrcAlpha
            tw, th, blob = texture_blob(tpk.get(keys[ti]), 128)
            out += struct.pack('<HHII', tw, th, count, mode) + blob
            for i in ids[first:first + count]:
                x, y, z, argb, u, v = verts[i]
                abgr = argb & 0xff00ff00 | (argb >> 16 & 0xff) | (argb & 0xff) << 16
                out += struct.pack('<2fI3f', u, v, abgr, x, y, z)
        path.write_bytes(out)
        return len(draws)
    raise SystemExit('MARKER_GPS_AID not found')


def event_records(path):
    """The events of races.n2rc (tools/convert_races.py), raw headers."""
    d = Path(path).read_bytes()
    count, at, out = struct.unpack_from('<I', d, 8)[0], 12, []
    for _ in range(count):
        npts, ngates = struct.unpack_from('<HH', d, at + 4)
        out.append(d[at:at + 12])
        at += 12 + 12 * npts + 2 * ngates
        at += (-at) % 4
    return out


def resample(rgba, w, x0, y0, cw, ch, W, H):
    """The crop (x0, y0, cw, ch) of an RGBA image w pixels wide, to W x H:
    each output pixel the mean of 3 x 3 bilinear samples."""
    h = len(rgba) // (4 * w)
    def at(x, y, c):
        x, y = min(max(x, 0), w - 1.001), min(max(y, 0), h - 1.001)
        i, j = int(x), int(y)
        fx, fy = x - i, y - j
        p = lambda a, b: rgba[4 * (b * w + a) + c]
        return (p(i, j) * (1 - fx) + p(i + 1, j) * fx) * (1 - fy) + (p(i, j + 1) * (1 - fx) + p(i + 1, j + 1) * fx) * fy
    out = bytearray(4 * W * H)
    for Y in range(H):
        for X in range(W):
            for c in range(4):
                acc = 0.0
                for sy in (1, 3, 5):
                    for sx in (1, 3, 5):
                        acc += at(x0 + (X + sx / 6) * cw / W - .5, y0 + (Y + sy / 6) * ch / H - .5, c)
                out[4 * (Y * W + X) + c] = int(acc / 9 + .5)
    return out


def eboot_art(src, out):
    """The XMB's art for the EBOOT (and the savedata's icon), from the
    game's loading screen: pic1.png 480 x 272, its splash (LS_SPLASH, 512 x
    256) scaled to cover the screen; icon0.png 144 x 80, the chrome logo
    (NFSU_LOGO_CHROME) over a darkened crop of the splash's street."""
    def tex(name):
        for t in src.flat:
            x = t.by_name(name)
            if x is not None:
                return x
        raise ValueError(f'{name} missing')
    splash, logo = tex('LS_SPLASH'), tex('NFSU_LOGO_CHROME')
    srgba, lrgba = splash.rgba(), logo.rgba()
    scale = 272 / splash.h
    pic = resample(srgba, splash.w, (splash.w - 480 / scale) / 2, 0, 480 / scale, splash.h, 480, 272)
    for i in range(3, len(pic), 4):
        pic[i] = 255
    write_png(out / 'pic1.png', 480, 272, pic)
    icon = resample(srgba, splash.w, 0, 74, 300, 167, 144, 80)
    mark = resample(lrgba, logo.w, 0, 0, logo.w, logo.h, 136, 68)
    for y in range(80):
        for x in range(144):
            k = 4 * (y * 144 + x)
            rgb = [v * .45 for v in icon[k:k + 3]]
            mx, my = x - 4, y - 6
            if 0 <= mx < 136 and 0 <= my < 68:
                m = 4 * (my * 136 + mx)
                a = mark[m + 3] / 255
                rgb = [mark[m + c] * a + rgb[c] * (1 - a) for c in range(3)]
            icon[k:k + 4] = bytes([int(v + .5) for v in rgb] + [255])
    write_png(out / 'icon0.png', 144, 80, icon)
    return 'icon0.png 144x80, pic1.png 480x272'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--preview', type=Path, help='render the INIT state to a PNG (640x480) and stop')
    ap.add_argument('--package', default='HUD_SingleRace.fng', help='--preview: any FEng package')
    ap.add_argument('--bundle', default='GLOBAL/INGAMERACE.BUN', help='--preview: the bundle holding it')
    ap.add_argument('--hud-only', action='store_true', help='write only hud.n2ui to an isolated output directory')
    args = ap.parse_args()
    pkg = hud_package(args.game, args.package, args.bundle)
    localise(pkg, strings(args.game))
    src = Sources(args.game)
    fnts = fonts(args.game, src)
    if args.preview:
        preview(pkg, src, fnts, args.preview, everything=args.package != 'HUD_SingleRace.fng')
        print('preview', args.preview)
        return
    table = strings(args.game)
    if args.hud_only:
        args.out.mkdir(parents=True, exist_ok=True)
        print('hud', write(pkg, src, fnts, args.out / 'hud.n2ui', map_texture(args.game),
                           extra=('ICON_RANDOM_ENCOUNTER', 'MINIMAP_ICON_CROSS', 'ENGAGE_PHONE_ICON') + STREET_ENGAGE_TEXTURES + SMS_INDICATOR_TEXTURES,
                           groups={**GROUPS, **FREE_GROUPS, **RACE_EXTRA}))
        return
    print('eboot', eboot_art(src, args.out))
    print('hud', write(pkg, src, fnts, args.out / 'hud.n2ui', map_texture(args.game), extra=('ICON_RANDOM_ENCOUNTER', 'MINIMAP_ICON_CROSS', 'ENGAGE_PHONE_ICON') + STREET_ENGAGE_TEXTURES + SMS_INDICATOR_TEXTURES, groups={**GROUPS, **FREE_GROUPS, **RACE_EXTRA}))
    drag = hud_package(args.game, 'HUD_Drag.fng', 'GLOBAL/INGAMEDRAG.BUN')
    localise(drag, strings(args.game))
    print('hud_drag', write(drag, src, fnts, args.out / 'hud_drag.n2ui', groups=DRAG_GROUPS, extra=DRAG_SCALES))   # the drag's own HUD
    drift = hud_package(args.game, 'HUD_Drift.fng', 'GLOBAL/INGAMEDRIFT.BUN')
    localise(drift, strings(args.game))
    print('hud_drift', write(drift, src, fnts, args.out / 'hud_drift.n2ui', map_texture(args.game), groups=DRIFT_GROUPS))
    print('gps.n2gp', gps_arrow(args.game, src, args.out / 'gps.n2gp'), 'submeshes')
    print('credits.txt', credits_text(args.game, args.out / 'credits.txt'), 'lines')
    print('help.txt', help_texts(args.game, table, args.out / 'help.txt'), 'screens')
    print('style.n2st', style_table(load(args.game, 'GLOBAL/GLOBALB.BUN'), table, args.out / 'style.n2st'), 'style bonuses')
    print('hudstyles.n2tx', hud_styles(args.game, src, args.out / 'hudstyles.n2tx'), 'textures')
    print('fx.n2tx', fx_textures(src, args.out / 'fx.n2tx'), 'textures')
    print('rain.n2tx', rain_textures(src, args.out / 'rain.n2tx'), 'textures')
    print('skids.n2tx', skid_textures(src, args.out / 'skids.n2tx'), 'textures')
    print('damage.n2tx', damage_textures(src, args.out / 'damage.n2tx'), 'textures')
    # every player car's logo (SECONDARY_LOGO_<MODEL>, TPK names cut at 23)
    globalb = load(args.game, 'GLOBAL/GLOBALB.BUN')
    recs = convert_physics.records(globalb)
    playable = convert_physics.playable(recs)
    logos = tuple(('SECONDARY_LOGO_' + m)[:23] for m in playable)
    # and its maker's emblem (CARSELECT_MANUFACTURER_<MAKER>, the maker at
    # record +0xC0), the car select's left box; the logo goes in the right one
    logos += tuple(sorted({'CARSELECT_MANUFACTURER_' + recs[m][0xC0:0xD0].split(b'\0')[0].decode().upper() for m in playable}))
    decal_icons = tuple(sorted({r[0] for t in src.flat for r in t.records.values() if r[0].startswith('DECAL_ICON_')}))
    decal_slots = tuple(sorted({r[0] for t in src.flat for r in t.records.values()
                                if r[0].startswith('DECAL_') and ('SLOT' in r[0] or 'LAYOU' in r[0])}))
    splash = title_art()
    for out, name, bundle, extra in SCREENS:
        extra = logos if extra == 'LOGOS' else extra
        extra = tuple(x for e in extra for x in (decal_icons if e == 'DECAL_ICONS' else decal_slots if e == 'DECAL_SLOTS' else (e,)))
        scr = hud_package(args.game, name, bundle)
        localise(scr, table)
        city = map_texture(args.game) if out in CITY_MAPS else None   # the world map's picture
        print(out, write(scr, src, fnts, args.out / out, citymap=city, screen=True, extra=extra,
                         drop=out not in RACE_SCREENS, splash=splash if out in {'title.n2ui', 'loading.n2ui'} else None))
    # the city's events and the closed tracks' (drag, drift, Street X, URL)
    ids = [struct.unpack_from('<H', d, 0)[0] for f in [args.out / 'races.n2rc', *sorted(args.out.glob('races-L4R?.n2rc'))]
           for d in event_records(f)]
    print('maps.n2tx', track_maps(args.game, ids, table, args.out / 'maps.n2tx'), 'track maps')
    print('minimaps.n2mm', minimaps(args.game, ids, args.out / 'minimaps.n2mm'), 'maps, bytes')
    for code, language in LANGUAGES.items():   # English: the screens' own texts
        if code == 'en':
            continue
        print(f'lang_{code}.n2lg', language_table(args.game, language, bool(src.buttons), args.out / f'lang_{code}.n2lg', table), 'texts')
        help_texts(args.game, table, args.out / f'help_{code}.txt', strings(args.game, language))


if __name__ == '__main__':
    main()
