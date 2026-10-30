#!/usr/bin/env python3
"""The garage: customisation options of the 29 player cars.

The PSP builds the player's car from its stock pack (car-<MODEL>.n2pv, its
parts tagged by slot, tools/convert_car.py) and one small pack per chosen
option, merged in memory, and paints its body skin at run time, as the
original composites <CAR>_SKIN1 itself. Per car, every option the game has
for it:

- parts-<MODEL>.n2op: the car's options, a pack per option and level of
  detail: every kit's front bumper, rear bumper and skirts (KITnn, each
  bought on its own as the PS2's), hood (STYLEnn), spoiler (the 40 of the
  SPOILER library at the car's socket), head and tail lights (STYLEnn),
  roof scoop (the ROOF library's 51 at the ROOF_SCOOP socket), exhaust tips
  (the EXHAUST library's 10 at the car's own tips' places) (the rims:
  the shop's library, tools/convert_rims.py; the table keeps a selection's
  names, the rims of the looks saved before it); a pack's
  body-skin texture is a stub (key 0 maps to the stock pack's skin);
- skin-<MODEL>.n2sk: the body skin's panel lines and details (DOORLINE and
  its mask) as a layer over the paint;
- the vinyls: the whole catalogue per car, tools/convert_vinyls.py
  (vinyls-<MODEL>.n2vc); the table here keeps a selection of 16 names, the
  vinyls of the looks saved before it;
- opt-<MODEL>-decal.n2pv: where the decals go on the car, one mesh per
  slot of each zone's layouts (DECAL_ZONES; the car's geometry
  <CAR>_DECAL_<ZONE>_<SHAPE>, each slot a slice textured DUMMY_DECAL<n>
  for the game to fill): the medium layout (fewer, larger slots) and,
  for the hood and the quarters, the small one; tagged with flag F_DECAL,
  slot SLOT_DECAL + zone, the layout slot << DECAL_SLOT_SHIFT and
  DECAL_SMALL; one level of detail;
- decal-<NAME>.n2pv: a decal's art (CARS/TEXTURES.BIN <NAME>_RECT for
  the hood, doors and quarters, <NAME>_WIDE for the windows), a pack of
  those two textures and no meshes; the decals are the decal shop's, the
  parts table's type 52 (tools/partsdb.py) in its order: per brand
  <BRAND>_BLACK_DECAL (the brand's own art) and <BRAND>_WHITE_DECAL (its
  white one), 182 with art on the disc;
- garage.n2gr: per car, the options there are, and the decals.

N2SK: 'N2SK', u32 width, height, kind (0: a layer's RGBA), 1024-byte RGBA
palette, w*h indices (linear, the CPU composites them).
garage.n2gr: 'N2GR', u32 version 7, u32 count, count x (char model[16],
u8 count per category (kit (the front bumpers), hood, spoiler, rims, vinyl,
headlight, brakelight, rear bumper, skirt, roof scoop, exhaust tips, wide
body kits, side mirrors, trunk audio layouts (version 4 without the last
three, 5 without the mirrors and audio, 6 without the audio)), u8
decal zones (bits), 2 bytes
0, then the names (char[24]) of each category in turn), then u32
decals, decals x (char art[24], char icon[24] (its brand's
DECAL_ICON_<BRAND>, cut at 23), char title[24]).
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import struct

import functools

import convert_car as cc
import mac_slots
import convert_hud
import convert_physics
import n2feng as fe
import partsdb
from n2formats import archived, car_record, lod_family, parts, wheel_faces
import n2source

ROOT = Path(__file__).resolve().parents[1]
# the table's categories (psp-viewer/garage.h GCAT_*: kit is the front bumper)
CATS = ('kit', 'hood', 'spoiler', 'rims', 'vinyl', 'headlight', 'brakelight', 'rear_bumper', 'skirt', 'roof', 'exhaust', 'wide', 'mirror', 'audio')
PARTS = ('kit', 'rear_bumper', 'skirt', 'hood', 'spoiler', 'headlight', 'brakelight', 'roof', 'exhaust', 'wide', 'mirror', 'audio')   # those with packs
PRESET_KEY = {'kit': 'kit', 'rear_bumper': 'rear_bumper', 'skirt': 'skirt', 'hood': 'hood', 'spoiler': 'spoiler',
              'headlight': 'headlight', 'brakelight': 'brakelight', 'roof': 'roof', 'exhaust': 'exhaust', 'wide': 'widebody', 'mirror': 'mirror', 'audio': 'trunk_audio'}
SLOT = {'kit': cc.SLOT_KIT, 'rear_bumper': cc.SLOT_REAR_BUMPER, 'skirt': cc.SLOT_SKIRT, 'hood': cc.SLOT_HOOD,
        'spoiler': cc.SLOT_SPOILER, 'rims': cc.SLOT_RIMS, 'headlight': cc.SLOT_HEADLIGHT, 'brakelight': cc.SLOT_BRAKELIGHT,
        'roof': cc.SLOT_ROOF, 'exhaust': cc.SLOT_EXHAUST, 'wide': cc.SLOT_BODY, 'mirror': cc.SLOT_MIRROR}
MAX = 64
SUVS = ('HUMMER', 'NAVIGATOR', 'ESCALADE')   # tools/convert_rims.py's: the SUVs' mirrors
RIM_BRANDS = ('ENKEI', 'VOLK', 'OZ', 'BBS', 'MOMO', 'KONIG', 'ADVAN', '5ZIGEN', 'RACINGHART', 'WORK')
# A brand's rim for a car: the one of the car's tyre, as the PS2's shop
# offers (the name gives the rim and the tyre in inches: BRAND_STYLE01_18_25,
# an 18 in rim in a 25 in tyre; the models are built at that size, and
# convert_car draws them so), the rim 18 in, else 17, 19, 16, 20, 15. One
# size for every car (18_25) gave a 206 (23 in) and an Escalade (32 in)
# the same wheels.
RIM_INNER = (18, 17, 19, 16, 20, 15)


def tyre_inches(game, car):
    """The car's tyre as a diameter in inches: its record's front radius
    (GLOBALB, the PS2's the same)."""
    return 2 * cc.car_record(archived(game, 'GLOBAL/GLOBALB.BUN'), car)[0][3] / 0.0254
VINYL_GROUPS = ('FLAME', 'TRIBAL', 'WILD', 'LIGHTNG', 'TEAR', 'STRIPE', 'LIQUID', 'MODERN')
# The decal shop's zones (the original's order) and the art each takes.
DECAL_ZONES = (('HOOD', 'RECT'), ('FRONT_WINDOW', 'WIDE'), ('REAR_WINDOW', 'WIDE'), ('LEFT_DOOR', 'RECT'),
               ('RIGHT_DOOR', 'RECT'), ('LEFT_QUARTER', 'RECT'), ('RIGHT_QUARTER', 'RECT'))
# The decal shop's brands whose icon is not DECAL_ICON_<BRAND>.
DECAL_ICON_OF = {'OZ': 'OZ_WHEELS', 'NX': 'NITROUS_EXPR', 'HNR': 'HANDR', 'JICMAGIC': 'JIC', 'JLA': 'JLAUDIO', 'B&M': 'BM',
                 'DO-LUCK': 'DOLUCK', 'ACAUTOTECHNIC': 'AUTOTECHNIC', 'XSENGINEERING': 'XSENG', 'WELD': 'WELD_RACING',
                 'CLUTCHMASTERS': 'CLUTCHMASTER', 'JAPAN': 'JAPAN_TUNING'}
DECAL_NO_ICON = 'VISUAL_PART_DECALS'   # ABS, Henshin, JSP, Extra Dimension: none of their own
DUMMY_DECAL1 = 0x910e6654       # game_hash('DUMMY_DECAL1'): a layout's first slot, DUMMY_DECAL<n> the n-th
F_DECAL = 0x800                 # pack mesh flag, mirrored in psp-viewer/main.c
SLOT_DECAL = 8                  # slots 8-14: the zones
DECAL_SLOT_SHIFT, DECAL_SMALL = 16, 1 << 19   # psp-viewer/garage.h
DECAL_WIDE_SHIFT = 21   # bits 21-23: a wide body kit's own places (WIDEn_DECAL_*, n its KITWnn), 0 the stock body's
DECAL_SLOTS = ((4, 8), (1, 0), (1, 0), (6, 0), (6, 0), (1, 2), (1, 2))   # per zone: medium, small layout


def car_options(game, car):
    """{category: [option names]} this port offers for the car."""
    names = [p.name for p in parts(archived(game, f'CARS/{car}/GEOMETRY.BIN'))]
    have = lambda owner, what: any(n.startswith(f'{car}_{owner}_') and cc.slot_of(n[len(car) + len(owner) + 2:]) == what
                                   for n in names)
    # every kit's pieces, the hoods and the lights (the first ones in the
    # order of the 8, 6 and 6 of before: a look saved then keeps its parts)
    kit = lambda what: [f'KIT{k:02d}' for k in range(1, 30) if have(f'KIT{k:02d}', what)]
    hoods = [f'STYLE{k:02d}' for k in range(1, 30) if any(n.startswith(f'{car}_STYLE{k:02d}_HOOD') for n in names)]
    spoilers = [f'SPOILER_STYLE{k:02d}' for k in range(1, 41)]   # the 40 of CARS/SPOILER (the first 10 those of before)
    roof = sorted({lod_family(p.name) for p in parts(archived(game, 'CARS/ROOF/GEOMETRY.BIN'))
                   if not lod_family(p.name).endswith('_CF')},
                  key=lambda f: (int(re.search(r'STYLE(\d+)', f).group(1)), f))   # STYLEnn, its DUAL and OFFSET
    exhaust = sorted({lod_family(p.name) for p in parts(archived(game, 'CARS/EXHAUST/GEOMETRY.BIN'))})
    if not any(n.startswith(f'{car}_KIT00_EXHAUST') for n in names):   # no tips of its own: nowhere to put the shop's
        exhaust = []
    rims, tyre = [], tyre_inches(game, car)
    for brand in RIM_BRANDS:
        sized = []
        for f in {lod_family(p.name) for p in parts(archived(game, f'CARS/WHEELS/GEOMETRY_{brand}.BIN'))}:
            m = re.fullmatch(rf'{brand}_STYLE\d+_(\d+)_(\d+)', f)
            if m and int(m[1]) in RIM_INNER:   # the nearest tyre, then the rim's order, then the first style
                sized.append((abs(int(m[2]) - tyre), RIM_INNER.index(int(m[1])), f))
        if sized:
            rims.append(min(sized)[2])
    store, index = cc.vinyl_index(game, car)
    vinyls = []
    for group in VINYL_GROUPS:
        vinyls += sorted(n[len(car) + 1:] for n in index
                         if n.startswith(f'{car}_{group}_') and n != cc.mask_name(n[:-5] if n.endswith('_MASK') else n)
                         and cc.mask_name(n) in index)[:2]
    lights = lambda what: [f'STYLE{k:02d}' for k in range(1, 30) if have(f'STYLE{k:02d}', what)]
    wide = [f'KITW{k:02d}' for k in range(1, 10) if have(f'KITW{k:02d}', 'BODY')]   # the wide body kits
    # the side mirrors: where the car's own are parts apart (on the others
    # they are the body's); the SUVs' (the Hummer its own), the cars' body
    # and post mirrors, 15 styles each, then their carbon ones (_CF; the
    # Hummer's have none)
    own = any(cc.is_mirror(n[len(car) + 7:]) for n in names if n.startswith(f'{car}_KIT00_'))
    kinds = (('HUMMER',) if car == 'HUMMER' else ('SUV',)) if car in SUVS else ('BODY', 'POST')
    mirror = [f'{kind}_STYLE{k:02d}{cf}' for cf in ('', '_CF') for kind in kinds if not (cf and kind == 'HUMMER')
              for k in range(1, 16)] if own else []
    audio = [f'KIT{k:02d}' for k in range(1, 10) if any(n.startswith(f'{car}_KIT{k:02d}_TRUNK_AUDIO') for n in names)]   # the trunk's layouts
    return dict(kit=kit('FRONT_BUMPER'), rear_bumper=kit('REAR_BUMPER'), skirt=kit('SKIRT'), hood=hoods, spoiler=spoilers,
                rims=rims, vinyl=vinyls, headlight=lights('HEADLIGHT'), brakelight=lights('BRAKELIGHT'), roof=roof, exhaust=exhaust,
                wide=wide, mirror=mirror, audio=audio)


def wide_track(game, car, kit):
    """How far a wide body kit takes the wheels out (cc.wheel_track: its
    faces past the stock body's), front and rear, in millimetres."""
    globalb = archived(game, 'GLOBAL/GLOBALB.BUN')
    wheels, faces = car_record(globalb, car), wheel_faces(globalb, car)
    stock, wide = cc.wheel_track(faces, wheels), cc.wheel_track(faces, wheels, kit)
    out = [max(0, round(1000 * (abs(w[1]) - abs(s[1])))) for s, w in zip(stock[:4], wide[:4])]
    return min(255, max(out[:2])), min(255, max(out[2:]))


def option_blobs(game, car, cat, name, lod):
    """The meshes of one option's slot as N2PV packs: {category: blob} (a
    kit gives its front bumper, rear bumper and skirts at once); {} when the
    car has no such part (or socket)."""
    preset = {'car': car}
    preset[PRESET_KEY[cat]] = name
    if cat == 'audio':   # a trunk layout: the inside, in the garage's pack alone
        if lod:
            return {}
        preset['interior'] = True
    if cat == 'wide':   # on the street its body a level coarser (368 triangles to the 206's 950), as Rachel's: the stock's 60 Hz kept
        preset['light_lod'] = {f'{car}_{name}_BODY': 2}
    try:
        b, textures, _ = cc.assemble(game, preset, lod)
    except ValueError:
        return {}
    out = {}
    for c in (('kit', 'rear_bumper', 'skirt') if cat == 'kit' else (cat,)):
        meshes = [m for m in b.batched() if (m[4] & cc.F_AUDIO if c == 'audio' else
                  (m[4] >> cc.SLOT_SHIFT) & 15 in ((SLOT[c], cc.SLOT_DOOR, cc.SLOT_DOOR_RIGHT) if c == 'wide' else (SLOT[c],)))]
        if not meshes:
            continue
        texdata = {}
        for ref in {m[2] for m in meshes if m[2] is not None}:
            if ref == 'SKIN':                      # the stock pack's skin is used
                texdata[ref] = (16, 8, bytes(1024), bytes(128), 1.0)
            elif ref == cc.CARBONFIBRE_TEX:
                texdata[ref] = cc.carbon_tile(textures.get(ref))
            else:   # (a shop's lamps on the street: 64 texels, as convert_car's)
                texdata[ref] = cc.to_clut8(textures.get(ref), 64 if lod and ref in b.lamps.values() else 128)
        tmp = Path(os.environ.get('TMPDIR', '/tmp')) / f'opt-{os.getpid()}.n2pv'
        cc.save_pack(tmp, meshes, texdata, 0)
        out[c] = tmp.read_bytes()
        tmp.unlink()
    return out


def decal_pack(game, car, out):
    """The car's decal places (see the module doc): the zones found, as bits."""
    every = list(parts(archived(game, f'CARS/{car}/GEOMETRY.BIN'), lambda n: n.startswith(car + '_')))
    meshes, zones = [], 0
    for wide in range(8):   # the stock body's places, then each wide body kit's (doors and quarters)
        head = f'{car}_DECAL_' if not wide else f'{car}_WIDE{wide}_DECAL_'
        found = [p for p in every if p.name.startswith(head)]
        # laid on the body they go on (cc.hug), the outermost of its
        # garage's and street's levels (one pack for both): the stock
        # body's parts, or the wide kit's KITW0n with the base's
        owner = f'{car}_KITW{wide:02d}_' if wide else f'{car}_KIT00_'
        surface = cc.decal_surface([p for p in every if p.name[-2:] in ('_A', '_B', '_C')
                                    and (p.name.startswith((owner, f'{car}_BASE_')) or (wide and p.name.startswith(f'{car}_KIT00_')
                                                                                           and 'BODY' not in p.name and 'DOOR' not in p.name))])
        for z, (zone, _) in enumerate(DECAL_ZONES):
            # Names are cut at 27 characters (the RECT_SMALL and RECT_MEDIUM
            # layouts can read the same): a layout is told by its slot count.
            cands = [p for p in found if p.name[len(head):].startswith(zone[:27 - len(head)])
                     and any(t == DUMMY_DECAL1 for _, t, _ in p.subs)]
            for small, n in enumerate(DECAL_SLOTS[z]):
                part = next((p for p in cands if len({t for _, t, _ in p.subs}) == n), None) if n else None
                if part is None:
                    continue
                for k in range(n):
                    ids = [i for i_, t, _ in part.subs if t == DUMMY_DECAL1 + k for i in i_]
                    if not ids:
                        continue
                    remap = {v: i for i, v in enumerate(sorted(set(ids)))}
                    vertices = []
                    for x, y, z3, u, v, nx, ny, nz in cc.hug([part.verts[vi] for vi in remap], surface):
                        vertices.append(struct.pack('<8f', u, v, ny, nz, nx, y, z3, x))   # as convert_car.Builder
                    meshes.append((vertices, [remap[i] for i in ids], DUMMY_DECAL1, 0xffffffff,
                                   F_DECAL | (SLOT_DECAL + z) << cc.SLOT_SHIFT | k << DECAL_SLOT_SHIFT | (DECAL_SMALL if small else 0)
                                   | wide << DECAL_WIDE_SHIFT, 1.0))
                if not wide:
                    zones |= 1 << z
    if meshes:
        cc.save_pack(out / f'opt-{car}-decal.n2pv', meshes, {DUMMY_DECAL1: (16, 8, bytes(1024), bytes(128), 1.0)}, 0)
    return zones


def decal_catalogue(game):
    """[(art, icon, title)]: the decal shop's decals in the parts table's
    order, those with art on the disc."""
    store = cc.SlotTPK(archived(game, 'CARS/TEXTURES.BIN'))
    art = {store.name(k)[:-5] for k in store.slots if store.name(k).endswith('_RECT')}
    named = {}
    for a in art:   # <BRAND>_BLACK_DECAL: the brand's art; <BRAND>_WHITE_DECAL: <BRAND>_WHITE
        named[fe.bin_hash(a + '_DECAL')] = a
        named[fe.bin_hash(a + '_BLACK_DECAL')] = a
    icons = {r[0] for t in convert_hud.Sources(game).flat for r in t.records.values() if r[0].startswith('DECAL_ICON_')}
    out = []
    for q in partsdb.Parts(game).of_kind(52):
        a = named.get(q['key'])
        if a is None:
            continue
        white = a.endswith('_WHITE')
        brand = a[:-6] if white else a
        icon = ('DECAL_ICON_' + DECAL_ICON_OF.get(brand, brand))[:23]
        out.append((a, icon if icon in icons else DECAL_NO_ICON, brand + (' White' if white else '')))
    return out


def decal_art(game, out):
    """decal-<NAME>.n2pv for the shop's decals: their RECT and WIDE art."""
    store = cc.SlotTPK(archived(game, 'CARS/TEXTURES.BIN'))
    names = {store.name(k): k for k in store.slots}
    decals = decal_catalogue(game)
    for name, _, _ in decals:
        blob = bytearray(struct.pack('<4s6I', b'N2PV', 3, 2, 0, 0, 0, 0))
        blob += bytes((-len(blob)) % 16)
        for shape in ('RECT', 'WIDE'):
            w, h, pal, idx, vrepeat = cc.to_clut8(store.get(names[f'{name}_{shape}']), 128)
            blob += struct.pack('<5If', names[f'{name}_{shape}'], w, h, 1, len(idx), vrepeat)
            blob += bytes((-len(blob)) % 16) + pal + idx
            blob += bytes((-len(blob)) % 16)
        (out / f'decal-{name}.n2pv').write_bytes(blob)
    return decals


def write_n2sk(path, kind, rgba, w, h, size=256):
    rgba, w, h = cc.box_downscale(rgba, w, h, size)
    pal, idx = cc.quantize(rgba)
    path.write_bytes(struct.pack('<4s3I', b'N2SK', w, h, kind) + pal + idx)


def skin_layers(game, car, out):
    """The panel-line layer."""
    textures = cc.Textures(game, car)
    detail = textures.car(f'{car}_DOORLINE').rgba()
    dmask = textures.car(f'{car}_DOORLINE_MASK').rgba()
    layer = bytearray(len(detail))
    for i in range(0, len(detail), 4):
        layer[i:i+3] = detail[i:i+3]
        layer[i+3] = min(255, dmask[i] * 3)     # as composite_skin: details ~0x80, lines 0x10-0x30
    write_n2sk(out / f'skin-{car}.n2sk', 0, layer, 512, 512)


def convert_car(game, out, car):
    """The car's options (parts-<MODEL>.n2op), its skin layer and decal places."""
    opts = car_options(game, car)
    done = {c: [] for c in CATS}
    packs = {}   # (category, name, lod) -> blob
    for cat in ('kit', 'hood', 'spoiler', 'headlight', 'brakelight', 'roof', 'exhaust', 'wide', 'mirror', 'audio'):
        for name in opts[cat] if cat != 'kit' else sorted(set(opts['kit']) | set(opts['rear_bumper']) | set(opts['skirt'])):
            for lod in (0, 1):
                for c, blob in option_blobs(game, car, cat, name, lod).items():
                    packs[(c, name, lod)] = blob
    for cat in PARTS:   # an option is offered with both its levels of detail (a trunk layout: the garage's alone)
        done[cat] = [n for n in opts[cat] if (cat, n, 0) in packs and ((cat, n, 1) in packs or cat == 'audio')]
    done['rims'] = opts['rims']   # the names alone: the rims are the library's (tools/convert_rims.py), these the looks saved before it
    done['vinyl'] = opts['vinyl']
    pads = {}
    for name in done['wide']:   # a wide body kit's entries: how far it takes the wheels out
        front, rear = wide_track(game, car, name)
        pads.update({('wide', name, lod): front | rear << 8 for lod in (0, 1)})
    write_options(out / f'parts-{car}.n2op', packs, pads)
    skin_layers(game, car, out)
    done['decal_zones'] = decal_pack(game, car, out)
    return car, done


NEON_MARKERS = (('ENGINE_NEON_START_%d', 'ENGINE_NEON_END_%d'), ('MARKER_TRUNK_NEON_START_%d', 'MARKER_TRUNK_NEON_END_%d'),
                ('CABIN_NEON_START_%d', 'CABIN_NEON_END_%d'))
# the underglow's, on the stock body's parts (CarRenderInfo's constructor:
# the front bumper's FRONT_NEON_n_, the skirts' SIDE_NEON_n_, the rear
# bumper's REAR_NEON_n_, n 0-2, START and END; a wide body kit's own are
# its body's)
UNDER_MARKERS = (('FRONT_BUMPER', 'FRONT_NEON_%d_'), ('SKIRT', 'SIDE_NEON_%d_'), ('REAR_BUMPER', 'REAR_NEON_%d_'))


def neon_tubes(game, out, cars):
    """neon.n2nn: the engine's, trunk's, cabin's and underglow's neon tubes,
    from the cars' markers (an upgraded engine's, a trunk layout's, the
    base's; the underglow's the stock front bumper's, skirts' and rear
    bumper's): 'N2NN', u32 version 2, u32 cars; cars x (char model[16], u8
    tubes engine, trunk, cabin, underglow; tubes x f32 start x, y, z, end x,
    y, z (GU, as the packs)). Version 1 had no underglow (its 4th count 0)."""
    blob = bytearray()
    for car in cars:
        marks, under = {}, {}
        for p in parts(archived(game, f'CARS/{car}/GEOMETRY.BIN'), lambda n: n.startswith(car + '_')):
            for k, m in p.sockets.items():
                marks.setdefault(k, m[12:15])
            for part, _ in UNDER_MARKERS:
                if p.name == f'{car}_KIT00_{part}_A':
                    under[part] = {k: m[12:15] for k, m in p.sockets.items()}
        tubes = []
        for start, end in NEON_MARKERS:
            row = []
            for i in range(8):
                a, b = marks.get(fe.bin_hash(start % i)), marks.get(fe.bin_hash(end % i))
                if a is None or b is None:
                    break
                row.append([a[1], a[2], a[0], b[1], b[2], b[0]])   # Xbox x forward, y lateral, z up -> GU
            tubes.append(row)
        row = []
        for part, name in UNDER_MARKERS:
            m = under.get(part, {})
            for i in range(3):
                a, b = m.get(fe.bin_hash(name % i + 'START')), m.get(fe.bin_hash(name % i + 'END'))
                if a is not None and b is not None:
                    row.append([a[1], a[2], a[0], b[1], b[2], b[0]])
        tubes.append(row)
        blob += struct.pack('<16s4B', car.encode(), *(len(r) for r in tubes))
        blob += b''.join(struct.pack('<6f', *t) for r in tubes for t in r)
    (out / 'neon.n2nn').write_bytes(struct.pack('<4s2I', b'N2NN', 2, len(cars)) + blob)


def carbon_pack(game, out):
    """carbon.n2pv: the carbon fibre (cc.carbon_tile) the PSP puts on the
    pieces bought in it (the hood, trunk, doors, kit), in place of their
    paint; one degenerate triangle carries it."""
    tex = cc.Textures(game, '350Z').get(cc.CARBONFIBRE_TEX)
    vert = struct.pack('<8f', 0, 0, 0, 1, 0, 0, 0, 0)
    cc.save_pack(out / 'carbon.n2pv', [([vert] * 3, [0, 1, 2], cc.CARBONFIBRE_TEX, 0xffffffff, cc.F_CARBON, 3.0)],
                 {cc.CARBONFIBRE_TEX: cc.carbon_tile(tex)}, 0)


def write_options(path, packs, pads={}):
    """parts-<MODEL>.n2op: 'N2OP', u32 version 1, u32 count, count x (u8
    category (CATS' order), u8 detail (0 the garage's, 1 the street's), u16
    pad (a wide body kit's: how far out the wheels go, mm, the front's in
    the low byte, the rear's in the high; 0 the others), char name[24], u32
    offset, u32 bytes), then the packs."""
    keys = sorted(packs, key=lambda k: (CATS.index(k[0]), k[1], k[2]))
    base = 12 + 36 * len(keys)
    base += -base % 16
    table, data, at = bytearray(), bytearray(), base
    for c, name, lod in keys:
        blob = packs[(c, name, lod)]
        table += struct.pack('<2BH24s2I', CATS.index(c), lod, pads.get((c, name, lod), 0), name.encode()[:23], at, len(blob))
        data += blob + bytes(-len(blob) % 16)
        at += len(blob) + (-len(blob) % 16)
    head = struct.pack('<4s2I', b'N2OP', 1, len(keys)) + table
    path.write_bytes(head + bytes(base - len(head)) + data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--cars', help='comma-separated models (default: the fleet)')
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--decals', action='store_true', help='the decals alone: their art and the table, the cars as garage.json has them')
    ap.add_argument('--neon', action='store_true', help='neon.n2nn alone (the neon tubes), the cars as garage.json has them')
    args = ap.parse_args()
    if args.neon:
        neon_tubes(args.game, args.out, list(json.loads((args.out / 'garage.json').read_text())))
        return
    cars = args.cars.split(',') if args.cars else convert_physics.playable(
        convert_physics.records(archived(args.game, 'GLOBAL/GLOBALB.BUN')))
    table = args.out / 'garage.json'
    if args.decals:
        result = {}
    else:
        with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
            # one core per car from the Mac's queue (tools/mac_slots.py)
            result = dict(pool.map(functools.partial(mac_slots.call, 'convert_garage', convert_car),
                                   [args.game] * len(cars), [args.out] * len(cars), cars))
    known = json.loads(table.read_text()) if table.exists() and (args.cars or args.decals) else {}
    known.update(result)
    decals = decal_art(args.game, args.out)
    carbon_pack(args.game, args.out)
    neon_tubes(args.game, args.out, list(known))
    blob = bytearray(struct.pack('<4s2I', b'N2GR', 7, len(known)))
    for car, opts in known.items():
        counts = [min(MAX, len(opts.get(c, []))) for c in CATS]
        blob += struct.pack(f'<16s{len(CATS) + 1}B2x', car.encode(), *counts, opts.get('decal_zones', 0))
        for c, n in zip(CATS, counts):
            blob += b''.join(struct.pack('<24s', name.encode()) for name in opts.get(c, [])[:n])
    blob += struct.pack('<I', len(decals)) + b''.join(struct.pack('<24s24s24s', *(f.encode()[:23] for f in d)) for d in decals)
    (args.out / 'garage.n2gr').write_bytes(blob)
    table.write_text(json.dumps(known, indent=1) + '\n')
    for car, opts in result.items():
        print(car, {c: v if isinstance(v, int) else len(v) for c, v in opts.items()})


if __name__ == '__main__':
    main()
