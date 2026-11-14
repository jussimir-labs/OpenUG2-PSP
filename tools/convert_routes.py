#!/usr/bin/env python3
"""Xbox NFSU2 free-roam road network -> PSP road graph roads.n2rd.

TRACKS/ROUTESL4RA/RoutesFreeRoam.bin leaf 0x34121 (Xbox layout verified:
8-byte header; records with name @+8, u16 point counts @+44/+46, points
from +128 every 56 bytes with world X/Y f32 @+4/+8, two u16 lateral
extents @+24/+26 (/256 m) and the height as s16 @+22 (/8 m: the game's
TrackRoutePoint::GetForwardPosition); next record after 140+56*N bytes).

Points of a record are linked in order; record ends are welded to any
point within WELD metres, which forms the junctions. The city GPS uses
the points' own heights. Closed tracks keep the collision-derived heights
used to build their race courses and checkpoints. A city routes file with
no heights also falls back to collision (tools/convert_city.py), following
the layer that keeps each route continuous across bridges. This previously
put 37 of the city's 10,577 points on the wrong layer, including the
U-turn off the airport's ramp 23 m below its actual height.

roads.n2rd: magic, version, nodes, links; nodes x 20 B (GU x y z f32,
u16 left/right extent in cm, u16 first link, u16 link count); links u16.

Leaf 0x34123 holds the city's traffic intersections: 38 records of 0x114 B
(i32 kind @+0, all 1; s16 route numbers @+4/+6, the number being the u16
@+2 of each route record above, not its order in the leaf; s16 point index
on each @+10/+12; the rest is the game's state at run time). Route 0 has
the right of way. Points carry the intersection number in bits 19-24 of
their u32 @+16 and their forward direction as s16/32767 @+28/+30; a
point's road edges lie at -left and +right along (dir.y, -dir.x). Routes
with traffic lanes (477 of the city's 851) have a non-zero byte in the 8
bytes @+124.

roads.n2ti: magic, version, count; per intersection 60 B: f32 y, then for
route 0 and route 1: f32 x z, unit direction dx dz, the stop distances
before the point when driving along +dir and along -dir (to the near edge
of the other road), u16 graph node of the point, u16 pad. GU coordinates.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import struct

from n2city import CityCollision
from n2formats import archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
WELD = 9.0


def read_routes(game, track='L4RA', meta=None, zs=None):
    """Points (x, y, left, right) of each route; meta, when given, gets
    per kept route its number and its points' forward directions; zs, per
    kept route its points' heights (game z, m)."""
    d = archived(game, f'TRACKS/ROUTES{track}/ROUTESFREEROAM.BIN')
    leaf = [d[p:p+n] for t, p, n in chunks(d) if t == 0x34121][0]
    at, routes = 8, []
    while at + 140 <= len(leaf):
        n = struct.unpack_from('<H', leaf, at + 44)[0]
        pts, dirs, hs = [], [], []
        for k in range(n):
            p = at + 128 + 56 * k
            if p + 56 > len(leaf):
                break
            x, y = struct.unpack_from('<2f', leaf, p + 4)
            wl, wr = struct.unpack_from('<HH', leaf, p + 24)
            pts.append((x, y, wl / 256, wr / 256))
            hs.append(struct.unpack_from('<h', leaf, p + 22)[0] / 8)
            dirs.append(tuple(v / 32767 for v in struct.unpack_from('<2h', leaf, p + 28)))
        if len(pts) >= 2:
            routes.append(pts)
            if meta is not None:
                meta.append((struct.unpack_from('<H', leaf, at + 2)[0], dirs))
            if zs is not None:
                zs.append(hs)
        at += 140 + 56 * n
    return routes


def read_intersections(game, track='L4RA'):
    """(route number, point) of route 0 and of route 1 of each traffic
    intersection; [] where the track has none."""
    d = archived(game, f'TRACKS/ROUTES{track}/ROUTESFREEROAM.BIN')
    leaves = [d[p:p+n] for t, p, n in chunks(d) if t == 0x34123]
    if not leaves:
        return []
    leaf, out = leaves[0], []
    for o in range(0, len(leaf) - 0x113, 0x114):
        r0, r1 = struct.unpack_from('<2h', leaf, o + 4)
        p0, p1 = struct.unpack_from('<2h', leaf, o + 10)
        out.append(((r0, p0), (r1, p1)))
    return out


def intersection_side(route, dirs, k, other, odirs, ok, node):
    """One route's half of an intersection, in GU: its point, direction,
    and how far before the point the other road's near edge is, driving
    along the route and against it (the original's stopping line)."""
    x, y = route[k][:2]
    fx, fy = dirs[k]
    qx, qy, wl, wr = other[ok]
    gx, gy = odirs[ok]
    proj = [((qx + w * gy - x) * fx + (qy - w * gx - y) * fy) for w in (-wl, wr)]
    return struct.pack('<6f2H', y, x, fy, fx, max(0.0, -min(proj)), max(0.0, max(proj)), node, 0)


# A car finds the ground from where it stands, as the original's
# TopologyCoordinate::NewGetElevation does: the highest surface at most
# SURFACE_ABOVE over its height and less than SURFACE_ABOVE + SURFACE_BELOW
# under that.
SURFACE_ABOVE, SURFACE_BELOW = 1.0, 3.0
SURFACE_GAP = 5   # m of a layer missing in a row the walk steps over
BROKEN = 1000.0   # cost of a stretch the surface does not carry a car along


def surface_walk(city, ax, ay, az, bx, by):
    """The height a car starting at (ax, ay) on the ground at az reaches at
    (bx, by) going straight, a metre at a time over the surface it stands
    on; None when for more than SURFACE_GAP metres in a row there is ground
    and none of it in its reach (a step up or a drop a car does not drive;
    shorter, a hole in the grid's layer: a street under an elevated road
    lost 3 m of its own under it). Cells without ground keep the height."""
    n = max(1, int(math.hypot(bx - ax, by - ay)))
    h, gap = az, 0
    for k in range(1, n + 1):
        t = k / n
        ls = city.layers(ax + (bx - ax) * t, ay + (by - ay) * t)
        if not ls:
            continue
        reach = [v for v in ls if h - SURFACE_BELOW < v <= h + SURFACE_ABOVE]
        if not reach:
            gap += 1
            if gap > SURFACE_GAP:
                return None
            continue
        h, gap = max(reach), 0
    return h


def heights(route, city):
    """Per point one of the ground layers under it, the whole route at once:
    the chain with the least sum of dz^2 / run between points, so a ramp
    climbs at its slope instead of staying on the street under the bridge
    and stepping up at the end (the layer nearest the previous point alone
    took the street at a ramp's foot: route 509 then crossed the street
    under it at grade, beta issue 27). A step between two layers the
    surface does not join (surface_walk: the ground a car driving from the
    one would stand on at the other) costs BROKEN: a route that ends on a
    ramp's top, over a bank under the bridge the ramp joins, ended on the
    bank, 5 m under the deck, by the points alone (its traffic dived under
    the bridge, beta issue 138). A point with no layer keeps the height
    before it."""
    states = None   # per candidate height: (cost, chain)
    px = py = None
    for x, y, _, _ in route:
        ls = city.layers(x, y)
        if states is None:
            states = [(0.0, [z]) for z in ls or [0.0]]
        elif not ls:
            states = [(c, zs + [zs[-1]]) for c, zs in states]
        else:
            run = max(1.0, math.hypot(x - px, y - py))
            reach = [surface_walk(city, px, py, zs[-1], x, y) for _, zs in states]
            step = lambda z, k: (z - states[k][1][-1]) ** 2 / run + (0.0 if reach[k] is not None and abs(reach[k] - z) < 1.0 else BROKEN)
            best = [min(range(len(states)), key=lambda k: states[k][0] + step(z, k)) for z in ls]
            states = [(states[k][0] + step(z, k), states[k][1] + [z]) for z, k in zip(ls, best)]
        px, py = x, y
    return min(states, key=lambda s: s[0])[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--track', default='L4RA', help='L4RA the city (roads.n2rd); another: roads-<track>.n2rd')
    args = ap.parse_args()
    meta, own = [], []
    routes = read_routes(args.game, args.track, meta, own)
    if args.track != 'L4RA' or not any(z for zs in own for z in zs):
        world = 'city.n2cy' if args.track == 'L4RA' else f'track-{args.track}.n2cy'
        city = CityCollision(args.out / world)   # the v2 collision blobs (base + unit header)
        own = [heights(route, city) for route in routes]
    nodes, links, ends = [], collections.defaultdict(set), []
    for route, zs in zip(routes, own):
        base = len(nodes)
        for (x, y, wl, wr), z in zip(route, zs):
            nodes.append((x, y, z, wl, wr))
        for k in range(len(route) - 1):
            links[base + k].add(base + k + 1)
            links[base + k + 1].add(base + k)
        ends += [base, base + len(route) - 1]
    # Weld route ends to the nearest point of any other route (junctions).
    grid = collections.defaultdict(list)
    for i, (x, y, z, _, _) in enumerate(nodes):
        grid[(int(x // 16), int(y // 16))].append(i)
    welded = 0
    for e in ends:
        x, y, z = nodes[e][:3]
        best = None
        for gx in range(int(x // 16) - 1, int(x // 16) + 2):
            for gy in range(int(y // 16) - 1, int(y // 16) + 2):
                for i in grid[(gx, gy)]:
                    if i == e or i in links[e]:
                        continue
                    d = math.hypot(nodes[i][0] - x, nodes[i][1] - y)
                    if d < WELD and abs(nodes[i][2] - z) < 2.5 and (best is None or d < best[0]):
                        best = (d, i)
        if best:
            links[e].add(best[1])
            links[best[1]].add(e)
            welded += 1
    # Crossings: segments of different routes that intersect at the same
    # height (|dz| < 1.5 m) are split at a new junction node (streets that
    # cross mid-route otherwise never connect).
    route_of, k = [], 0
    for r, route in enumerate(routes):
        route_of += [r] * len(route)
    segs = [(i, j) for i in range(len(nodes)) for j in links[i] if i < j and route_of[i] == route_of[j]]
    sgrid = collections.defaultdict(list)
    for s_, (i, j) in enumerate(segs):
        x0, x1 = sorted((nodes[i][0], nodes[j][0])); y0, y1 = sorted((nodes[i][1], nodes[j][1]))
        for gx in range(int(x0 // 32), int(x1 // 32) + 1):
            for gy in range(int(y0 // 32), int(y1 // 32) + 1):
                sgrid[(gx, gy)].append(s_)
    seen, crossings, split = set(), 0, collections.defaultdict(list)
    for bucket in sgrid.values():
        for a_ in range(len(bucket)):
            for b_ in range(a_ + 1, len(bucket)):
                sa, sb = bucket[a_], bucket[b_]
                key = (min(sa, sb), max(sa, sb))
                if key in seen:
                    continue
                seen.add(key)
                (i, j), (m, n) = segs[sa], segs[sb]
                if route_of[i] == route_of[m] or len({i, j, m, n}) < 4:
                    continue
                p0, p1, q0, q1 = nodes[i], nodes[j], nodes[m], nodes[n]
                d = (p1[0] - p0[0]) * (q1[1] - q0[1]) - (p1[1] - p0[1]) * (q1[0] - q0[0])
                if abs(d) < 1e-9:
                    continue
                t = ((q0[0] - p0[0]) * (q1[1] - q0[1]) - (q0[1] - p0[1]) * (q1[0] - q0[0])) / d
                u = ((q0[0] - p0[0]) * (p1[1] - p0[1]) - (q0[1] - p0[1]) * (p1[0] - p0[0])) / d
                if not (0 < t < 1 and 0 < u < 1):
                    continue
                za, zb = p0[2] + (p1[2] - p0[2]) * t, q0[2] + (q1[2] - q0[2]) * u
                if abs(za - zb) > 1.5:
                    continue                     # over/underpass
                x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
                c = len(nodes)
                nodes.append((x, y, (za + zb) / 2, min(p0[3], q0[3]), min(p0[4], q0[4])))
                split[sa].append((t, c))
                split[sb].append((u, c))
                crossings += 1
    # Each crossed segment now runs through its crossing nodes in order: a
    # car on either street passes the junction node (and claims it), never
    # a direct link beside it.
    for s_, cuts in split.items():
        i, j = segs[s_]
        links[i].discard(j)
        links[j].discard(i)
        chain = [i] + [c for _, c in sorted(cuts)] + [j]
        for a, b in zip(chain, chain[1:]):
            links[a].add(b)
            links[b].add(a)
    if len(nodes) > 65535:
        raise ValueError('too many road nodes for u16 links')
    out = bytearray(struct.pack('<4s3I', b'N2RD', 1, len(nodes), sum(len(v) for v in links.values())))
    flat, first = [], []
    for i in range(len(nodes)):
        first.append(len(flat))
        flat += sorted(links[i])
    for i, (x, y, z, wl, wr) in enumerate(nodes):
        out += struct.pack('<3f2H2H', y, z, x, min(65535, int(wl * 100)), min(65535, int(wr * 100)),
                           first[i], len(links[i]))
    out += struct.pack(f'<{len(flat)}H', *flat)
    name = 'roads' if args.track == 'L4RA' else f'roads-{args.track}'
    (args.out / f'{name}.n2rd').write_bytes(out)
    # The traffic intersections, on the graph nodes of their two points.
    base, at = {}, 0
    for (number, _), route in zip(meta, routes):
        base[number] = (at, route)
        at += len(route)
    dirs = {number: d for number, d in meta}
    xout = bytearray()
    for (r0, p0), (r1, p1) in read_intersections(args.game, args.track):
        (b0, a), (b1, b) = base[r0], base[r1]
        xout += struct.pack('<f', (nodes[b0 + p0][2] + nodes[b1 + p1][2]) / 2)
        xout += intersection_side(a, dirs[r0], p0, b, dirs[r1], p1, b0 + p0)
        xout += intersection_side(b, dirs[r1], p1, a, dirs[r0], p0, b1 + p1)
    if xout:
        (args.out / f'{name}.n2ti').write_bytes(struct.pack('<4s2I', b'N2TI', 1, len(xout) // 60) + xout)
    degree = collections.Counter(len(links[i]) for i in range(len(nodes)))
    report = dict(routes=len(routes), nodes=len(nodes), links=len(flat), welded_ends=welded, crossings=crossings,
                  degree=dict(sorted(degree.items())), bytes=len(out), intersections=len(xout) // 60)
    (args.out / f'{name}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
