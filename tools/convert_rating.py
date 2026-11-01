#!/usr/bin/env python3
"""The visual rating's points (the stars), as the game counts them: rating.n2vr.

The Xbox default.xbe adds up, over the car's 170 slots (the slot names at
0x3ccd18: BASE, FRONT_BUMPER ... MISC), the points of each part fitted
(0x130300; past the vinyls' colours, 0x4e-0x5c, and the rear wheel; with a
wide body kit not its bumpers and skirts), and makes stars of them, 500 a
star, at most 10 (0x3a379c). A part's points (0x120b30): its kind by the
slot (0x112a10; the table at 0x3fb580 filled by 0x1125c0, the paint by its
brand, a vinyl by its group, a carbon piece its own) and its level (the
parts table's level byte >> 5): the kind's row of GLOBALB chunk 0x34A1C
(86 rows of 40 bytes: per level 1-3 a u16 of points and one of price, -1
none), level 4 (the unique parts) a constant (0x112500: 400, the wide body
kit 1000). KIND below is the slot's kind; POINTS the rows, as read.

The port's choices map onto the game's slots: the parts the garage lists
per car (garage.json, tools/convert_garage.py) by their part's own level
(the parts table, by name: 350Z_KIT01_FRONT_BUMPER, SPOILER_STYLE02...),
the rims (rims.n2rm's order), the vinyls (tools/convert_vinyls.py's
catalogue) and the neons (the parts table's type 54) likewise. Where the
parts table has no name the port can match (the side mirrors, the window
tints, the colours of the parts painted), the level is the style's tier:
noted per row.

Each level of a kind's row also says when the shops have it (Xbox
default.xbe 0x121bd0, the row's level block: +4 the condition's type, +8
its value): 0, 5 and 6 from the start; 1 a shop open (0x11f940: its stage
reached and, a hidden one, found; the value its name's hash, GLOBALB
0x34A12's +0x38); 2, 3 and 4 a count of the stage's (value >> 16) reached
(value & 0xffff): its races won (0x1110d0, 35 slots of the stage's
record), its URL races won (0x111110, 10) and its sponsor's races won
(0x111190, 4: the contract's 3); level 4 the unique parts' showcase.

rating.n2vr: 'N2VR', u32 version 7; every entry four u16, the part's
points, its price (0 none in the table: the port's own price stays), its
level (4 a unique part: opened by its showcase, free) and when it opens
(type << 12 | stage << 8 | count, a shop's: 1 << 12 | its index in
career.n2cr's shops; 0 from the start);
  u32 cars, cars x (char model[16], u8 count per category (CATS), then the
    categories' entries in garage.json's order);
  then lists, each u32 count and its entries: the rims (rims.n2rm's order; a
    spinner its wheel and its spinning part), the vinyls (the catalogue),
    the neons (3 entries each: under the car, the engine's, the trunk's), the
    body's paints (paints.n2pt's 150), the spoiler's paints (the same 150),
    the rims' and the calipers' palettes (60 each), and the constants: a
    decal zone's layout, a decal, the window tints 1-3, the carbon hood,
    trunk, doors and kit; then the performance shop's packages (psp-viewer
    perf.h's 9 categories x levels 1-3: their opening alone; the category's
    kind by 0x112ea0);
  then (version 6) u32 presets, each char name[32] and u32 its points
    (preset_points: Rachel's 350Z, DDAY_PLAYER_CAR);
  then (version 7) the other paint shop parts' lists, as the ones above:
    the roof scoop's and the mirrors' paints (the body's 150), the
    spinners' (the rims' palette), the engine accents' (HOSES) and the
    muffler tips' (MUFFLERS), 60 each.
"""
import argparse
import json
from pathlib import Path
import struct

import convert_garage
import convert_perf
import convert_vinyls
import n2feng as fe
import partsdb
from n2formats import archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
CATS = convert_garage.CATS
# the slot's kind (0x3fb580 as 0x1125c0 fills it, and 0x112a10's own cases)
KIND = dict(kit=18, rear_bumper=17, skirt=16, hood=11, spoiler=12, rims=13, headlight=15, brakelight=14,
            roof=20, exhaust=21, wide=22, mirror=19, audio=30, spinner=33, neon=25, neon_engine=26, neon_trunk=28,
            decal=76, tint=32, kit_carbon=85, trunk_carbon=86, paint_spoiler=80, paint_rim=77, paint_brake=79,
            paint_engine=40, paint_muffler=78, paint_spinner=81, paint_roof=83, paint_mirror=84)   # the paint slots 0x44-0x4c
HOOD_GROUP5 = 34                     # a hood of group 5 (0x112bbe)
PAINT_KIND = {'GLOSS': 37, 'METAL': 38, 'PEARL': 39}   # the base paint by its BRAND_NAME (0x112be2)
# a vinyl's kind by its group (0x112a43, its table at 0x112ce8)
VINYL_KIND = (41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 56, 57, 58, 59, 65, 66, 67, 60, 61, 64, 62, 55, 63, 68)
PERF_KIND = (6, 5, 4, 1, 9, 3, 8, 2, 7, 10)   # 0x34A15's category: its kind (0x112ea0)
UNIQUE = {11: 400, 12: 400, 13: 400, 34: 400, 74: 400, 75: 400, 22: 1000, 62: 100}   # level 4 (0x112500)
# A preset car's points (Rachel's 350Z, which the garage never builds): the
# game sums, over the car's 170 slots, each fitted part's points by the
# slot's kind and the part's level (GameCube StarGazerGuide::GetNumberOfStars,
# PlayerCareerState::GetCarPartRep). The slot's kind, as the game's table
# holds it once filled (InitUnlockablesMap), 0 none; the base paint (63) by
# its brand, a vinyl layer (64-67) by its group (VINYL_KIND), a carbon
# hood, roof, spoiler or mirror (the part's CARBONFIBRE) by SLOT_CARBON, a
# hood of group 5 the HOOD_GROUP5 kind, a spinner wheel its own. Skipped:
# the vinyl colours (78-92), the rear wheel (30) and, under a wide body kit
# of level > 0, the bumpers and the skirt (1, 2, 11).
SLOT_UNLOCK = ([0, 18, 17, 19, 19, 0, 22, 20, 0, 11, 0, 16, 12, 0, 15, 14, 21] + [0] * 12 + [13, 13, 33, 19, 0, 30] + [31] * 12
               + [0] * 5 + [76] * 7 + [0] * 9 + [40, 80, 79, 78, 82, 77, 81, 83, 84] + [0] * 16 + [85, 0, 0, 86] + [76] * 56
               + [32, 25, 26, 27, 28, 27, 24, 35, 36, 29, 23, 23, 23, 23, 0, 0, 0])
assert len(SLOT_UNLOCK) == 170
SLOT_CARBON = {9: 75, 7: 70, 12: 74, 32: 69}
CARBONFIBRE, SPINNER_SLOT, SPINNER_MODEL = 0x721aff7c, 29, 0xc8d1a75e
PRESETS = ('DDAY_PLAYER_CAR',)       # rating.n2vr's presets: Rachel's 350Z (the prologue's car)


def preset_points(game, rows, name):
    """The points of a preset car (GLOBALB 0x30220, tools/preset_dump.py)
    the game's way; None without it."""
    import preset_dump as pd
    table = pd.PartsTable(game)
    raw = table.block[0x34604]
    flags = {}
    for o in range(14, len(raw) - 13, 14):
        key, kind, fl = struct.unpack_from('<IBB', raw, o)
        flags.setdefault((key, kind), fl)
    pre = next((p for p in pd.presets(table) if p['name'] == name), None)
    if pre is None:
        return None

    def part(slot):
        v = pre['slots'][slot]
        return None if v in (0, 1) else table.find(slot, v, pre['car'])   # (1: the stock part, level 0, no points)
    wide = part(6)
    wide_on = wide is not None and wide['level'] > 0
    total = 0
    for slot in range(170):
        if 78 <= slot <= 92 or slot == 30 or (wide_on and slot in (1, 2, 11)):
            continue
        q = part(slot)
        if q is None:
            continue
        fl = flags.get((q['key'], q['kind']), q['level'] << 5)
        level, group = fl >> 5, fl & 31
        attrs = dict(table.attributes(q))
        if slot == 63:
            kind = PAINT_KIND.get(table.name(attrs.get(fe.bin_hash('BRAND_NAME'), 0)), PAINT_KIND['METAL'])
        elif 63 < slot <= 67:
            kind = VINYL_KIND[group] if group < len(VINYL_KIND) else 0
        elif attrs.get(CARBONFIBRE, 0):
            kind = SLOT_CARBON.get(slot, 0)
        elif slot == SPINNER_SLOT and attrs.get(0xebb03e66, 0) == SPINNER_MODEL:
            kind = KIND['spinner']
        elif slot == 9 and group == 5:
            kind = HOOD_GROUP5
        else:
            kind = SLOT_UNLOCK[slot]
        total += rows.points(kind, level) if kind else 0
    return total


class Rows:
    """GLOBALB chunk 0x34A1C: per kind and level 1-3 its points, price and
    when it opens (shops: the shops' names in career.n2cr's order)."""
    def __init__(self, game, shops=()):
        g = archived(game, 'GLOBAL/GLOBALB.BUN')
        s = [g[p:p + n] for t, p, n in chunks(g) if t == 0x34A1C][0]
        self.rows = {k + 1: [struct.unpack_from('<hh', s, 40 * k + o) for o in (4, 16, 28)] for k in range(len(s) // 40)}
        shop = {fe.bin_hash(n): i for i, n in enumerate(shops)}
        self.opens = {}
        for k in range(len(s) // 40):
            for lv, o in enumerate((4, 16, 28), 1):
                typ, val = s[40 * k + o + 4], struct.unpack_from('<I', s, 40 * k + o + 8)[0]
                if typ == 1:
                    c = 1 << 12 | shop[val] if val in shop else 0
                elif typ in (2, 3, 4):
                    c = typ << 12 | min(val >> 16, 15) << 8 | min(val & 0xffff, 255)
                else:
                    c = 0
                self.opens[k + 1, lv] = c

    def points(self, kind, level):
        if level == 4:
            return UNIQUE.get(kind, 0)
        if not 1 <= level <= 3 or kind not in self.rows:
            return 0
        return max(self.rows[kind][level - 1][0], 0)

    def price(self, kind, level):
        """0x120ac0: the level's price (a unique part, level 4, none; a decal at level 1 when it has none)."""
        if level == 0 and kind == 76:
            level = 1
        if not 1 <= level <= 3 or kind not in self.rows:
            return 0
        return max(self.rows[kind][level - 1][1], 0)

    def pair(self, kind, level):
        """(points, price, level, when it opens): the level too, a unique part's (4) opening with its showcase"""
        return (self.points(kind, level), self.price(kind, level), level, self.opens.get((kind, level), 0))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    a = ap.parse_args()
    world = json.loads((a.out / 'career-world.json').read_text())
    rows, parts = Rows(a.game, [v['name'] for v in world['shops']]), partsdb.Parts(a.game)
    by_key = {q['key']: q for q in parts.parts}

    def part(name):
        return by_key.get(fe.bin_hash(name))

    def level(name):
        q = part(name)
        return q['level'] >> 5 if q else 0

    def pair(kind, name):
        return rows.pair(kind, level(name))

    def pack(pairs):
        return struct.pack(f'<I{4 * len(pairs)}H', len(pairs), *[v for p in pairs for v in p])

    garage = json.loads((a.out / 'garage.json').read_text())
    report, cars = {}, b''
    for model, opts in garage.items():
        per = {}
        for cat in CATS:
            names = opts.get(cat, [])
            if cat in ('kit', 'rear_bumper', 'skirt'):
                slot = {'kit': 'FRONT_BUMPER', 'rear_bumper': 'REAR_BUMPER', 'skirt': 'SKIRT'}[cat]
                v = [pair(KIND[cat], f'{model}_{n}_{slot}') for n in names]
            elif cat == 'hood':
                v = []
                for n in names:
                    q = part(f'{model}_{n}_HOOD')
                    kind = HOOD_GROUP5 if q and q['level'] & 31 == 5 else KIND['hood']
                    v.append(rows.pair(kind, q['level'] >> 5 if q else 0))
            elif cat in ('headlight', 'brakelight'):
                v = [pair(KIND[cat], f'{model}_{n}_{cat.upper()}') for n in names]
            elif cat in ('spoiler', 'roof', 'exhaust', 'rims'):
                v = [pair(KIND[cat], n) for n in names]
            elif cat == 'wide':
                v = [pair(KIND['wide'], f'{model}_{n}_BODY') for n in names]
            elif cat == 'audio':
                v = [pair(KIND['audio'], f'{model}_{n}_TRUNK_AUDIO') for n in names]
            elif cat == 'mirror':
                # the parts table names no mirror the port can match: the
                # level by the style's tier (15 styles: 5 a level), both sides
                v = []
                for n in names:
                    pt, pr, lv, op = rows.pair(KIND['mirror'], min(3, 1 + (int(n.split('STYLE')[1][:2]) - 1) // 5))
                    v.append((2 * pt, 2 * pr, lv, op))
            elif cat == 'vinyl':   # the old selection: its vinyl's group and level
                v = []
                for n in names:
                    q = part(n)
                    g = q['level'] & 31 if q else len(VINYL_KIND)
                    v.append(rows.pair(VINYL_KIND[g] if g < len(VINYL_KIND) else 0, q['level'] >> 5 if q else 0))
            else:
                v = [(0, 0, 0, 0)] * len(names)
            per[cat] = v[:255]
        cars += model.encode()[:15].ljust(16, b'\0') + bytes(len(per[c]) for c in CATS)
        for c in CATS:
            cars += b''.join(struct.pack('<4H', *x) for x in per[c])
        report[model] = {c: [x[0] for x in per[c]] for c in CATS}
    out = struct.pack('<4sI', b'N2VR', 7) + struct.pack('<I', len(garage)) + cars
    # the rims' library, in rims.n2rm's order (a spinner: its wheel and its spinning part)
    rm = (a.out / 'rims.n2rm').read_bytes()
    ncars, nrims = struct.unpack_from('<II', rm, 8)
    at, rims = 16 + ncars * (16 + 4 + 4 * 5 * 4), []
    for k in range(nrims):
        name = rm[at:at + 24].split(b'\0')[0].decode()
        at += 24 + 4 + 16
        pt, pr, lv, op = pair(KIND['rims'], name)
        if name.startswith('SPINNER_'):
            spt, spr, _, sop = rows.pair(KIND['spinner'], level(name))
            pt, pr, op = pt + spt, pr + spr, op or sop
        rims.append((pt, pr, lv, op))
    out += pack(rims)
    # the vinyls' catalogue (convert_vinyls: the layers' index)
    vin = [rows.pair(VINYL_KIND[g] if g < len(VINYL_KIND) else 0, level(n)) for n, g, _, _ in convert_vinyls.catalogue(a.game)]
    out += pack(vin)
    # the neons (the parts table's type 54, in its order: garage.c's), 3 pairs each
    neons = parts.of_kind(54)
    out += struct.pack('<I', len(neons)) + b''.join(
        struct.pack('<4H', *rows.pair(KIND[k], q['level'] >> 5)) for q in neons for k in ('neon', 'neon_engine', 'neon_trunk'))
    # the body's paints (convert_paints' order: 60 Gloss, 60 Metallic, 30 Pearlescent) by their brand
    by48 = {q['key']: q for q in parts.of_kind(48)}
    body = partsdb.palette(parts, 'GLOSS') + partsdb.palette(parts, 'METAL') + [by48[fe.bin_hash(f'PEARL{k}_PAINT')] for k in range(1, 31)]
    brand = (['GLOSS'] * 60 + ['METAL'] * 60 + ['PEARL'] * 30)
    out += pack([rows.pair(PAINT_KIND[b], q['level'] >> 5) for q, b in zip(body, brand)])
    out += pack([rows.pair(KIND['paint_spoiler'], q['level'] >> 5) for q in body])
    out += pack([rows.pair(KIND['paint_rim'], q['level'] >> 5) for q in partsdb.palette(parts, 'RIMS')])
    out += pack([rows.pair(KIND['paint_brake'], q['level'] >> 5) for q in partsdb.palette(parts, 'CALIPERS')])
    carbon = level('CARBON FIBRE')   # the carbon pieces' part (type 51)
    consts = [rows.pair(KIND['decal'], 1), rows.pair(KIND['decal'], 1),
              rows.pair(KIND['tint'], 1), rows.pair(KIND['tint'], 2), rows.pair(KIND['tint'], 3),
              (0, 0, 0, 0), rows.pair(KIND['trunk_carbon'], carbon), (0, 0, 0, 0), rows.pair(KIND['kit_carbon'], carbon)]
    out += pack(consts)
    # the performance packages: perf.h's categories (convert_perf's
    # SHOP_CATEGORY: 0x34A15's) and the kind of each (0x112ea0)
    out += pack([(0, 0, lv, rows.opens.get((PERF_KIND[c], lv), 0)) for c in convert_perf.SHOP_CATEGORY for lv in (1, 2, 3)])
    # the preset cars' points (Rachel's 350Z: the career's prologue car)
    presets = [(n, preset_points(a.game, rows, n)) for n in PRESETS]
    out += struct.pack('<I', len(presets)) + b''.join(n.encode()[:31].ljust(32, b'\0') + struct.pack('<I', p or 0) for n, p in presets)
    # the other parts' paints (version 7), garage.h's VR_PAINT_ROOF on
    out += pack([rows.pair(KIND['paint_roof'], q['level'] >> 5) for q in body])
    out += pack([rows.pair(KIND['paint_mirror'], q['level'] >> 5) for q in body])
    out += pack([rows.pair(KIND['paint_spinner'], q['level'] >> 5) for q in partsdb.palette(parts, 'RIMS')])
    out += pack([rows.pair(KIND['paint_engine'], q['level'] >> 5) for q in partsdb.palette(parts, 'HOSES')])
    out += pack([rows.pair(KIND['paint_muffler'], q['level'] >> 5) for q in partsdb.palette(parts, 'MUFFLERS')])
    (a.out / 'rating.n2vr').write_bytes(out)
    (a.out / 'rating.json').write_text(json.dumps(dict(cars=report, rims=[x[0] for x in rims], vinyls=len(vin), consts=consts), indent=1))
    print(f'rating.n2vr: {len(garage)} cars, {len(rims)} rims, {len(vin)} vinyls, {len(neons)} neons, {len(body)} paints, consts {consts}, presets {presets}')


if __name__ == '__main__':
    main()
