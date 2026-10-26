#!/usr/bin/env python3
"""Xbox NFSU2 free-roam traffic lanes and signposts -> lanes.n2tl, beside
roads.n2rd (run tools/convert_routes.py first: the lanes walk its nodes).

What the data holds (TRACKS/ROUTESL4RA/ROUTESFREEROAM.BIN; offsets from the
start of a route record, which is 8 bytes before the `at` of
convert_routes.py):
- route record: u16 number @+10 (the index the signposts use), s16 point
  count @+0x34, then 8 lane bytes @+0x84: slots 0-3 driving along the
  route, 4-7 against it; non-zero where the lane exists. Slot 3 is the
  outermost (rightmost) lane of its direction. 477 of the 851 routes have
  lanes; the original's traffic drives only those.
- points of 0x38 B from +0x8c: x, y @+0; u32 flags @+0xc (bits 0-10 the
  route number, 19-24 the traffic intersection); u16 left/right extent
  /256 m @+0x14/+0x16; direction s16/32767 @+0x18; two u16 signpost
  indices @+0x1c (0xffff none); per driving line L (0 and 1 the racing
  lines, 2-5 the lanes along, 6-9 against) a signed byte @+0x22+L, the
  line's offset to the right of its own direction as a fraction (/128) of
  the wider extent, and a byte @+0x2c+L, its speed in m/s.
- leaf 0x34122, signposts of 32 B after a 4-byte header: s16 new point
  @+4, old point @+6, new route @+8, old route @+10; u32 flags @+12: bits
  4-7 the traffic sign (2 always taken, 4 taken half the time, 5 and 10
  never, 7 ignored), bit 18 the sign applies driving against the old
  route, bit 23 the new route is driven the other way round, bit 26 an
  optional sign taken only from the outermost lane, into the same lane.

lanes.n2tl (little endian): magic, version, routes, chain entries, signs,
lane-data bytes (u32 each); then per route 16 B: u16 first chain entry,
u16 entries, u8 lane mask (bit s = slot s exists), u8 lanes, u16 first
sign, u16 signs, u16 route number, u32 byte offset of its lane data; per
chain entry a u16 roads.n2rd node: the route's points in order with the
graph's crossing nodes between them; per sign 8 B: u16 chain entry where
it stands, u16 new route (index in this file), u16 chain entry it leads
to, u8 traffic sign type, u8 flags (1 against the old route, 2 the new
route the other way, 4 outermost lane only), sorted by entry; then the
lane data: per route, per chain entry, per existing slot in slot order,
s8 offset (1/8 m, right of the slot's direction) and u8 speed (m/s).
"""
import argparse
import collections
import json
import math
from pathlib import Path
import struct

from n2formats import archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]


def read_leaves(game, track):
    d = archived(game, f'TRACKS/ROUTES{track}/ROUTESFREEROAM.BIN')
    return {t: d[p:p+n] for t, p, n in chunks(d)}


def read_routes(leaf):
    """Per route as convert_routes.py keeps them (two points or more, in
    file order): number, lane bytes, points (x, y, wl, wr, signposts, line
    offsets, line speeds)."""
    at, out = 0, []
    while at + 0x8c <= len(leaf):
        n = struct.unpack_from('<h', leaf, at + 0x34)[0]
        number = struct.unpack_from('<H', leaf, at + 10)[0]
        pts = []
        for k in range(n):
            p = at + 0x8c + 0x38 * k
            if p + 0x38 > len(leaf):
                break
            x, y = struct.unpack_from('<2f', leaf, p)
            wl, wr = struct.unpack_from('<2H', leaf, p + 0x14)
            signs = [s for s in struct.unpack_from('<2H', leaf, p + 0x1c) if s != 0xffff]
            offs = struct.unpack_from('<10b', leaf, p + 0x22)
            speeds = leaf[p + 0x2c:p + 0x36]
            pts.append((x, y, wl / 256, wr / 256, signs, offs, speeds))
        if len(pts) >= 2:
            out.append(dict(number=number, lanes=bytes(leaf[at + 0x84:at + 0x8c]), pts=pts))
        at += 0x8c + 0x38 * n
    return out


def read_signs(leaf):
    out = []
    for o in range(4, len(leaf) - 31, 32):
        newp, oldp, newr, oldr = struct.unpack_from('<4h', leaf, o + 4)
        flags = struct.unpack_from('<I', leaf, o + 12)[0]
        out.append(dict(newp=newp, oldp=oldp, newr=newr, oldr=oldr, kind=flags >> 4 & 15,
                        back=flags >> 18 & 1, flip=flags >> 23 & 1, outer=flags >> 26 & 1))
    return out


def read_graph(path):
    d = path.read_bytes()
    magic, version, n, nl = struct.unpack_from('<4s3I', d, 0)
    if magic != b'N2RD' or version != 1:
        raise ValueError(f'{path}: not a roads.n2rd version 1')
    nodes = [struct.unpack_from('<3f4H', d, 16 + 20 * i) for i in range(n)]
    flat = struct.unpack_from(f'<{nl}H', d, 16 + 20 * n)
    links = [flat[f:f + c] for (_, _, _, _, _, f, c) in nodes]
    return nodes, links


def chain_of(base, count, nodes, links, npts):
    """The route's nodes in order: its points and, between two of them, the
    crossing nodes convert_routes.py split the segment at: a path of
    crossing nodes from one point to the next that only moves forward
    along the segment (a route lying over another one has its crossings
    on the same line, but its chain leads to its own points)."""
    out = []
    for k in range(count - 1):
        a, b = base + k, base + k + 1
        ax, az = nodes[a][0], nodes[a][2]
        dx, dz = nodes[b][0] - ax, nodes[b][2] - az
        ln = math.hypot(dx, dz) or 1.0

        def along(c):
            t = ((nodes[c][0] - ax) * dx + (nodes[c][2] - az) * dz) / ln
            lat = abs((nodes[c][0] - ax) * dz - (nodes[c][2] - az) * dx) / ln
            return t if lat < 0.5 and -0.01 <= t <= ln + 0.01 else None

        def walk(cur, t0, depth, seen):
            if b in links[cur]:
                return [cur]
            if depth > 64:
                return None
            for t, c in sorted((along(c), c) for c in links[cur] if c >= npts and along(c) is not None):
                if t > t0 - 1e-3 and c not in seen:   # crossings of several routes can share a spot
                    rest = walk(c, t, depth + 1, seen | {c})
                    if rest:
                        return [cur] + rest
            return None

        path = walk(a, -1.0, 0, {a})
        if path is None:
            raise ValueError(f'route chain broken between nodes {a} and {b}')
        out += path
    out.append(base + count - 1)
    return out


def lane_bytes(pt, slot):
    """s8 offset (1/8 m) and u8 speed of a lane slot at a route point."""
    line = 2 + slot
    frac = pt[5][line] / 128
    off = max(pt[2], pt[3]) * frac
    return max(-127, min(127, round(off * 8))), pt[6][line]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--track', default='L4RA')
    args = ap.parse_args()
    leaves = read_leaves(args.game, args.track)
    routes = read_routes(leaves[0x34121])
    signs = read_signs(leaves.get(0x34122, b'\0' * 4))
    name = 'roads' if args.track == 'L4RA' else f'roads-{args.track}'
    nodes, links = read_graph(args.out / f'{name}.n2rd')
    npts = sum(len(r['pts']) for r in routes)
    base = 0
    for r in routes:
        r['base'] = base
        base += len(r['pts'])
        # The graph's node for the point is the route point itself (y, x in GU).
        x, y = r['pts'][0][:2]
        node = nodes[r['base']]
        if abs(node[0] - y) > 0.01 or abs(node[2] - x) > 0.01:
            raise ValueError(f'roads node {r["base"]} is not route {r["number"]}\'s first point')
    kept = [r for r in routes if any(r['lanes'])]
    index = {r['number']: i for i, r in enumerate(kept)}
    chain, lane_data, table, sign_out = [], bytearray(), [], []
    entry_of = {}
    for i, r in enumerate(kept):
        nodes_r = chain_of(r['base'], len(r['pts']), nodes, links, npts)
        slots = [s for s in range(8) if r['lanes'][s]]
        first = len(chain)
        k_of = {}
        for e, nd in enumerate(nodes_r):
            if nd < npts:
                k_of[nd - r['base']] = first + e
        for e, nd in enumerate(nodes_r):
            if nd < npts:
                pt = r['pts'][nd - r['base']]
                vals = [lane_bytes(pt, s) for s in slots]
            else:
                # A crossing node: the lanes as at the points on either side,
                # weighted by distance along the segment.
                prev = max(k for k, ee in k_of.items() if ee - first < e)
                nxt = prev + 1
                pa, pb = nodes[r['base'] + prev], nodes[r['base'] + nxt]
                seg = math.hypot(pb[0] - pa[0], pb[2] - pa[2]) or 1.0
                t = math.hypot(nodes[nd][0] - pa[0], nodes[nd][2] - pa[2]) / seg
                va = [lane_bytes(r['pts'][prev], s) for s in slots]
                vb = [lane_bytes(r['pts'][nxt], s) for s in slots]
                vals = [(round(a[0] + (b[0] - a[0]) * t), min(a[1], b[1])) for a, b in zip(va, vb)]
            for o, sp in vals:
                lane_data += struct.pack('<bB', o, sp)
            chain.append(nd)
        entry_of[r['number']] = k_of
        table.append([first, len(nodes_r), sum(1 << s for s in slots), len(slots), 0, 0, r['number'],
                      len(lane_data) - len(nodes_r) * 2 * len(slots)])
    kinds = collections.Counter()
    for s in signs:
        if s['kind'] not in (2, 4) or s['oldr'] not in index or s['newr'] not in index:
            continue
        old, new = entry_of[s['oldr']], entry_of[s['newr']]
        if s['oldp'] not in old or s['newp'] not in new:
            continue
        kinds[s['kind']] += 1
        sign_out.append((old[s['oldp']], index[s['newr']], new[s['newp']], s['kind'],
                         s['back'] | s['flip'] << 1 | s['outer'] << 2))
    sign_out.sort()
    at = 0
    for i, row in enumerate(table):
        lo, hi = row[0], row[0] + row[1]
        while at < len(sign_out) and sign_out[at][0] < lo:
            at += 1
        row[4] = at
        while at < len(sign_out) and sign_out[at][0] < hi:
            at += 1
        row[5] = at - row[4]
    if len(chain) > 65535 or len(sign_out) > 65535:
        raise ValueError('too many lane entries for u16 indices')
    out = bytearray(struct.pack('<4s5I', b'N2TL', 1, len(table), len(chain), len(sign_out), len(lane_data)))
    for row in table:
        out += struct.pack('<2H2B3HI', *row)
    out += struct.pack(f'<{len(chain)}H', *chain)
    for s in sign_out:
        out += struct.pack('<3H2B', *s)
    out += lane_data
    (args.out / 'lanes.n2tl').write_bytes(out)
    report = dict(routes=len(table), entries=len(chain), crossing_entries=sum(1 for c in chain if c >= npts),
                  signs=len(sign_out), always=kinds[2], optional=kinds[4], lane_bytes=len(lane_data), bytes=len(out))
    (args.out / 'lanes.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
