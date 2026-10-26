#!/usr/bin/env python3
"""Where and when it rains in the city, as the game decides: rain.n2rr.

TRACKS/L4RA.BUN chunk 0x8003B600 holds the city's environment maps by
region, 13 sets of chunk 0x8003B601 (Rain, Wind, Normal Fog, Night Raining
Fog, the screen tints, Volumes...), each:
  0x3B602 (36 bytes): u32 the set's name hash (h*33+c from -1, Xbox
    default.xbe 0x4cdc0), f32 x min, y min, x max, y max (game axes), u32
    nodes, regions, u32 ?, u32 values per region;
  0x3B603/0x3B604: the values' types and offsets in a region's record;
  0x3B605: the regions' records (Rain: f32 intensity, f32 threshold);
  0x3B608: a quadtree of u16 x 4 nodes (0x1e0c70): an inner node's four
    children (x below the middle and y below it, x below and y above, x
    above and y below, x above and y above); a leaf has 0 first and its
    region second.
The game (0x209850, 0x20a730) rains at the car's place with its region's
intensity when 0.5 + 0.5 sin((x / 50 + t) degrees), x the car's game x and
t the clock in seconds (0x2026c0), is at most the region's threshold: a band
sweeping the city, back over a place every six minutes.

rain.n2rr: 'N2RR', u32 version 1, f32 x min, y min, x max, y max, u32 nodes,
u32 regions, nodes x u16[4], regions x f32[2] (intensity, threshold).
"""
import argparse
from pathlib import Path
import struct

from n2formats import archived
import n2source

ROOT = Path(__file__).resolve().parents[1]


def name_hash(s):
    h = 0xffffffff
    for c in s.encode():
        h = (h * 33 + c) & 0xffffffff
    return h


def region_set(game, name):
    """The set's header fields, nodes and region records."""
    d = archived(game, 'TRACKS/L4RA.BUN')
    at = d.index(struct.pack('<I', 0x8003b600))
    blk = d[at + 8:at + 8 + struct.unpack_from('<I', d, at + 4)[0]]
    o = 0
    while o + 8 <= len(blk):
        _, n = struct.unpack_from('<II', blk, o)
        sub, parts, p = blk[o + 8:o + 8 + n], {}, 0
        while p + 8 <= len(sub):
            t, m = struct.unpack_from('<II', sub, p)
            parts[t] = sub[p + 8:p + 8 + m]
            p += 8 + m
        h = parts.get(0x3b602, b'')
        if len(h) >= 36 and struct.unpack_from('<I', h, 0)[0] == name_hash(name):
            bounds = struct.unpack_from('<4f', h, 4)
            nodes, regions, _, per = struct.unpack_from('<4I', h, 20)
            values = struct.unpack_from(f'<{regions * per}f', parts[0x3b605])
            tree = parts[0x3b608][:nodes * 8]
            return bounds, nodes, regions, per, tree, values
        o += 8 + n
    raise ValueError(f'no region set {name} in L4RA')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    a = ap.parse_args()
    bounds, nodes, regions, per, tree, values = region_set(a.game, 'Rain')
    pairs = [values[per * r:per * r + 2] for r in range(regions)]
    out = struct.pack('<4sI4f2I', b'N2RR', 1, *bounds, nodes, regions) + tree + b''.join(struct.pack('<2f', *p) for p in pairs)
    (a.out / 'rain.n2rr').write_bytes(out)
    print(f'rain.n2rr: {nodes} nodes, {regions} regions (intensity, threshold): ' + ', '.join(f'{i:.2f}/{t:.2f}' for i, t in pairs))


if __name__ == '__main__':
    main()
