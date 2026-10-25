#!/usr/bin/env python3
"""The street's event markers, as the game's: trigfx.n2te.

Out in free roam the original marks the places the career offers with
particle effects (GameCube Speed.elf: InitTriggerEffects,
UpdateTriggerEffects, TriggerEffectEmitters): one per kind, at the nearest
place of that kind within 200 m of the car. Each kind is an emitter group
of GLOBAL/GLOBALB's Acid effects (chunk 0x35021, the emitters' data, 0x220
bytes each; 0x35020, the emitters, 0xd0 bytes, their data's hash at +0x0c),
named in the game's table (TheTriggerEffectMap, 12 x {mode, zone type,
group}): XEE_EVENT_CIRCUIT, _DRAG, _STREET, _DRIFT, _SPRINT, _URL,
XEE_SHOWCASE, XEE_WORLD_CHALLENGE, XEE_EVENT_SUV, XEE_DVD (no zone has its
type: never shown), XEE_GAME_TIP and XEE_BANK. A group is an icon (the
event's mode icon, 128 x 64, a camera-facing sprite 3 m up), a ring on the
ground (XE_RINGWAVEA, a flat quad turning) and for some a burst of smoke.
The shops have none.

An emitter's data (offsets in the record; AcidEffectSystem::TriggerEmitter,
DoTimeStep, RenderEffects): +0x10 its group's hash; bytes +0x18 the
direction (0: the emitter's up turned at random within the cone, else
flat, at a random heading), +0x19 and +0x1a the texture's frames across and
down (as powers of two), +0x1b frames a second (0: none), +0x1c a random
first frame, +0x1d a random spin's sense, +0x1f the size's aspect
(ParticleAspectRatioAmounts: 1 x 1, 1 x 0.5, 0.5 x 1), +0x20 the
orientation (0 facing the camera, 1 flat, 2 and 3 upright); f32 +0x34
particles a second, +0x38 its spread (the share it can lose), +0x3c life
(s) and +0x40 its spread, +0x44 speed (m/s) and +0x48 its spread, +0x4c the
first angle's range (degrees), +0x50 the cone (degrees), +0x60 the offset
(game x, y, z) and +0x70 the random box around it, +0x80 the distance it
emits and draws within, +0x84 gravity (m/s^2 down), +0x88 the drag
(v -= drag v^2 dt), +0x100 the colour over the life (rows t^3, t^2, t, 1
of R, G, B, A, 0-255) and +0x140 the size (m) and turn (65536 a turn).
The texture's own blend: 2 additive, 1 alpha.

trigfx.n2te: 'N2TE', u32 version 1, u32 groups (12, the table's order),
u32 emitters, u32 textures; groups x (u8 first emitter, u8 count, u16 0);
emitters x (f32 rate, rate spread, life, life spread, speed, speed spread,
angle range, cone, offset[3] (GU: game y, z, x), box[3] (the same), gravity,
drag, distance, colour[16], shape[16]; u8 flat, frames across (log2),
down (log2), fps, random frame, random sense, aspect, orientation,
texture, blend (0 additive, 1 alpha), 2 x u8 0); textures x (u16 w, h,
u8 format (0: 4-bit alpha, white; 1: 4444), 3 x u8 0), then their pixels
in order (4-bit: the PSP's T4, low nibble first; 4444: the PSP's ABGR).
"""
import argparse
from pathlib import Path
import struct

from convert_hud import load
from n2formats import FlatTPK, chunks, game_hash

ROOT = Path(__file__).resolve().parents[1]
GROUPS = ('XEE_EVENT_CIRCUIT', 'XEE_EVENT_DRAG', 'XEE_EVENT_STREET', 'XEE_EVENT_DRIFT', 'XEE_EVENT_SPRINT', 'XEE_EVENT_URL',
          'XEE_SHOWCASE', 'XEE_WORLD_CHALLENGE', 'XEE_EVENT_SUV', 'XEE_DVD', 'XEE_GAME_TIP', 'XEE_BANK')
TEXTURE_SOURCES = ('GLOBAL/GLOBALB.LZC', 'GLOBAL/INGAMECOMMON.LZC')
SHEET = 64   # the 4444 textures' size (the smoke's 128 x 128 sheet halved)


def records(d, tag, size):
    for t, p, n in chunks(d):
        if t == tag:
            at = (p + 15) & ~15
            count, = struct.unpack_from('<I', d, at + 12)
            return [d[at + 16 + size * k:at + 16 + size * (k + 1)] for k in range(count)]
    raise SystemExit(f'GLOBALB: no chunk {tag:#x}')


def gu(v):
    """Game (x forward, y left, z up) to GU (x, y up, z): the game's y, z, x."""
    return v[1], v[2], v[0]


def texture(tpks, name):
    for tpk in tpks:
        key = game_hash(name)
        if key in tpk.records:
            return tpk.get(key), tpk.records[key][1]
    raise SystemExit(f'texture {name} missing')


def alpha4(tex):
    """White with the alpha's 16 levels (the DXT3's own): T4, low nibble first.
    The top and bottom rows clear: the port draws an icon as the PS2 does, a
    texture read as square, its rows past the edges the edges' own (two
    icons have a 1/15 there)."""
    px = tex.rgba()
    out = bytearray()
    for i in range(0, tex.w * tex.h, 2):
        a, b = px[4 * i + 3] * 15 // 255, px[4 * i + 7] * 15 // 255
        out.append(0 if i < tex.w or i >= tex.w * (tex.h - 1) else a | b << 4)
    return bytes(out)


def rgba4444(tex):
    """The texture at SHEET x SHEET (2 x 2 box when halved), the PSP's 4444."""
    px, f = tex.rgba(), tex.w // SHEET
    out = bytearray()
    for y in range(SHEET):
        for x in range(SHEET):
            acc = [0, 0, 0, 0]
            for dy in range(f):
                for dx in range(f):
                    o = 4 * ((y * f + dy) * tex.w + x * f + dx)
                    for c in range(4):
                        acc[c] += px[o + c]
            r, g, b, a = (min(15, (v // (f * f) + 8) // 17) for v in acc)
            out += struct.pack('<H', r | g << 4 | b << 8 | a << 12)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    a = ap.parse_args()
    globalb = load(a.game, 'GLOBAL/GLOBALB.LZC')
    tpks = [FlatTPK(globalb if n == 'GLOBAL/GLOBALB.LZC' else load(a.game, n)) for n in TEXTURE_SOURCES]
    data = records(globalb, 0x35021, 0x220)
    emitters = records(globalb, 0x35020, 0xd0)
    by_hash = {struct.unpack_from('<I', r, 8)[0]: r for r in data}
    textures, tex_index, groups, out = [], {}, [], bytearray()
    count = 0
    for g in GROUPS:
        h = game_hash(g)
        mine = [by_hash[struct.unpack_from('<I', e, 0x0c)[0]] for e in emitters
                if struct.unpack_from('<I', e, 0x0c)[0] in by_hash and struct.unpack_from('<I', by_hash[struct.unpack_from('<I', e, 0x0c)[0]], 0x10)[0] == h]
        if not mine:
            raise SystemExit(f'{g}: no emitters')
        groups.append((count, len(mine)))
        for r in mine:
            tname = r[0x1c0:0x1f0].split(b'\0')[0].decode('ascii')
            if tname not in tex_index:
                tex, info = texture(tpks, tname)
                blend = 0 if info[0x32] == 2 else 1
                icon = tex.w == 128 and tex.h == 64
                tex_index[tname] = (len(textures), blend)
                textures.append((tex.w, tex.h, 0, alpha4(tex)) if icon else (SHEET, SHEET, 1, rgba4444(tex)))
            ti, blend = tex_index[tname]
            f = lambda o: struct.unpack_from('<f', r, o)[0]
            b = lambda o: r[o]
            vals = [f(0x34), f(0x38), f(0x3c), f(0x40), f(0x44), f(0x48), f(0x4c), f(0x50),
                    *gu([f(0x60), f(0x64), f(0x68)]), *gu([f(0x70), f(0x74), f(0x78)]), f(0x84), f(0x88), f(0x80),
                    *struct.unpack_from('<16f', r, 0x100), *struct.unpack_from('<16f', r, 0x140)]
            out += struct.pack(f'<{len(vals)}f', *vals)
            out += bytes([b(0x18), b(0x19), b(0x1a), b(0x1b), b(0x1c), b(0x1d), b(0x1f), b(0x20), ti, blend, 0, 0])
            count += 1
    head = struct.pack('<4s4I', b'N2TE', 1, len(GROUPS), count, len(textures))
    head += b''.join(struct.pack('<BBH', first, n, 0) for first, n in groups)
    head += bytes(out)
    head += b''.join(struct.pack('<HHB3x', w, h, fmt) for w, h, fmt, _ in textures)
    head += b''.join(px for _, _, _, px in textures)
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / 'trigfx.n2te').write_bytes(head)
    print(f'trigfx.n2te: {len(GROUPS)} groups, {count} emitters, {len(textures)} textures, {len(head)} bytes')


if __name__ == '__main__':
    main()
