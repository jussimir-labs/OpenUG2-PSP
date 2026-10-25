#!/usr/bin/env python3
"""The race AI's difficulty data from the Xbox NFSU2 GLOBALB.BUN ->
difficulty.n2df.

GLOBALB chunk 0x34B00 (2784 bytes after the 8-byte chunk header) is the
game's difficulty table, read by the race starter (the opponents' skill
levels) and by every AI racer (its pace and catch-up). The fields, all
little-endian floats (graphs are (x, y) pairs, linear between points):

  0x000  catch-up by the gap to the player (m, + = the AI ahead), for the
         lowest skill: [laps class 2][traffic class 2] graphs of 11 points
  0x160  the same for the highest skill
  0x2C0  / 0x318  Street X's (short track) low / high skill graphs
  0x420  4 bytes: on Street X, use the place boost, the car-class scale,
         the speed-ratio factor, the race-progress fade (all 0)
  0x424  / 0x464  place boost, 16 per place difference, low / high skill
  0x4A4  / 0x4D4  speed-ratio slope by the player's place (6 x low, high),
         the player faster than the line / slower
  0x504  catch-up fade over the race's progress (4 points)
  0x524  10 skill levels x 0x68 bytes: 25 floats (+0x14 line speed
         factor, +0x18 floor of the speed-ratio factor, +0x1C/+0x20 the
         same on Street X, +0x5C seconds before the catch-up starts, +0x60
         metres of random look-ahead) and 4 bytes
  0x94C  catch-up scale by difficulty and the player's car class (3 x 4)
  0x97C  skill level odds: [2][3 difficulties][10 levels], cumulative
  0xAD4  seconds between look-ahead changes; 0xAD8 seconds between
         catch-up updates; 0xADC the largest gap that still counts (m)

The block goes out whole. After it, the career's opponents (bin 0x34A11,
struct CareerRace of 0x88 bytes): per race, the opponents at +0x40 (12
bytes each, the skill level for Easy/Medium/Hard at +8..+10), their number
at +0x7C, the drag AI's throttle index at +0x7D, the stage at +0x37 and
the events at +0x18 (count at +0x7E), as tools/convert_career.py reads them.

difficulty.n2df: 'N2DF', u32 version 1, u32 block size, the block, u32 n,
n x 24 bytes: u16 event id, u8 stage, u8 opponents, u8 throttle index,
u8 skill[5][3] (Easy, Medium, Hard per opponent), u8 pad x4. One entry
per (event, stage), from the first career race that lists it.
"""
import argparse
import struct
from pathlib import Path

from n2formats import archived, chunks

ROOT = Path(__file__).resolve().parents[1]
BLOCK = 0xAE0


def find_chunk(d, cid):
    for t, p, n in chunks(d):
        if t == cid:
            return d[p:p + n]
    raise ValueError(f'chunk {cid:#x} not found')


def career_skills(d):
    races = find_chunk(d, 0x34A11)
    out = {}
    for k in range(len(races) // 0x88):
        r = races[k * 0x88:(k + 1) * 0x88]
        nopp = min(r[0x7C], 5)
        skills = bytes(r[0x40 + 12 * j + 8 + i] if j < nopp else 0 for j in range(5) for i in range(3))
        for i in range(min(r[0x7E], 4)):
            eid = struct.unpack_from('<H', r, 0x18 + 4 * i)[0]
            out.setdefault((eid, r[0x37]), (nopp, r[0x7D], skills))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    d = archived(args.game, 'GLOBAL/GLOBALB.BUN')
    block = find_chunk(d, 0x34B00)
    if len(block) != BLOCK:
        raise ValueError(f'difficulty block of {len(block)} bytes')
    career = career_skills(d)
    out = bytearray(b'N2DF') + struct.pack('<II', 1, BLOCK) + block + struct.pack('<I', len(career))
    for (eid, stage), (nopp, mod, skills) in sorted(career.items()):
        out += struct.pack('<HBBB', eid, stage, nopp, mod) + skills + bytes(4)
    (args.out / 'difficulty.n2df').write_bytes(out)
    print(f'difficulty.n2df: {len(career)} career entries, {len(out)} bytes')


if __name__ == '__main__':
    main()
