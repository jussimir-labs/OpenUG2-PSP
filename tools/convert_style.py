#!/usr/bin/env python3
"""The style moments of GLOBAL/GLOBALB (chunk 0x34A07) -> moments.n2sm.

The race's style points come from 39 moments ("POWER SLIDE", "HANG TIME",
"AVOIDED ACCIDENT", "GAINED 1ST"...), one 48-byte record each from the
chunk's data aligned to 16 bytes (the GameCube's Speed.elf reads it so:
LoaderStyleMomentsInfo, StyleMoment): u32 the name's language key, u8 the
moment's number, u8 its family, u8 1 when it lasts (scored while it goes
on) and u8 the race kinds it counts in (1 circuit and sprint, 2 drag, 4
drift, 8 and 0x40 the other kinds, 0x10 Street X: none), then f32 the time
it must last before it scores, the time without it that ends it, the
points it must pass, a cap (0 none), the most its multiplier grows to and
by how much each time, two unused, the points a second (or a time), and
s8 how many times a race it can score (-1 any).

moments.n2sm: 'N2SM', u32 count, count x 40 bytes: char name[20] (the
English text, "" none), u8 number, continuous, kinds, s8 times, f32 lasts,
timeout, threshold, cap, mult_max, mult_step, rate.
"""
import argparse
from pathlib import Path
import struct

from n2formats import archived, chunks

ROOT = Path(__file__).resolve().parents[1]


def moments(globalb):
    t, p, n = next(c for c in chunks(globalb) if c[0] == 0x34a07)
    at = (p + 0xf) & ~0xf
    out = []
    for k in range((p + n - at) // 48):
        r = globalb[at + 48 * k:at + 48 * (k + 1)]
        key, num, family, cont, kinds = struct.unpack_from('<I4B', r)
        f = struct.unpack_from('<10f', r, 8)
        times = struct.unpack_from('<b', r, 0x2c)[0]
        out.append(dict(key=key, num=num, cont=cont, kinds=kinds, times=times, lasts=f[0], timeout=f[1],
                        threshold=f[2], cap=f[3], mult_max=f[4], mult_step=f[5], rate=f[8]))
    if [m['num'] for m in out] != list(range(len(out))):
        raise ValueError('style moments out of order')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    from convert_hud import strings
    lang = strings(args.game)
    ms = moments(archived(args.game, 'GLOBAL/GLOBALB.BUN'))
    blob = bytearray(struct.pack('<4sI', b'N2SM', len(ms)))
    for m in ms:
        name = lang.get(m['key'], '')
        blob += struct.pack('<20s3Bb7f', name.encode('latin-1', 'replace')[:19], m['num'], m['cont'], m['kinds'], m['times'],
                            m['lasts'], m['timeout'], m['threshold'], m['cap'], m['mult_max'], m['mult_step'], m['rate'])
        print(f"{m['num']:2} {name:18} kinds {m['kinds']:3} times {m['times']:3} rate {m['rate']:7.0f}")
    (args.out / 'moments.n2sm').write_bytes(blob)


if __name__ == '__main__':
    main()
