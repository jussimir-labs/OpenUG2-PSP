#!/usr/bin/env python3
"""The body shop's names, as the game's shop shows them: partnames.n2pn.

The parts table (tools/partsdb.py, GLOBAL/GLOBALB.BUN) gives each part a
title (its `no`: the place of the name in the names' pool, in words): the
kits by number, the same on every car (KIT01 "SNYPER", KIT03 "WOLFPACK",
up to KIT30 "VALHALLA"), the hoods (STYLE02 "BEAST"), the head and tail
lights, the wide body kits (KITW01 "VAPOR"), the spoilers (SPOILER_STYLE04
"JORGEN"), the roof scoops (ROOF_STYLE02 "JETSTREAM") and the exhaust tips
(EXHAUST_STYLE01_LEVEL1 "VORTEX"); the mirrors' keys are not found yet. A part's key is
the bin hash of its name: <CAR>_KIT03_FRONT_BUMPER (REAR_BUMPER, SKIRT),
<CAR>_STYLE05_HOOD, <CAR>_STYLE02_HEADLIGHT (BRAKELIGHT), <CAR>_KITW01_BODY,
SPOILER_STYLE07, ROOF_STYLE05_DUAL, EXHAUST_STYLE01_LEVEL1; psp-viewer/main.c cust_name builds it
from the garage's option and looks its title up (garage_part_title).

partnames.n2pn: 'N2PN', u32 version 1, u32 parts, u32 titles' bytes; parts
x (u32 key, u32 the title's offset), sorted by key; then the titles
(zero-terminated).
"""
import argparse
from pathlib import Path
import struct

from partsdb import Parts
import n2source

ROOT = Path(__file__).resolve().parents[1]
KINDS = (1, 2, 6, 7, 9, 11, 12, 14, 15, 16)   # front and rear bumpers, wide body, roof scoops, hoods, skirts, spoilers, head and tail lights, exhaust tips


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    a = ap.parse_args()
    parts = Parts(a.game)
    keys = {q['key']: q['title'].strip() for q in parts.parts if q['kind'] in KINDS and q['title'].strip()}
    pool, at = bytearray(), {}
    for t in sorted(set(keys.values())):
        at[t] = len(pool)
        pool += t.encode('latin-1') + b'\0'
    pool += bytes(-len(pool) % 4)
    blob = struct.pack('<4s3I', b'N2PN', 1, len(keys), len(pool))
    blob += b''.join(struct.pack('<2I', k, at[keys[k]]) for k in sorted(keys)) + bytes(pool)
    (a.out / 'partnames.n2pn').write_bytes(blob)
    print(f'partnames.n2pn: {len(keys)} parts, {len(at)} names, {len(blob) // 1024} KB')


if __name__ == '__main__':
    main()
