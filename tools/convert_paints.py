#!/usr/bin/env python3
"""The original's body paints and their light materials, from GLOBALB ->
paints.n2pt.

The parts database (chunk 0x80034602) holds 12167 parts of 14 bytes in
0x34604: u32 part hash (the bin hash of its name, GLOSS_L1_COLOR01), u8
part type, u8 category, u16 car, u16 the attribute list of its geometry,
u16 the attribute list of its own, u16. An attribute list is a u16 count
then that many u16 indices (0x3460C) into the attributes (0x34605: u32 key
hash, u32 value). Body paints are type 48, BRAND_NAME GLOSS, METAL or PEARL
(categories 32, 64, 96): 60 Gloss, 60 Metallic and 30 Pearlescent in the
paint shop's grid order, each with RED, GREEN, BLUE (0-255) and a light
material (key 0x6ba02c05: its hash).

The light materials (chunks 0x135200 at GLOBALB's start, 160 bytes each:
u32 version, two u32, u32 name hash, u32, char name[28], 28 f32) give how
the paint takes the light, each term between a value at a grazing view
(Min) and one face-on (Max):
  0 diffuse Min scale, 1-3 its RGB; 4 diffuse Max scale, 5-7 its RGB
    (multiplying the paint's colour); 8, 9 alpha Min and Max;
  10 specular power; 11 specular Min scale, 12-14 RGB; 15 Max scale,
    16-18 RGB (the highlight's own colour);
  19 envmap power; 20 envmap Min scale, 21-23 RGB; 24 Max, 25-27 RGB.
The layout reads off the data: CHROME has no diffuse and an envmap of
1.1; the windshield tints' Max colour is the tint (red 1.5, 0, 0) and the
Min its complement; a paint's envmap is strong grazing (0.86) and weak
face-on (0.10), as the PS2 shows its coat. A Metallic's (METPAINTGREEN,
Rachel's 350Z) diffuse goes from 0.42 grazing to 0.97 x (0.97, 1.34,
0.99) face-on and its highlight is yellow (1.31 x (1.32, 1.14, 0.25)): the
green turning lime toward the viewer. A Pearlescent's (CUSTOMPAINT_n)
diffuse Min and Max differ in hue.

paints.n2pt: 'N2PT', u32 version 1, u32 paints, u32 materials; paints x 8
bytes (u8 r, g, b, category 0 Gloss 1 Metallic 2 Pearlescent; u16
material, u16 the part's level in the parts table, 1-3: the level
BuildRandomSkin draws a rival's paint by; files before it 0); materials x
28 f32 (the order above).
"""
import argparse
import json
from pathlib import Path
import struct

from n2feng import bin_hash
from n2formats import archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
PAINT_TYPE = 48
CATEGORIES = (('GLOSS', 60), ('METAL', 60), ('PEARL', 30))
MATERIAL_KEY = 0x6ba02c05


def materials(globalb):
    """{name hash: (name, 28 floats)} of the light materials."""
    out = {}
    for tag, p, n in chunks(globalb):
        if tag == 0x135200 and n >= 160:
            h = struct.unpack_from('<I', globalb, p + 12)[0]
            name = globalb[p + 20:p + 48].split(b'\0')[0].decode('latin-1')
            out[h] = (name, struct.unpack_from('<28f', globalb, p + 48))
    return out


def paints(globalb):
    """[(category, part index, (r, g, b), material hash, level)] in grid order."""
    sub = {tag: globalb[p:p + n] for tag, p, n in chunks(globalb) if tag in (0x34604, 0x34605, 0x3460c)}
    parts, attrs, lists = sub[0x34604], sub[0x34605], sub[0x3460c]
    attr = [struct.unpack_from('<2I', attrs, 8 * i) for i in range(len(attrs) // 8)]
    lst = struct.unpack_from(f'<{len(lists) // 2}H', lists)
    def attributes(o):
        if o >= len(lst):
            return {}
        return {attr[j][0]: attr[j][1] for j in lst[o + 1:o + 1 + lst[o]] if j < len(attr)}
    red, green, blue, brand = (bin_hash(k) for k in ('RED', 'GREEN', 'BLUE', 'BRAND_NAME'))
    want = {bin_hash(name): cat for cat, (name, _) in enumerate(CATEGORIES)}
    found = [[] for _ in CATEGORIES]
    for i in range(len(parts) // 14):
        h, kind, flags, _, _, own, _ = struct.unpack_from('<IBBHHHH', parts, 14 * i)
        if kind != PAINT_TYPE:
            continue
        a = attributes(own)
        cat = want.get(a.get(brand))
        if cat is None or red not in a:
            continue
        found[cat].append((cat, i, (a[red], a[green], a[blue]), a.get(MATERIAL_KEY, 0), flags >> 5))
    out = []
    for cat, (name, count) in enumerate(CATEGORIES):
        if len(found[cat]) < count:
            raise SystemExit(f'{name}: {len(found[cat])} paints, {count} expected')
        out += found[cat][:count]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    globalb = archived(args.game, 'GLOBAL/GLOBALB.BUN')
    mats = materials(globalb)
    rows = paints(globalb)
    used = sorted({m for _, _, _, m, _ in rows if m in mats})
    index = {m: k for k, m in enumerate(used)}
    blob = bytearray(struct.pack('<4s3I', b'N2PT', 1, len(rows), len(used)))
    for cat, _, rgb, m, level in rows:
        blob += struct.pack('<4BHH', *rgb, cat, index.get(m, 0xffff), level)
    for m in used:
        blob += struct.pack('<28f', *mats[m][1])
    (args.out / 'paints.n2pt').write_bytes(blob)
    report = dict(paints=len(rows), materials=len(used),
                  rows=[dict(category=CATEGORIES[cat][0], part=i, rgb=rgb, material=mats.get(m, ('?',))[0], level=level)
                        for cat, i, rgb, m, level in rows])
    (args.out / 'paints.json').write_text(json.dumps(report, indent=1) + '\n')
    print(len(rows), 'paints,', len(used), 'materials')


if __name__ == '__main__':
    main()
