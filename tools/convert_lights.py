#!/usr/bin/env python3
"""The original's light flares -> flares.n2fl (one per world) and lamps.n2lm.
Python stdlib only.

The game draws a flare (a camera-facing quad of one or two layers, the
rules in psp-viewer/flares.c) for every light it knows of:

- the world's own: STREAM<track>.BUN chunk 0x80135100, one pack per
  scenery section (0x135101 header: u32 version 3 at +8, u16 count at
  +0x50, u32 section at +0x54; 0x135102 flares of 48 bytes: u32 name hash
  +8, colour +0xC, f32 x y z +0x10 (game axes), +0x1C the reflection's
  ground (999 unknown), f32 direction +0x20, u8 type +0x2C, u8 flags +0x2D).
  The city's 2,121 are all type 9 (the street lamps' warm white), flags 2
  (no fall-off with the angle) or 4 (fall-off, a lamp shining one way);
- the scenery models' light markers (0x13401A in an object, 80 bytes: u32
  hash, u32 kind +4, the matrix +0x10, its place +0x40), drawn wherever the
  game draws the model (ScenerySectionHeader::DrawAScenery): type 10 + the
  kind's low byte; kind 3, a spot (traffic lights, median poles), blinks
  (type 13) and faces out from the instance's origin, level; kind 0/1
  (cat's eyes, guard rail reflectors: amber, red) only shine in the
  player's headlights. Its place is the marker's less the model's pivot
  (the header matrix's translation, +0x70), through the instance's matrix;
- each car's from the markers of its head and tail light parts
  (CarRenderInfo::CreateCarLightFlares): HEADLIGHT_LEFT/RIGHT,
  BRAKELIGHT_LEFT/RIGHT and CENTRE_BRAKELIGHT, the light's place and
  facing (the marker's third row).

flares.n2fl (the city's, L4RA) and flares-L4RB.n2fl... (the tracks'),
little endian: 'N2FL', u32 version 1, u32 cells, u32 flares; cells x 20
bytes (the 64 m cell's GU x / 64 and z / 64 as s16, u16 count, u16 0, u32
first flare, f32 lowest and highest GU y of its flares); flares x 10
bytes: s16 x, y, z in 1/16 m (x and z from the cell's corner), s8
direction x y z (x127, GU), u8 type | flags << 4 (flags: 1 the
cosine's fall-off, 2 both ways). The scenery instances are those
convert_city bakes (the SKIP names and the event barriers' groups out).

lamps.n2lm: 'N2LM', u32 version 1, u32 atlas width 64, height 128, u32
entries, u32 cookie width 64, height 32, 4 bytes 0; the atlas, GU_PSM_4444
(HEADLIGHTFLAREINNER 64x32 at row 0, HEADLIGHTFLAREOUTER 64x32 at 32,
HEADLIGHTGLOW 64x64 at 64, the GLOBALB textures the game's flare table
names); the headlights' cookie, 4444 (GLOBALB's HEADLIGHTS: the Xbox's
HEADLIGHTS2, the pattern the player's headlights throw on the street);
entries: char name[24],
u8 lamps, u8 flags (1: the centre lamp goes with a custom spoiler,
RemoveCentreBrakeWithCustomSpoiler's car types), u16 0, lamps x 16 bytes
(u8 kind 0 head left, 1 head right, 2 tail left, 3 tail right, 4 centre;
u8 0; s16 x, y, z in mm, car space GU; s8 direction x y z x127; 5
bytes 0). Names: the car's model (stock, KIT00), MODEL/H/STYLEnn and
MODEL/B/STYLEnn (the garage's head and tail lights), traffic-MODEL,
opp-NAME (the presets/opponents and presets/rivals), rachel
(presets/350z-showroom.json).
"""
import argparse
import collections
import json
import math
from pathlib import Path
import re
import struct

import convert_car as cc
import convert_physics
import convert_traffic
import convert_world as cw
import n2world as nw
from n2formats import FlatTPK, archived, chunks, lod_family, parts, unpad
import n2source

ROOT = Path(__file__).resolve().parents[1]
TRACKS = ('L4RA', 'L4RB', 'L4RC', 'L4RD', 'L4RF', 'L4RG')
CELL = 64.0
UNIT = 16.0       # 1/16 m

HEAD_L, HEAD_R, TAIL_L, TAIL_R, CENTRE = 0x9db90133, 0xd09091c6, 0x31a66786, 0xbf700a79, 0xa2a2fc7c
LAMP_KIND = {HEAD_L: 0, HEAD_R: 1, TAIL_L: 2, TAIL_R: 3, CENTRE: 4}
# RemoveCentreBrakeWithCustomSpoiler: the car types (GLOBALB record +0x840)
# whose centre lamp is on the stock trunk, gone under a shop spoiler
CENTRE_OFF_TYPES = {5, 11, 12, 24, 25, 30}
ATLAS = ((0x8868656f, 0, 32), (0x88d8e8a2, 32, 32), (0x3394fe62, 64, 64))   # key, row, height


def gu(x, y, z):
    """Game axes (X forward/east, Y, Z up) -> GU (x = Y, y = Z, z = X)."""
    return y, z, x


def world_flares(stream):
    """The packs' flares: [(GU pos, GU dir, type, flags)]."""
    out = []
    for name in stream.sections:
        d = stream.section(name)
        for t, p, n in chunks(d):
            if t != 0x80135100:
                continue
            count = 0
            for t2, p2, n2 in chunks(d, p, p + n):
                a = (p2 + 15) & ~15
                if t2 == 0x135101:
                    version, = struct.unpack_from('<I', d, a + 8)
                    count = struct.unpack_from('<H', d, a + 0x50)[0] if version == 3 else 0
                elif t2 == 0x135102:
                    if count * 48 > p2 + n2 - a:
                        raise ValueError(f'{name}: flare pack overrun')
                    for k in range(count):
                        r = a + k * 48
                        x, y, z = struct.unpack_from('<3f', d, r + 0x10)
                        dx, dy, dz = struct.unpack_from('<3f', d, r + 0x20)
                        kind, flags = d[r + 0x2c], d[r + 0x2d]
                        out.append((gu(x, y, z), gu(dx, dy, dz), kind, flags))
        stream.cache.clear()
    return out


def object_markers(stream):
    """key -> (pivot, [(kind, place)]) of the scenery models with light markers."""
    out = {}
    for name in stream.sections:
        d = stream.section(name)
        for tag, p, n in chunks(d):
            if tag != 0x80134010:
                continue
            leaves = {t: d[o:o+s] for t, o, s in chunks(d, p, p + n) if not t & 0x80000000}
            if 0x13401a not in leaves:
                continue
            h = unpad(leaves[0x134011])
            key, = struct.unpack_from('<I', h, 0x10)
            pivot = struct.unpack_from('<3f', h, 0x70)
            m = unpad(leaves[0x13401a])
            marks = []
            for at in range(0, len(m) - 79, 80):
                kind, = struct.unpack_from('<I', m, at + 4)
                marks.append((kind, struct.unpack_from('<3f', m, at + 0x40)))
            out.setdefault(key, (pivot, marks))
        stream.cache.clear()
    return out


def scenery_flares(stream):
    """The markers of every instance convert_city bakes, placed in the world."""
    marks = object_markers(stream)
    groups = {k: v for k, v in nw.scenery_groups(stream.track).items() if 'BARRIERS_' in k or k.startswith('FREE_ROAM')}
    grouped = set().union(*groups.values()) if groups else set()
    out = []
    for name in stream.sections:
        for inst in nw.section_instances(stream.section(name)):
            if inst.keys[0] not in marks or inst.name.upper().startswith(cw.SKIP) or (inst.section, inst.index) in grouped:
                continue
            pivot, ms = marks[inst.keys[0]]
            place = nw.place(inst)
            for kind, at in ms:
                x, y, z = place(at[0] - pivot[0], at[1] - pivot[1], at[2] - pivot[2])
                low = kind & 0xff
                if kind == 3:   # a spot: faces out from the instance's origin, level
                    dx, dy = x - inst.pos[0], y - inst.pos[1]
                    l = math.hypot(dx, dy) or 1.0
                    out.append((gu(x, y, z), gu(dx / l, dy / l, 0.0), 10 + low, 4))
                else:
                    out.append((gu(x, y, z), (0.0, -1.0, 0.0), 10 + low, 2))
        stream.cache.clear()
    return out


def pack_flares(flares):
    cells = collections.defaultdict(list)
    for f in flares:
        cells[(math.floor(f[0][0] / CELL), math.floor(f[0][2] / CELL))].append(f)
    head, body, first = bytearray(), bytearray(), 0
    for (ci, cj), fs in sorted(cells.items()):
        ys = [f[0][1] for f in fs]
        head += struct.pack('<hhHHIff', ci, cj, len(fs), 0, first, min(ys), max(ys))
        for (x, y, z), d, kind, flags in fs:
            q = [round((x - ci * CELL) * UNIT), round(y * UNIT), round((z - cj * CELL) * UNIT)]
            if not all(-32768 <= v < 32768 for v in q):
                raise ValueError('flare out of range')
            fl = (1 if flags & 4 else 0) | (2 if flags & 1 else 0)
            body += struct.pack('<3h3bB', *q, *(max(-127, min(127, round(v * 127))) for v in d), (kind & 15) | fl << 4)
        first += len(fs)
    return struct.pack('<4sIII', b'N2FL', 1, len(cells), first) + bytes(head) + bytes(body), len(cells)


# ------------------------------------------------------------------- cars

def lamps_of(chosen):
    """The light markers of these parts: [(kind, GU pos, GU dir)], one a
    kind and a side of the car (the first part's, its last marker of that
    name). CreateCarLightFlares makes a flare for every marker; a kind
    whose markers sit on both sides is the SUV's, its two tail lamps both
    named BRAKELIGHT_RIGHT (one tail light lit, beta issue 39). The
    inner and outer lamps some models carry on one side under one name
    (the SUV's headlamps, the sedans', the 350Z's tail lamps) stay one
    flare, as before: up to LAMPS_MAX (6) a car in the viewer."""
    out = {}
    for part in chosen:
        mine = {}
        for key, m in part.markers:
            if key in LAMP_KIND:
                p = gu(*m[12:15])
                mine[LAMP_KIND[key], (p[0] > .05) - (p[0] < -.05)] = (p, gu(*m[8:11]))
        for k, v in mine.items():
            out.setdefault(k, v)
    return sorted((k, p, d) for (k, side), (p, d) in out.items())


def car_lamps(game, car, preset):
    all_parts = list(parts(archived(game, f'CARS/{car}/GEOMETRY.BIN'), lambda n: n.startswith(car + '_')))
    families = cc.choose_car_parts(all_parts, car, dict(preset, car=car))
    return lamps_of([cc.pick_lod(c, 0) for c in families.values()])


def option_lamps(all_parts, car, style, what):
    head = f'{car}_{style}_{what}'
    chosen = {}
    for p in all_parts:
        if p.name.startswith(head):
            chosen.setdefault(lod_family(p.name), []).append(p)
    return lamps_of([cc.pick_lod(c, 0) for c in chosen.values()])


def entry(name, lamps, flags=0):
    if len(name) > 23:
        raise ValueError('name too long: ' + name)
    out = struct.pack('<24sBBH', name.encode(), len(lamps), flags, 0)
    for kind, p, d in lamps:
        q = [round(v * 1000) for v in p]
        out += struct.pack('<BB3h3b5x', kind, 0, *q, *(max(-127, min(127, round(v * 127))) for v in d))
    return out


def to4444(rgba, out, at):
    for i in range(len(rgba) // 4):
        r, g, b, a = ((rgba[i*4+c] * 15 + 127) // 255 for c in range(4))
        struct.pack_into('<H', out, at + i * 2, r | g << 4 | b << 8 | a << 12)


def atlas(game):
    tpk = FlatTPK(archived(game, 'GLOBAL/GLOBALB.BUN'))
    out = bytearray(64 * 128 * 2)
    for key, row, height in ATLAS:
        t = tpk.get(key)
        rgba, w, h = cc.box_downscale(t.rgba(), t.w, t.h, 64)
        if (w, h) != (64, height):
            raise ValueError(f'flare texture {key:08x}: {w}x{h}')
        to4444(rgba, out, row * 64 * 2)
    t = tpk.by_name('HEADLIGHTS') or tpk.by_name('HEADLIGHTSPSX')   # the PS2's cookie has its own name
    rgba, w, h = cc.box_downscale(t.rgba(), t.w, t.h, 64)
    if (w, h) != (64, 32):
        raise ValueError(f'HEADLIGHTS: {w}x{h}')
    cookie = bytearray(64 * 32 * 2)
    to4444(rgba, cookie, 0)
    return bytes(out) + bytes(cookie)


def build_lamps(game):
    globalb = archived(game, 'GLOBAL/GLOBALB.BUN')
    recs = convert_physics.records(globalb)
    body, n, report = bytearray(), 0, {}
    for car in convert_physics.playable(recs):
        kind, = struct.unpack_from('<I', recs[car], 0x840)
        flags = 1 if kind in CENTRE_OFF_TYPES else 0
        stock = car_lamps(game, car, {})
        body += entry(car, stock, flags)
        n += 1
        report[car] = len(stock)
        all_parts = list(parts(archived(game, f'CARS/{car}/GEOMETRY.BIN'), lambda nm: nm.startswith(car + '_')))
        for what, tag in (('HEADLIGHT', 'H'), ('BRAKELIGHT', 'B')):
            styles = sorted({m.group(1) for p in all_parts for m in [re.match(re.escape(car) + rf'_(STYLE\d+)_{what}', p.name)] if m})
            for style in styles:
                lamps = option_lamps(all_parts, car, style, what)
                if lamps:
                    body += entry(f'{car}/{tag}/{style}', lamps)
                    n += 1
    for model in convert_traffic.MODELS:
        ps = [p for p in parts(archived(game, f'CARS/{model}/GEOMETRY.BIN')) if p.name.endswith('_A')]
        lamps = lamps_of(ps)
        body += entry(f'traffic-{model}', lamps)
        n += 1
        report[f'traffic-{model}'] = len(lamps)
    presets = [(f'opp-{p.stem}', p) for p in sorted((ROOT / 'presets/opponents').glob('*.json'))]
    presets += [(f'opp-{p.stem}', p) for p in sorted((ROOT / 'presets/rivals').glob('*.json'))]   # the career's preset rivals (Caleb and his crew)
    presets.append(('rachel', ROOT / 'presets/350z-showroom.json'))
    for name, path in presets:
        preset = json.loads(path.read_text())
        kind, = struct.unpack_from('<I', recs[preset['car']], 0x840)
        lamps = car_lamps(game, preset['car'], preset)
        body += entry(name, lamps, 1 if kind in CENTRE_OFF_TYPES else 0)
        n += 1
        report[name] = len(lamps)
    blob = struct.pack('<4sIIIIII4x', b'N2LM', 1, 64, 128, n, 64, 32) + atlas(game) + bytes(body)
    return blob, n, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--track', action='append', help='only these worlds (default: all)')
    ap.add_argument('--no-cars', action='store_true', help='the worlds only')
    a = ap.parse_args()
    def write(name, blob):
        target = a.out / name
        if target.is_symlink():
            target.unlink()
        target.write_bytes(blob)
    for track in a.track or TRACKS:
        stream = nw.Stream(a.game, track)
        world = world_flares(stream)
        scenery = scenery_flares(stream)
        blob, cells = pack_flares(world + scenery)
        name = 'flares.n2fl' if track == 'L4RA' else f'flares-{track}.n2fl'
        write(name, blob)
        kinds = collections.Counter(f[2] for f in world + scenery)
        print(f'{name}: {len(blob)} bytes, {cells} cells, {len(world)} world flares, {len(scenery)} markers, types {dict(sorted(kinds.items()))}', flush=True)
    if not a.no_cars:
        blob, n, report = build_lamps(a.game)
        write('lamps.n2lm', blob)
        short = {k: v for k, v in report.items() if v < 4}
        print(f'lamps.n2lm: {len(blob)} bytes, {n} entries; fewer than 4 lamps: {short or "none"}')


if __name__ == '__main__':
    main()
