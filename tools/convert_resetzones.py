#!/usr/bin/env python3
"""Xbox NFSU2 reset zones -> PSP resetzones.n2rz.

Every TRACKS/ROUTES<track>/PATHS<id>.BIN carries the track's zones in
leaf 0x3414a: records of a u32 type, the zone's spot (f32 X, Y) and
heading (f32 X, Y), an f32 height, its bounding box, then at 0x40 an s16
point count and an s16 record size, the outline from 0x44 (f32 X, Y
each). In the city every event's file has the same set. The ones of type
4 are where the game puts a player who resets inside them (the original's
Player::HandleReset), on its spot and heading; types 0 to 3 (wrong way
and the other resets) do not occur in the city's files.

resetzones.n2rz: magic, version 1, zones, bytes of floats; per zone: GU
x, z of the spot, its yaw, the point count, then GU x, z of each point
(f32 each). GU x = game Y, GU z = game X.
"""
import argparse
import math
from pathlib import Path
import struct

from n2formats import archived, chunks, game_hash
import n2source

ROOT = Path(__file__).resolve().parents[1]
RESET = 4


def zones(d):
    out = []
    for t, p, n in chunks(d):
        if t != 0x3414a:
            continue
        o = p
        while o < p + n:
            kind, px, py, dx, dy = struct.unpack_from('<I4f', d, o)
            npts, size = struct.unpack_from('<hh', d, o + 0x40)
            if size <= 0:
                break
            pts = [struct.unpack_from('<2f', d, o + 0x44 + 8 * k) for k in range(npts)]
            if kind == RESET and npts >= 3:
                out.append(((py, px), math.atan2(dy, dx), [(y, x) for x, y in pts]))
            o += size
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--track', default='L4RA')
    args = ap.parse_args()
    have = {e[0] for e in struct.iter_unpack('<6I', (args.game / 'ZDIR.BIN').read_bytes())}
    first = next(e for e in range(4000, 4800) if game_hash(f'TRACKS/ROUTES{args.track}/PATHS{e}.BIN') in have)
    zs = zones(archived(args.game, f'TRACKS/ROUTES{args.track}/PATHS{first}.BIN'))
    body = bytearray()
    for (x, z), yaw, pts in zs:
        body += struct.pack('<4f', x, z, yaw, len(pts))
        for p in pts:
            body += struct.pack('<2f', *p)
    name = 'resetzones' if args.track == 'L4RA' else f'resetzones-{args.track}'
    (args.out / f'{name}.n2rz').write_bytes(struct.pack('<4s3I', b'N2RZ', 1, len(zs), len(body)) + body)
    print(len(zs), 'reset zones')


if __name__ == '__main__':
    main()
