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

Under cover it does not rain: Rain::Update looks for a TrackPathZone of
types 5 and 6 (TrackPathManager::FindZone, mask 0x60) at the car and 9.5 m
ahead of it; in one, with the car below the zone's height (+0x14), no drop
is drawn on that side of 9.5 m. Those zones are the free roam's tunnels and
covered roads (TRACKS/ROUTESL4RA/PathsFreeRoam.bin, chunk 0x3414A: a
68-byte header, u32 type at +0, f32 height at +0x14, the bounding box
(x min, y min, x max, y max) at +0x20, u16 vertices at +0x40 and the
record's size at +0x42, then the vertices, f32 x y each).

rain.n2rr: 'N2RR', u32 version 2, f32 x min, y min, x max, y max, u32 nodes,
u32 regions, nodes x u16[4], regions x f32[2] (intensity, threshold); then
u32 cover zones, u32 vertices, zones x (f32 x min, y min, x max, y max,
height, u16 first vertex, u16 vertices), vertices x f32[2] (game x, y).
"""
import argparse
from pathlib import Path
import struct

from n2formats import archived, chunks
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


def cover_zones(game):
    """The zones the rain stops in: [(bounds, height, vertices)], types 5 and 6."""
    path = archived(game, 'TRACKS/ROUTESL4RA/PathsFreeRoam.bin')
    tables = [path[p:p + n] for t, p, n in chunks(path) if t == 0x3414a]
    if len(tables) != 1:
        raise ValueError('free-roam zone table')
    data, at, out = tables[0], 0, []
    while at + 68 <= len(data):
        n, size = struct.unpack_from('<2H', data, at + 64)
        if size < 68 or at + size > len(data) or n > (size - 68) // 8:
            raise ValueError('zone record bounds')
        if struct.unpack_from('<I', data, at)[0] in (5, 6):
            out.append((struct.unpack_from('<4f', data, at + 32), struct.unpack_from('<f', data, at + 0x14)[0],
                        [struct.unpack_from('<2f', data, at + 68 + 8 * i) for i in range(n)]))
        at += size
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    a = ap.parse_args()
    bounds, nodes, regions, per, tree, values = region_set(a.game, 'Rain')
    pairs = [values[per * r:per * r + 2] for r in range(regions)]
    out = struct.pack('<4sI4f2I', b'N2RR', 2, *bounds, nodes, regions) + tree + b''.join(struct.pack('<2f', *p) for p in pairs)
    zones = cover_zones(a.game)
    out += struct.pack('<2I', len(zones), sum(len(z[2]) for z in zones))
    first = 0
    for box, height, points in zones:
        out += struct.pack('<5f2H', *box, height, first, len(points))
        first += len(points)
    out += b''.join(struct.pack('<2f', *p) for z in zones for p in z[2])
    (a.out / 'rain.n2rr').write_bytes(out)
    print(f'rain.n2rr: {nodes} nodes, {regions} regions (intensity, threshold): ' + ', '.join(f'{i:.2f}/{t:.2f}' for i, t in pairs)
          + f'; {len(zones)} cover zones')


if __name__ == '__main__':
    main()
