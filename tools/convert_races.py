#!/usr/bin/env python3
"""Xbox NFSU2 race events -> PSP race courses races.n2rc.

The event catalog (leaf 0x3414c of any TRACKS/ROUTESL4RA/Paths<id>.bin,
272-byte records: u16 id, u8 outline points, u8 circuit flag, u8 flag,
u8 length hint, then 33 x (f32 X, f32 Y)) gives each course as an ordered
outline. Every outline point is snapped to the free-roam road graph
(roads.n2rd) and consecutive points are joined by shortest paths, giving
a drivable polyline; the outline points become the checkpoint gates.

Some events run on roads closed in free roam (e.g. 4002/4005 near the
stadium). Each Paths<id>.bin also has leaf 0x34148: the event's race road
segments, 24-byte nodes (f32 X, f32 Y, u8 segment, u8, s16, 3 x s16 links
to other nodes of the file, u16, f32 distance metric). They are added to
the graph for that event only, welded to free-roam nodes nearby; heights
come from the city collision grid (city.n2cy), resolved across stacked
roads by continuity along the race links.

The start and finish lines and the grid are the game's own position
markers (Tracks\\Routes<track>\\TrackPosMarkersAll.bin, chunk 0x34146:
ForwardStartLine, ForwardFinishLine, their Backward* for the other way and
8 ForwardStartingPosition and BackwardStartingPosition slots an event). A
sprint is laid from just behind its start line to past its finish line; a
circuit whose start line lies off its course is laid again from the line.
Each course then starts at its start line (point 0) and a sprint runs
RUNOUT m past its finish line, where its last gate stands.

races.n2rc: magic, version, events; per event: u16 id, u8 circuit, u8
hint, u16 points, u16 gates, f32 length (the course's own), then points x
GU xyz f32 and gates x u16 point index (padded to 4 bytes).
marks.n2mk (marks-<track>.n2mk beside races-<track>.n2rc): "N2MK", version
1, events; per event (marks_record) the finish line's distance, the other
way's lines and the grid's slots.
"""
import argparse
import collections
import heapq
import json
import math
from pathlib import Path
import struct

from n2city import CityCollision
from n2formats import archived, chunks
import n2routes
import n2source

ROOT = Path(__file__).resolve().parents[1]


def read_catalog(game, track='L4RA'):
    """The track's event catalog, from its first PATHS file."""
    from n2formats import game_hash
    z = (game / 'ZDIR.BIN').read_bytes()
    have = {e[0] for e in struct.iter_unpack('<6I', z)}
    first = next(e for e in range(4000, 4800) if game_hash(f'TRACKS/ROUTES{track}/PATHS{e}.BIN') in have)
    d = archived(game, f'TRACKS/ROUTES{track}/PATHS{first}.BIN')
    cat = [d[p:p+n] for t, p, n in chunks(d) if t == 0x3414c][0]
    events = []
    for at in range(0, len(cat) - 271, 272):
        eid, npts, circuit, flag, hint = struct.unpack_from('<HBBBB', cat, at)
        pts = [struct.unpack_from('<2f', cat, at + 8 + 8 * k) for k in range(npts)]
        events.append(dict(id=eid, circuit=circuit, hint=hint, outline=pts))
    return events


TRACK = 'L4RA'


def path_exists(game, track, eid):
    from n2formats import game_hash
    z = (game / 'ZDIR.BIN').read_bytes()
    return game_hash(f'TRACKS/ROUTES{track}/PATHS{eid}.BIN') in {e[0] for e in struct.iter_unpack('<6I', z)}


OUTLINE_MISS = 50.0     # m: a closed track's course this far off a point of its outline missed part of the race
                        # (the drifts that drive well miss its coarse corners by 4-35 m; the 4306, 103)
OUTLINE_KEEP = 15.0     # m: its event's routes within this of every point keep to it


def off_outline(ev, line):
    """The farthest a point of the event's outline lies off a line (game X, Y)."""
    def seg(p, a, b):
        ux, uy = b[0] - a[0], b[1] - a[1]
        l2 = ux * ux + uy * uy
        t = 0.0 if l2 < 1e-9 else max(0.0, min(1.0, ((p[0] - a[0]) * ux + (p[1] - a[1]) * uy) / l2))
        return math.hypot(p[0] - a[0] - ux * t, p[1] - a[1] - uy * t)
    ring = list(line) + list(line[:1])
    return max(min(seg(q[:2], ring[k], ring[k + 1]) for k in range(len(ring) - 1)) for q in ev['outline'])


def route_file_exists(game, track, name):
    from n2formats import game_hash
    z = (game / 'ZDIR.BIN').read_bytes()
    return game_hash(f'TRACKS/ROUTES{track}/{name}') in {e[0] for e in struct.iter_unpack('<6I', z)}


def read_roads(path):
    d = path.read_bytes()
    _, _, n, l = struct.unpack_from('<4s3I', d, 0)
    nodes, adj = [], []
    for i in range(n):
        x, y, z, wl, wr, first, count = struct.unpack_from('<3f2H2H', d, 16 + 20 * i)
        nodes.append((x, y, z))
        adj.append((first, count))
    links = struct.unpack_from(f'<{l}H', d, 16 + 20 * n)
    return nodes, [links[f:f+c] for f, c in adj]


def shortest(nodes, links, a, b):
    """Dijkstra with an A* bound on 3D distance."""
    if a == b:
        return [a]
    goal = nodes[b]
    def h(i):
        return math.dist(nodes[i], goal)
    best = {a: 0.0}
    prev = {}
    heap = [(h(a), 0.0, a)]
    while heap:
        _, g, i = heapq.heappop(heap)
        if i == b:
            path = [b]
            while path[-1] != a:
                path.append(prev[path[-1]])
            return path[::-1]
        if g > best.get(i, 1e18):
            continue
        for j in links[i]:
            ng = g + math.dist(nodes[i], nodes[j])
            if ng < best.get(j, 1e18):
                best[j] = ng
                prev[j] = i
                heapq.heappush(heap, (ng + h(j), ng, j))
    return None


def race_roads(game, eid, nodes, links, city=None):
    """Free-roam graph plus the event's own race road segments."""
    try:
        d = archived(game, f'TRACKS/ROUTES{TRACK}/PATHS{eid}.BIN')
    except (KeyError, ValueError):
        return nodes, links
    leaf = [d[p:p+n] for t, p, n in chunks(d) if t == 0x34148]
    if not leaf:
        return nodes, links
    recs = [struct.unpack_from('<2fBBh3hHf', leaf[0], 24 * k) for k in range(len(leaf[0]) // 24)]
    grid = {}
    for k, (x, y, z) in enumerate(nodes):
        grid.setdefault((int(x // 32), int(z // 32)), []).append(k)
    def near(gx, gy, r):
        # GU node: x = game Y, z = game X
        out = []
        for i in range(int(gy // 32) - 1, int(gy // 32) + 2):
            for j in range(int(gx // 32) - 1, int(gx // 32) + 2):
                for k in grid.get((i, j), ()):
                    dd = math.hypot(nodes[k][0] - gy, nodes[k][2] - gx)
                    if dd < r:
                        out.append((dd, k))
        return sorted(out)
    base = len(nodes)
    nodes = list(nodes)
    links = [list(l) for l in links]
    # Ground layers under each node come from the city collision grid
    # (roads stack under overpasses); free-roam node heights stand in
    # outside it. Nodes with a single layer anchor the heights, which then
    # spread along the race links, each node taking the layer closest to
    # the neighbour it was reached from.
    adj = [[] for _ in recs]
    for k, (gx, gy, seg, _, _, l0, l1, l2, _, _) in enumerate(recs):
        if k + 1 < len(recs) and recs[k + 1][2] == seg and math.dist(recs[k][:2], recs[k + 1][:2]) < (300 if TRACK == 'L4RA' else 1000):
            adj[k].append(k + 1)
            adj[k + 1].append(k)
        for l in (l0, l1, l2):
            if 0 <= l < len(recs) and math.dist(recs[k][:2], recs[l][:2]) < 60:
                adj[k].append(l)
                adj[l].append(k)
    layers = []
    for gx, gy, *_ in recs:
        found = city.layers(gx, gy) if city else []
        if not found:
            for dd, k in near(gx, gy, 32):
                if all(abs(nodes[k][1] - f) > 3 for f in found):
                    found.append(nodes[k][1])
        layers.append(found)
    heights = [l[0] if len(l) == 1 else None for l in layers]
    queue = collections.deque(k for k in range(len(recs)) if heights[k] is not None)
    while True:
        while queue:
            k = queue.popleft()
            for j in adj[k]:
                if heights[j] is None:
                    heights[j] = min(layers[j], key=lambda y: abs(y - heights[k])) if layers[j] else heights[k]
                    queue.append(j)
        left = [k for k in range(len(recs)) if heights[k] is None]
        if not left:
            break
        k = left[0]     # no anchor in this part: start from its lowest layer
        heights[k] = layers[k][0] if layers[k] else 0.0
        queue.append(k)
    # A segment's run of consecutive nodes where those heights climb more
    # than a road can (0.35 m a metre plus 0.3 m, as relayer) is redone as
    # a global choice (Viterbi): no such climb, then the smallest changes.
    # Taking the layer nearest the neighbour it was reached from put a
    # deck's ramp down on the street under it (the 4083's, 71 m from its
    # foot) and the ramp's race road broke there, so its course cut a
    # barrier to the next branch instead. Runs without such a climb keep
    # their heights (where two layers lie a metre apart either will do).
    k = 0
    while k < len(recs):
        run = [k]
        while run[-1] + 1 < len(recs) and run[-1] + 1 in adj[run[-1]] and recs[run[-1] + 1][2] == recs[run[-1]][2] \
                and layers[run[-1]] and layers[run[-1] + 1]:
            run.append(run[-1] + 1)
        k = run[-1] + 1
        if len(run) < 2 or all(abs(heights[x] - heights[y]) <= .35 * math.dist(recs[x][:2], recs[y][:2]) + .3
                               for x, y in zip(run, run[1:])):
            continue
        cost, back = [[0.0] * len(layers[run[0]])], []
        for a, b in zip(run, run[1:]):
            d = math.dist(recs[a][:2], recs[b][:2])
            row, brow = [], []
            for v in layers[b]:
                best = min(range(len(layers[a])), key=lambda i: cost[-1][i] + abs(v - layers[a][i])
                           + (1000.0 if abs(v - layers[a][i]) > .35 * d + .3 else 0.0))
                u = layers[a][best]
                row.append(cost[-1][best] + abs(v - u) + (1000.0 if abs(v - u) > .35 * d + .3 else 0.0))
                brow.append(best)
            cost.append(row)
            back.append(brow)
        i = min(range(len(cost[-1])), key=lambda i: cost[-1][i])
        for pos in range(len(run) - 1, -1, -1):
            heights[run[pos]] = layers[run[pos]][i]
            if pos:
                i = back[pos - 1][i]
    for k, (gx, gy, *_) in enumerate(recs):
        nodes.append((gy, heights[k], gx))
        links.append([])
    def join(a, b):
        if a != b and b not in links[a]:
            links[a].append(b)
            links[b].append(a)
    # A closed track's race roads are its whole course (the drag strips
    # space their nodes up to 730 m apart along straight highway): there,
    # consecutive nodes of a segment always join.
    reach, slope = (300, .15) if TRACK == 'L4RA' else (1000, .3)
    for k, (gx, gy, seg, _, _, l0, l1, l2, _, _) in enumerate(recs):
        if (k + 1 < len(recs) and recs[k + 1][2] == seg and math.dist(recs[k][:2], recs[k + 1][:2]) < reach
                and abs(heights[k] - heights[k + 1]) < 3 + slope * math.dist(recs[k][:2], recs[k + 1][:2])):
            join(base + k, base + k + 1)
        for l in (l0, l1, l2):
            if 0 <= l < len(recs) and math.dist(recs[k][:2], recs[l][:2]) < 60 and abs(heights[k] - heights[l]) < 3:
                join(base + k, base + l)
        for dd, j in near(gx, gy, 12):
            if abs(nodes[j][1] - heights[k]) < 3:
                join(base + k, j)
    return nodes, links


def components(nodes, links):
    comp = [-1] * len(nodes)
    sizes = []
    for s in range(len(nodes)):
        if comp[s] >= 0:
            continue
        c = len(sizes)
        stack = [s]
        comp[s] = c
        n = 0
        while stack:
            i = stack.pop()
            n += 1
            for j in links[i]:
                if comp[j] < 0:
                    comp[j] = c
                    stack.append(j)
        sizes.append(n)
    main = max(range(len(sizes)), key=lambda c: sizes[c])
    return [c == main for c in comp]


RACE_ROAD_COST = 1.6     # race-only roads hug medians; use them only to avoid long detours


BLOCKED_COST = 40.0      # extra metres per metre of an edge running into walls
BARRIER_RUN = 100        # blocked metres an edge through an event's barrier counts as (4 km)
MEDIAN_SHIFT = 5.0       # m a race road's edge may slide sideways off a median (nudge goes to 10)


class EdgeCheck:
    """Longest run (m) of an edge's car-wide corridor inside walls or off
    the ground in the city collision grid; cached for the free-roam edges."""

    def __init__(self, city, free_count, barriers=False, median=True, walls=False):
        self.city, self.free_count, self.cache, self.barriers, self.median, self.walls = city, free_count, {}, barriers, median, walls

    def __call__(self, nodes, i, j):
        key = (min(i, j), max(i, j))
        if key in self.cache:
            return self.cache[key]
        a, b = nodes[i], nodes[j]
        # The game's race roads run down a road's middle, on the median of a
        # dual carriageway (the 4083's ramp: two walls 1.7 m apart, 60 m
        # long): an edge touching them counts as free when a parallel line
        # up to MEDIAN_SHIFT m to a side is, as nudge then moves the course.
        shifts = (0.0,) if max(i, j) < self.free_count or not self.median else (0.0, 2.5, -2.5, MEDIAN_SHIFT, -MEDIAN_SHIFT)
        dx, dz = b[0] - a[0], b[2] - a[2]
        l = math.hypot(dx, dz) or 1.0
        sx, sz = -dz / l, dx / l
        best = None
        for o in shifts:
            p = (a[0] + sx * o, a[1], a[2] + sz * o)
            q = (b[0] + sx * o, b[1], b[2] + sz * o)
            run = longest = 0
            for hit in self.city.corridor(p[2], p[0], p[1], q[2], q[0], q[1])[:-1]:
                run = run + 1 if hit else 0
                longest = max(longest, run)
            # barriers: through the event's barriers counts as closed,
            # whatever the run (see lay_course)
            if self.barriers and self.city.crosses_barrier(p[2], p[0], p[1], q[2], q[0], q[1]):
                longest = max(longest, BARRIER_RUN)
            # through a thin wall across the road, or off a step: closed too
            if self.walls and (self.city.crosses_wall(p[2], p[0], p[1], q[2], q[0], q[1]) or
                               self.city.crosses_step(p[2], p[0], p[1], q[2], q[0], q[1])):
                longest = max(longest, BARRIER_RUN)
            best = longest if best is None else min(best, longest)
            if not best:
                break
        if max(i, j) < self.free_count:
            self.cache[key] = best
        return best


def dijkstra(nodes, links, src, targets, limit, race_from=None, check=None):
    """Distances and predecessors from src to each target within limit m;
    edges touching nodes >= race_from cost RACE_ROAD_COST times more and
    edges running into walls (check) BLOCKED_COST per blocked metre."""
    race_from = len(nodes) if race_from is None else race_from
    best, prev, heap, left = {src: 0.0}, {}, [(0.0, src)], set(targets)
    while heap and left:
        g, i = heapq.heappop(heap)
        if g > best.get(i, 1e18) or g > limit:
            continue
        left.discard(i)
        for j in links[i]:
            ng = g + math.dist(nodes[i], nodes[j]) * (RACE_ROAD_COST if max(i, j) >= race_from else 1)
            if check:
                blocked = check(nodes, i, j)
                if blocked >= 2:
                    ng += BLOCKED_COST * blocked
            if ng < best.get(j, 1e18):
                best[j] = ng
                prev[j] = i
                heapq.heappush(heap, (ng, j))
    return best, prev


def course(nodes, links, usable, outline, circuit, race_from=None, check=None):
    """Choose one road node per outline point (the 6 nearest within 60 m of
    the main graph component, plus the 2 nearest within 30 m on each other
    height level, e.g. under an overpass) minimising the total distance."""
    cands = []
    for gx, gy in outline:
        near = sorted((k for k in range(len(nodes)) if usable[k] and abs(nodes[k][0] - gy) < 60 and abs(nodes[k][2] - gx) < 60),
                      key=lambda k: (nodes[k][0] - gy) ** 2 + (nodes[k][2] - gx) ** 2)
        if not near:
            return None
        pick = near[:6]
        for k in near[6:]:
            if (nodes[k][0] - gy) ** 2 + (nodes[k][2] - gx) ** 2 > 900:
                break
            level = [c for c in pick if abs(nodes[c][1] - nodes[k][1]) < 3]
            if len(level) < 2:
                pick.append(k)
        cands.append(pick)
    if circuit and len(cands) > 1:
        # Closed outline: the last point is the first, and the lap must end
        # on the node it started from; try the nearest few start nodes.
        best = None
        for start in cands[0]:
            found = chain_course(nodes, links, outline, [[start]] + cands[1:-1] + [[start]], race_from, check)
            if found and (best is None or found[2] < best[2]):
                best = found
        return best[:2] if best else None
    found = chain_course(nodes, links, outline, cands, race_from, check)
    return found[:2] if found else None


UTURN_COST = 400.0       # metres added for turning back at a gate (dead-end snaps)


def heading(nodes, path, at_end, span=25.0):
    """Horizontal direction of the first (or last) span metres of a path."""
    seq = path[::-1] if at_end else path
    a = nodes[seq[0]]
    for i in seq[1:]:
        b = nodes[i]
        if math.hypot(b[0] - a[0], b[2] - a[2]) >= span:
            break
    d = (b[0] - a[0], b[2] - a[2]) if len(seq) > 1 else (0.0, 0.0)
    if at_end:
        d = (-d[0], -d[1])
    l = math.hypot(*d)
    return (d[0] / l, d[1] / l) if l > 1e-6 else None


def chain_course(nodes, links, outline, cands, race_from, check=None):
    """Shortest chain through one candidate per outline point: (route, gates,
    cost). Turning back at a gate (the path in and out overlapping, as when
    a gate snaps to a dead end or the far carriageway) costs UTURN_COST."""
    cost = [{c: 0.0 for c in cands[0]}]
    back = [{}]
    paths = {}
    for k in range(1, len(cands)):
        cost.append({})
        back.append({})
        straight = math.dist(outline[k - 1], outline[k])
        for a in cands[k - 1]:
            if a not in cost[k - 1]:
                continue
            best, prev = dijkstra(nodes, links, a, cands[k], 8 * straight + 1500, race_from, check)
            into = heading(nodes, paths[(k - 1, a)], True) if (k - 1, a) in paths else None
            for b in cands[k]:
                if b in best:
                    path = [b]
                    while path[-1] != a:
                        path.append(prev[path[-1]])
                    path.reverse()
                    total = cost[k - 1][a] + best[b]
                    out = heading(nodes, path, False) if len(path) > 1 else None
                    if into and out and into[0] * out[0] + into[1] * out[1] < -.7:
                        total += UTURN_COST
                    if total < cost[k].get(b, 1e18):
                        cost[k][b] = total
                        back[k][b] = a
                        paths[(k, b)] = path
        if not cost[k]:
            return None
    end = min(cost[-1], key=cost[-1].get)
    chain = [end]
    for k in range(len(cands) - 1, 0, -1):
        chain.append(back[k][chain[-1]])
    chain.reverse()
    route, gates = [chain[0]], [0]
    for k in range(1, len(chain)):
        route += paths[(k, chain[k])][1:]
        gates.append(len(route) - 1)
    return route, gates, cost[-1][end]


def prune_spurs(route, gates):
    """Drop out-and-back spurs (A, B, A) left where a gate snapped to a
    dead end; gates on a removed node move to the spur's base."""
    i = 1
    while i < len(route) - 1:
        if route[i - 1] == route[i + 1]:
            gates = [g if g < i else (i - 1 if g <= i + 1 else g - 2) for g in gates]
            del route[i:i + 2]
            i = max(1, i - 1)
        else:
            i += 1
    return route, gates


def unkink(pts, gates, circuit, city, span=20.0):
    """Cut tight V kinks (the line turning > 110 degrees within span metres,
    left where a gate snapped beside the road) when the chord between the
    two sides is clear for a car; gates inside move to the chord's start."""
    cut = 0
    i = 1
    while i < len(pts) - 1:
        a = i - 1
        while a > 0 and math.hypot(pts[i][0] - pts[a][0], pts[i][2] - pts[a][2]) < span:
            a -= 1
        b = i + 1
        while b < len(pts) - 1 and math.hypot(pts[b][0] - pts[i][0], pts[b][2] - pts[i][2]) < span:
            b += 1
        u = (pts[i][0] - pts[a][0], pts[i][2] - pts[a][2])
        v = (pts[b][0] - pts[i][0], pts[b][2] - pts[i][2])
        lu, lv = math.hypot(*u), math.hypot(*v)
        if (lu > 1 and lv > 1 and (u[0] * v[0] + u[1] * v[1]) / (lu * lv) < -.34 and b - a >= 2
                and not any(city.corridor(pts[a][2], pts[a][0], pts[a][1], pts[b][2], pts[b][0], pts[b][1]))):
            gates = [g if g <= a else (a if g < b else g - (b - a - 1)) for g in gates]
            del pts[a + 1:b]
            cut += 1
            i = max(1, a)
        else:
            i += 1
    return pts, gates, cut


def straighten(pts, gates, city, reach=40.0, dev=8.0):
    """String-pull the line: from each kept point jump to the farthest point
    within reach metres (along the line) whose chord is clear for a car and
    stays within dev metres of the points it replaces. Removes the zigzags
    between road-graph nodes on different lanes; gates move to the next
    kept point."""
    def off_chord(p, a, b):
        dx, dz = b[0] - a[0], b[2] - a[2]
        l2 = dx * dx + dz * dz
        t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[2] - a[2]) * dz) / l2)) if l2 > 1e-9 else 0.0
        return math.hypot(a[0] + dx * t - p[0], a[2] + dz * t - p[2])
    keep, i = [0], 0
    while i < len(pts) - 1:
        along, j, best = 0.0, i + 1, i + 1
        while j < len(pts) and along + math.dist(pts[j - 1], pts[j]) <= reach:
            along += math.dist(pts[j - 1], pts[j])
            j += 1
        for k in range(j - 1, i + 1, -1):
            a, b = pts[i], pts[k]
            if all(off_chord(pts[m], a, b) <= dev for m in range(i + 1, k)) and \
                    not any(city.corridor(a[2], a[0], a[1], b[2], b[0], b[1])):
                best = k
                break
        keep.append(best)
        i = best
    index = {}
    for n, k in enumerate(keep):
        index[k] = n
    new_gates = []
    for g in gates:
        k = g
        while k not in index:
            k += 1
        new_gates.append(index[k])
    return [pts[k] for k in keep], new_gates, len(pts) - len(keep)


def drop_backtracks(pts, gates, circuit):
    """Closed tracks: drop the points where the line turns back on itself
    (a spike out to a gate snapped a few metres off and back); gates on
    one move to the point before."""
    dropped = 0
    i = 1
    while i < len(pts) - (0 if circuit else 1) and len(pts) > 3:
        a, p, b = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
        u = (p[0] - a[0], p[2] - a[2])
        v = (b[0] - p[0], b[2] - p[2])
        if u[0] * v[0] + u[1] * v[1] < -.5 * math.hypot(*u) * math.hypot(*v) and min(math.hypot(*u), math.hypot(*v)) < 15:
            del pts[i]
            gates = [g if g < i else max(0, g - 1) for g in gates]
            dropped += 1
            i = max(1, i - 1)
        else:
            i += 1
    return pts, gates, dropped


def follow_ground(pts, circuit, city, anchor=None):
    """Closed tracks: the course's heights along the ground, metre by
    metre: of the layers at each metre, the run that moves least up and
    down, where a road rises or falls at most 0.35 m a metre, through the
    anchor (a point's index and the floor it is on: start_anchor; by
    default the first point's height) and back to where it began on a
    circuit. The race nodes' own heights can sit on the wrong layer of a
    bridge (the ground under it), and the line between two far nodes cuts
    through the deck. Where the line between two points leaves the layers
    (it cuts the Parkade's spiral ramp across its well), the height is
    carried over the gap at a cost, and the climb allowed on landing grows
    with the metres carried; the points themselves stay on a layer.
    Returns (points, the largest step past what the metres allowed)."""
    samples = []      # (point index or None, layers)
    for i in range(len(pts) + (1 if circuit else 0)):
        b = pts[i % len(pts)]
        if i == 0:
            samples.append((0, city.layers(b[2], b[0]) or [b[1]]))
            continue
        a = pts[i - 1]
        n = max(1, int(math.hypot(b[0] - a[0], b[2] - a[2])))
        for k in range(1, n + 1):
            t = k / n
            layers = city.layers(a[2] + (b[2] - a[2]) * t, a[0] + (b[0] - a[0]) * t)
            samples.append((i % len(pts) if k == n else None, layers or None))
    at, floor = anchor or (0, pts[0][1])
    pin = next(j for j, (i, _) in enumerate(samples) if i == at)
    pinned = min(samples[pin][1] or [floor], key=lambda v: abs(v - floor))
    BREAK, CARRY, OFF_POINT = 1000.0, 3.0, 50.0
    best = None
    for s0 in samples[0][1]:
        # state: (height, carried) -> (cost, metres since the last layer)
        cost = {(v, False): ((0.0 if v == s0 else BREAK) + (BREAK if pin == 0 and v != pinned else 0), 0)
                for v in samples[0][1]}
        back = []     # per sample: {state: (previous state, excess step)}
        for j, (i, layers) in enumerate(samples[1:], 1):
            nxt, links = {}, {}
            def offer(key, c, k, prev, excess):
                if key not in nxt or c < nxt[key][0]:
                    nxt[key], links[key] = (c, k), (prev, excess)
            for (u, carried), (c, k) in cost.items():
                if layers is None:   # no ground: the height goes on as it was
                    offer((u, carried), c, k + 1, (u, carried), 0.0)
                    continue
                allowed = .35 * (k + 1) + .3
                for v in layers:
                    d = abs(v - u)
                    offer((v, False), c + d + (BREAK if d > allowed else 0) + (BREAK if j == pin and v != pinned else 0),
                          0, (u, carried), max(0.0, d - .35 * k))
                offer((u, True), c + CARRY + (OFF_POINT if i is not None else 0) + (BREAK if j == pin else 0), k + 1, (u, carried), 0.0)
            cost = nxt
            back.append(links)
        def closing(key):
            return BREAK if circuit and (key[1] or abs(key[0] - s0) >= .65) else 0
        end = min(cost, key=lambda key: cost[key][0] + closing(key))
        total = cost[end][0] + closing(end)
        if best is None or total < best[0]:
            best = (total, end, back)
    _, key, back = best
    chosen, step = [key[0]], 0.0
    for links in reversed(back):
        key, excess = links[key]
        step = max(step, excess)
        chosen.append(key[0])
    chosen.reverse()
    out = list(pts)
    for (i, layers), h in zip(samples, chosen):
        if i is not None:
            out[i] = (out[i][0], h, out[i][2])
            if layers and not any(abs(h - v) < .1 for v in layers):
                step = max(step, 99.0)   # a point carried over a gap: no course there
    return out, step


def walk_chord(a, b, city):
    """Whether the ground carries a car straight from point a to point b
    (GU x, height, z): every metre a layer within a road's rise of the last
    one, ending on b's."""
    n = max(1, int(math.hypot(b[0] - a[0], b[2] - a[2])))
    h = a[1]
    for k in range(1, n + 1):
        t = k / n
        layers = city.layers(a[2] + (b[2] - a[2]) * t, a[0] + (b[0] - a[0]) * t)
        if not layers:
            continue
        near = [v for v in layers if abs(v - h) <= .65]
        if not near:
            return False
        h = min(near, key=lambda v: abs(v - h))
    return abs(h - b[1]) <= .65


def ramp_path(a, b, city, margin=60.0, step=6.0):
    """The points (GU x, height, z), every ~step metres, of a way over the
    ground from a to b (A* on the 1 m cells, a layer at most a road's rise
    from the last, no walls), or None. The Parkade's courses join their
    points with straight lines that cut its spiral ramp across the well;
    this follows the ramp round instead."""
    x0, x1 = min(a[0], b[0]) - margin, max(a[0], b[0]) + margin
    z0, z1 = min(a[2], b[2]) - margin, max(a[2], b[2]) + margin
    start = (round(a[0]), round(a[2]), round(a[1], 1))
    goal = (round(b[0]), round(b[2]))
    def h(n):
        return math.hypot(n[0] - goal[0], n[1] - goal[1])
    seen, came = {start: 0.0}, {start: None}
    heap = [(h(start), 0.0, start)]
    found = None
    while heap and len(seen) < 400000:
        _, g, n = heapq.heappop(heap)
        if g > seen.get(n, math.inf):
            continue
        if (n[0], n[1]) == goal and abs(n[2] - b[1]) <= .65:
            found = n
            break
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            x, z = n[0] + dx, n[1] + dz
            if not (x0 <= x <= x1 and z0 <= z <= z1):
                continue
            d = math.hypot(dx, dz)
            for v in city.layers(z + .5, x + .5):
                if abs(v - n[2]) > .35 * d + .3 or city._walled(z + .5, x + .5, v):
                    continue
                m = (x, z, round(v, 1))
                g2 = g + d
                if g2 < seen.get(m, math.inf):
                    seen[m], came[m] = g2, n
                    heapq.heappush(heap, (g2 + h(m), g2, m))
    if found is None:
        return None
    path, n = [], found
    while n is not None:
        path.append(n)
        n = came[n]
    path.reverse()
    out, run = [], 0.0
    for p, q in zip(path, path[1:]):
        run += math.hypot(q[0] - p[0], q[1] - p[1])
        if run >= step and q is not path[-1]:
            out.append((q[0] + .5, q[2], q[1] + .5))
            run = 0.0
    return out


def thread_ramps(pts, gates, circuit, city):
    """Closed tracks: where the line between two points of the course
    leaves the ground (walk_chord), the points of the way over the ground
    between them (ramp_path) go in. Returns (points, gates, metres added)."""
    out, new_gates, added = [], [], 0
    index = {}
    for i, p in enumerate(pts):
        index[i] = len(out)
        out.append(p)
        if i + 1 >= len(pts) and not circuit:
            break
        q = pts[(i + 1) % len(pts)]
        if not walk_chord(p, q, city):
            way = ramp_path(p, q, city)
            if way:
                out.extend(way)
                added += len(way)
    new_gates = [index[g] for g in gates]
    return out, new_gates, added


def start_anchor(pts, start, city):
    """Closed tracks: (index of the course point nearest the game's start
    marker, the floor the marker stands over) for follow_ground, or None.
    The game puts each car on the topology below its marker
    (CalculateRacerPositions); the race nodes' own heights sit on the ground
    floor of the multi-storey car park, and the courses kept every Parkade
    drift (4301-4306) there, inside its dark ground floor, where the PS2
    races them on its ramps and decks (the 4301 at 29 m, lit, its camera
    at 32.7 m; the 4304 on the roof, 41 m). start: a marker (GU x, y, z,
    yaw)."""
    floor = max((v for v in city.layers(start[2], start[0]) if v <= start[1] + 1.0), default=None)
    if floor is None:
        return None
    k = min(range(len(pts)), key=lambda i: math.hypot(pts[i][0] - start[0], pts[i][2] - start[2]))
    here = city.layers(pts[k][2], pts[k][0])
    if math.hypot(pts[k][0] - start[0], pts[k][2] - start[2]) > 100 or not any(abs(v - floor) <= 1.0 for v in here):
        return None
    return k, floor


def nudge(pts, gates, circuit, city, walls=False):
    """Where the course line still runs into walls, split those segments
    into <= 6 m pieces and slide each point sideways (up to 10 m) to the
    spot whose two neighbouring segments cross the fewest blocked metres.
    Through one of the event's barriers counts as BARRIER_RUN metres: a
    1 m wall on the diagonal is only a metre of the corridor, and the
    4083's course cut the tip of the barrier between two branches of a
    deck 3 m short of going round it."""
    def blocked(a, b):
        return sum(city.corridor(a[2], a[0], a[1], b[2], b[0], b[1])) + \
            (BARRIER_RUN if city.crosses_barrier(a[2], a[0], a[1], b[2], b[0], b[1]) else 0) + \
            (BARRIER_RUN if walls and (city.crosses_wall(a[2], a[0], a[1], b[2], b[0], b[1])
                                       or city.crosses_step(a[2], a[0], a[1], b[2], b[0], b[1])) else 0)
    out, index = [], []
    count = len(pts) if circuit else len(pts) - 1
    for i in range(len(pts)):
        index.append(len(out))
        out.append(pts[i])
        if i < count:
            a, b = pts[i], pts[(i + 1) % len(pts)]
            if blocked(a, b):
                n = int(math.dist(a, b) // 6) + 1
                out += [tuple(a[c] + (b[c] - a[c]) * k / n for c in range(3)) for k in range(1, n)]
    pts, new_gates = out, [index[g] for g in gates]
    moved = 0
    for _ in range(3):
        for i in range(len(pts)):
            if not circuit and (i == 0 or i == len(pts) - 1):
                continue
            a, p, b = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
            cost = blocked(a, p) + blocked(p, b)
            if not cost:
                continue
            dx, dz = b[0] - a[0], b[2] - a[2]
            l = math.hypot(dx, dz) or 1
            sx, sz = -dz / l, dx / l
            best, best_cost = p, cost
            for off in sorted(range(-10, 11), key=abs):
                q = (p[0] + sx * off, p[1], p[2] + sz * off)
                if off and not city.blocked(q[2], q[0], q[1]):
                    c = blocked(a, q) + blocked(q, b)
                    if c < best_cost:
                        best, best_cost = q, c
            if best is not p:
                pts[i] = best
                moved += 1
    return pts, new_gates, moved


def centre_in_channel(pts, city, reach=12.0):
    """Each point to the middle of the free run across the course there
    (sampled every 0.5 m up to reach either side), when that run is a
    walled channel no wider than 2 x reach: a drag's four lanes span 11 m,
    and L4RB's runway lanes are 15 m channels whose race nodes run along
    one wall. Returns the points and how many moved."""
    out, moved = [], 0
    for i, p in enumerate(pts):
        a, b = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
        tx, tz = b[0] - a[0], b[2] - a[2]
        tl = math.hypot(tx, tz) or 1
        nx, nz = -tz / tl, tx / tl
        def free(o):
            return not city.blocked(p[2] + nz * o, p[0] + nx * o, p[1])
        if not free(0):
            out.append(p)
            continue
        lo = hi = 0.0
        while lo > -reach and free(lo - .5):
            lo -= .5
        while hi < reach and free(hi + .5):
            hi += .5
        if lo <= -reach or hi >= reach or hi - lo > 18:
            out.append(p)     # a road, not a lane channel
            continue
        o = (lo + hi) / 2
        if abs(o) > .25:
            moved += 1
        out.append((p[0] + nx * o, p[1], p[2] + nz * o))
    return out, moved


def trim_walled_ends(pts, gates, city, reach=60.0, margin=15.0):
    """A course whose first (or last) reach metres cross a wall starts
    margin metres past the last such wall (ends margin before the first):
    L4RB's runway drags have their first race node 45 m behind the wall
    that closes the lane channel (a 0x16 world border). Returns the points,
    the gates and the metres cut."""
    def walk(seq):
        """Distance along seq of the last blocked metre within reach, past
        free ground: a course whose end lies where the collision has no
        floor at its height (the drags along L4RB's terrain) is no wall."""
        d, last, free = 0.0, None, False
        # across the course's way over its first 30 m (a nudged point can
        # turn one segment 35 degrees)
        far = next((q for q in seq if math.hypot(q[0] - seq[0][0], q[2] - seq[0][2]) > 30), seq[-1])
        w = math.hypot(far[0] - seq[0][0], far[2] - seq[0][2]) or 1
        nx, nz = -(far[2] - seq[0][2]) / w, (far[0] - seq[0][0]) / w
        for a, b in zip(seq, seq[1:]):
            l = math.hypot(b[0] - a[0], b[2] - a[2])
            n = max(1, int(l * 2))     # every 0.5 m: a wall is one 1 m cell thick
            for k in range(n):
                t = k / n
                x, y, z = a[2] + (b[2] - a[2]) * t, a[1] + (b[1] - a[1]) * t, a[0] + (b[0] - a[0]) * t
                # a wall across the way: 5 m of it in a row within 7.5 m of
                # the line (a wall along the line hits one offset only). Not
                # at the line itself: the runway's race nodes slip round the
                # closing wall's end, 2 m outside the lane channel
                row = [city.blocked(x + nz * o, z + nx * o, y) for o in (-7.5, -5, -2.5, 0, 2.5, 5, 7.5)]
                if free and any(all(row[j:j + 3]) for j in range(5)):
                    last = d + l * t
                free = free or not any(row[2:5])   # 5 m of open road
            d += math.hypot(b[0] - a[0], b[2] - a[2])
            if d > reach:
                break
        return last
    def cut(seq, at):
        """seq from the point at metres along it; (points, index offset)."""
        d = 0.0
        for i, (a, b) in enumerate(zip(seq, seq[1:])):
            l = math.hypot(b[0] - a[0], b[2] - a[2])
            if d + l > at:
                t = (at - d) / l
                return [tuple(a[c] + (b[c] - a[c]) * t for c in range(3))] + seq[i + 1:], i
            d += l
        return seq, 0
    total = 0.0
    head = walk(pts)
    if head is not None:
        pts, off = cut(pts, head + margin)
        gates = [0] + [g - off for g in gates[1:] if g - off > 0]
        total += head + margin
    tail = walk(pts[::-1])
    if tail is not None:
        rev, off = cut(pts[::-1], tail + margin)
        n = len(pts)
        pts = rev[::-1]
        gates = [g for g in gates[:-1] if g < len(pts) - 1] + [len(pts) - 1]
        total += tail + margin
    return pts, gates, total


def relayer(pts, circuit, city):
    """Each course point on the ground layer that makes the course
    drivable: a global choice (Viterbi) among the layers under each point
    minimising the blocked metres between points, steps no road climbs
    (more than 0.35 m a metre plus 0.3 m) and, last, the distance from the
    height the point had. Under an overpass a course's heights could stay
    on the street below while the race runs on the deck (walls across the
    street there). Returns the points and how many changed layer."""
    if len(pts) < 3:
        return pts, 0
    def blocked(a, b):
        return sum(city.corridor(a[2], a[0], a[1], b[2], b[0], b[1]))
    cands = []
    for p in pts:
        layers = [v for v in city.layers(p[2], p[0])] or [p[1]]
        if all(abs(v - p[1]) > .5 for v in layers):
            layers.append(p[1])
        cands.append(layers)
    INF = 1e18
    cost = [[abs(v - pts[0][1]) * .01 for v in cands[0]]]
    back = [[-1] * len(cands[0])]
    for i in range(1, len(pts)):
        a, b = pts[i - 1], pts[i]
        dist = math.hypot(b[0] - a[0], b[2] - a[2])
        row, brow = [], []
        for v in cands[i]:
            best, arg = INF, -1
            for j, u in enumerate(cands[i - 1]):
                if cost[i - 1][j] >= INF:
                    continue
                step = abs(v - u)
                c = cost[i - 1][j] + (1000.0 if step > .35 * dist + .3 else 0.0)
                if c < best:
                    c += blocked((a[0], u, a[2]), (b[0], v, b[2]))
                    if c < best:
                        best, arg = c, j
            row.append(best + abs(v - b[1]) * .01)
            brow.append(arg)
        cost.append(row)
        back.append(brow)
    k = min(range(len(cands[-1])), key=lambda j: cost[-1][j])
    pick = [0] * len(pts)
    for i in range(len(pts) - 1, -1, -1):
        pick[i] = k
        k = back[i][k] if i else k
    out, changed = [], 0
    for p, layers, j in zip(pts, cands, pick):
        v = layers[j]
        if abs(v - p[1]) > .5:
            changed += 1
        out.append((p[0], v, p[2]))
    return out, changed


def longest_detour(laid, other):
    """How long a course may get to keep off barriers and walls: 10% over
    the one it replaces, or 1.3 times the outline (a barrier closing a road
    calls for the detour: the 4144's 1.2). When the one it replaces runs
    through barriers or walls (no car gets through) and the other through
    none, with nothing blocked, up to twice the outline (the 4178's 1.6:
    two barriers in a row close its short way)."""
    if (laid['crossings'] or laid['faults']) and not (other['crossings'] or other['faults'] or other['blocked']):
        return laid['outline'] * 2
    return max(laid['length'] * 1.1, laid['outline'] * 1.3)


REFERENCE = json.loads((ROOT / 'tools/race_reference.json').read_text())


def follow_reference(pts, gates, ref, city):
    """The course along a stretch measured on the original game
    (race_reference.json): the points between the ones nearest the
    stretch's ends give way to it, and the gates among them move to its
    nearest point. Heights: the ground layer under the bodies (half a
    metre below them). Returns the points, the gates and how many points
    came from the stretch."""
    def near(p):
        return min(range(len(pts)), key=lambda k: math.dist((pts[k][2], pts[k][0]), p[:2]))
    i0, i1 = near(ref[0]), near(ref[-1])
    if i1 <= i0:
        return pts, gates, 0
    new = []
    for x, y, z in ref:
        ls = city.layers(x, y)
        new.append((y, min(ls, key=lambda h: abs(h - (z - .5))) if ls else z - .5, x))
    shift = len(new) - (i1 + 1 - i0)

    def gate(g):
        if g < i0:
            return g
        if g > i1:
            return g + shift
        return i0 + min(range(len(new)), key=lambda k: math.dist(new[k], pts[g]))
    return pts[:i0] + new + pts[i1 + 1:], sorted(set(gate(g) for g in gates)), len(new)


def read_route_net(game, track, name='ROUTESFREEROAM.BIN'):
    """The free-roam routes (or an event's own, name) and their signposts
    as RacerWalk drives them (n2routes): ({route: [(x, y, racing line
    offset m, dx, dy)]}, {(route, point): [[new route, new point,
    backwards, flip, type]]})."""
    routes = {r: [(p['x'], p['y'], p['offset'], p['dx'], p['dy']) for p in pts]
              for r, pts in n2routes.read_routes(game, track, name).items()}
    signs = collections.defaultdict(list)
    for s in n2routes.read_signposts(game, track, name):
        newr, newp, oldr, oldp = s['new_route'], s['new_point'], s['old_route'], s['old_point']
        if newr in routes and oldr in routes and newp < len(routes[newr]) and oldp < len(routes[oldr]) and newr != oldr:
            signs[(oldr, oldp)].append([newr, newp, s['backwards'], s['flip'], s['racer_type']])
    return routes, signs


def segments_cross(p, q, a, b):
    def side(o, u, v):
        return (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0])
    return side(p, q, a) * side(p, q, b) < 0 and side(a, b, p) * side(a, b, q) < 0


SIGN_ALWAYS, SIGN_OPTIONAL = 2, 4
WALK_AHEAD = 30.0        # m a racer looks down each exit of a fork (the game's MostFruitfulExit)
WALK_BARRIER = 200.0     # m an exit is followed looking for barriers (WillCrossBarrier)
WALK_NEAR = 15.0         # m off the course an exit may end WALK_AHEAD on
WALK_MARGIN = 10.0       # m of progress an optional exit must gain over the way on
WALK_LOST = 25.0         # m off the course where the walk starts again
WALK_JOIN = 200.0        # m of course between two such stretches that go as one


class RacerWalk:
    """How the original game's racers drive a course over the free-roam
    routes. The routes give each carriageway of a dual road its own route;
    signposts join them. A racer crosses a sign of type 2 ("always") as it
    comes to it and keeps to its route past an optional one (type 4: taken
    at random, half the time, and never while the start's grace period
    lasts); types 5, 7, 8 and 10 are never taken. At loading the game fixes
    the signs against the event's barriers (EliminateBarrierViolations):
    a sign whose exit runs into a barrier within 200 m, before the next
    fork, is never taken; an optional one whose way on does and whose exit
    does not becomes "always". Here the race's course decides the rest: at
    a fork the way on is kept while 30 m down it stays near the course and
    gains as much on it as the best exit (within WALK_MARGIN), as the game
    keeps racers on the routes near its race path; otherwise the exit that
    gains most there (the game's MostFruitfulExit, ties to the first sign)."""

    def __init__(self, routes, signs, barriers):
        self.routes, self.barriers = routes, barriers
        self.signs = {key: [list(s) for s in v] for key, v in signs.items()}
        if barriers:
            for key, v in self.signs.items():
                for s in v:
                    if s[4] in (SIGN_ALWAYS, SIGN_OPTIONAL):
                        new = self.crosses(self.enter(key, s), skip=s, start=self.routes[key[0]][key[1]])
                        old = s[4] == SIGN_OPTIONAL and self.crosses((key[0], key[1], not s[2]), skip=s)
                        if new:
                            s[4] = 7
                        elif old:
                            s[4] = SIGN_ALWAYS

    @staticmethod
    def enter(key, s):
        return (s[0], s[1], (not s[2]) != s[3])

    def departing(self, state):
        """Signs at a point that stand for a car going its way."""
        r, k, fwd = state
        return [s for s in self.signs.get((r, k), ()) if s[2] != fwd]

    def step(self, state, stay=False):
        """The next point (the game's GetNextPointOnSegment): over the first
        "always" sign the car meets, or on along its route."""
        r, k, fwd = state
        if not stay:
            for s in self.departing(state):
                if s[4] == SIGN_ALWAYS:
                    return self.enter((r, k), s)
        k += 1 if fwd else -1
        return (r, k, fwd) if 0 <= k < len(self.routes[r]) else None

    def xy(self, state):
        return self.routes[state[0]][state[1]][:2]

    def way(self, state):
        """The car's heading at a route point (the route's way, or against it)."""
        _, _, _, dx, dy = self.routes[state[0]][state[1]]
        return (dx, dy) if state[2] else (-dx, -dy)

    def far_side(self, p, q, h, city):
        """p (game X, Y) lies off the racers' line at q (x, y, progress,
        heading) towards the other carriageway of a dual road: a route
        beside q, 5-30 m across, running against the racers' heading, with
        a median between (a wall on the straight at height h). A two-way
        street also has a route each way, a few metres apart, with nothing
        between."""
        if not hasattr(self, 'grid'):
            self.grid = collections.defaultdict(list)
            for pts in self.routes.values():
                for x, y, _, dx, dy in pts:
                    self.grid[(int(x // 32), int(y // 32))].append((x, y, dx, dy))
        tx, ty = q[3], q[4]
        vx, vy = p[0] - q[0], p[1] - q[1]
        for i in range(int(q[0] // 32) - 1, int(q[0] // 32) + 2):
            for j in range(int(q[1] // 32) - 1, int(q[1] // 32) + 2):
                for x, y, dx, dy in self.grid.get((i, j), ()):
                    if dx * tx + dy * ty > -.8:
                        continue
                    wx, wy = x - q[0], y - q[1]
                    if abs(wx * tx + wy * ty) <= 20 and 5 <= abs(tx * wy - ty * wx) <= 30 and wx * vx + wy * vy > 0:
                        n = int(math.hypot(wx, wy) * 2)
                        if any(city._walled(q[0] + wx * k / n, q[1] + wy * k / n, h) for k in range(1, n)):
                            return True
        return False

    def line(self, state):
        """The racing line at a route point: its offset to the right of the
        route's way (the same spot whichever way the car goes)."""
        x, y, off, dx, dy = self.routes[state[0]][state[1]]
        return (x + dy * off, y - dx * off)

    def crosses(self, state, skip=None, start=None):
        """A barrier within WALK_BARRIER m along from a point, before the
        next fork (another sign leaving the route the car's way)."""
        if state is None:
            return False
        prev, run, seen = start, 0.0, set()
        while state is not None and run < WALK_BARRIER and state not in seen:
            seen.add(state)
            here = self.xy(state)
            if prev is not None:
                if any(segments_cross(prev[:2], here, a, b) for a, b in self.barriers):
                    return True
                run += math.dist(prev[:2], here)
            if prev is not start and any(s is not skip and s[4] not in (7, 8) for s in self.departing(state)):
                return False
            prev = here
            state = self.step(state)
        return False

    def ahead(self, state, origin, dist=WALK_AHEAD):
        """The point dist m along from origin entering at state."""
        prev, run, seen = origin, 0.0, set()
        while state is not None and state not in seen:
            seen.add(state)
            here = self.xy(state)
            run += math.dist(prev, here)
            prev = here
            if run >= dist:
                break
            state = self.step(state)
        return prev


def event_lap(walk, pts, circuit, city, reach=15.0, step=5.0):
    """A closed track's lap along its event's own routes (ROUTES<id>F.BIN,
    the routes the race loads for its racers): from the route point nearest
    the course's start that runs its way, over each "always" sign the
    racers meet and on along the routes otherwise (RacerWalk.step), on the
    routes' own line (their middle: off it, the racing line's offset took
    the 4306 off its spiral ramp, where no ground carries the course to its
    decks) every step metres, back to where it began. The heights: the
    course's own nearest, on the collision's nearest layer (follow_ground
    lays them again from the start marker).
    (pts, how the old course's points map onto it), or None when the walk
    does not close the lap."""
    prog = CourseProgress(pts, circuit)
    p, _ = prog.at(0.0)
    q, _ = prog.at(15.0)
    h = (q[0] - p[0], q[1] - p[1])
    hl = math.hypot(*h) or 1.0
    seed = None
    for r, rp in walk.routes.items():
        for k, (x, y, _, dx, dy) in enumerate(rp):
            d = math.hypot(x - p[0], y - p[1])
            c = (dx * h[0] + dy * h[1]) / hl
            if d < reach and abs(c) > .7 and (seed is None or d < seed[0]):
                seed = (d, (r, k, c > 0))
    if seed is None:
        return None
    state, seen, line, run = seed[1], set(), [], 0.0
    while state is not None and state not in seen:
        seen.add(state)
        here = walk.xy(state)
        if line:
            a = line[-1]
            n = int(math.dist(a, here) // step)
            line += [(a[0] + (here[0] - a[0]) * m / (n + 1), a[1] + (here[1] - a[1]) * m / (n + 1)) for m in range(1, n + 1)]
            run += math.dist(a, here)
        line.append(here)
        if run > 3 * prog.length:
            return None
        state = walk.step(state)
    if state != seed[1] or len(line) < 3:
        return None
    out, s = [], 0.0
    for x, y in line:
        s, _ = prog.project((x, y), s, back=40.0, fwd=prog.length)
        k = min(range(len(pts)), key=lambda j: math.dist((pts[j][2], pts[j][0]), (x, y)))
        hgt = pts[k][1]
        ls = city.layers(x, y)
        out.append((y, min(ls, key=lambda v: abs(v - hgt)) if ls else hgt, x))
    return out


def simplify_course(pts, gates, flat=.5, rise=.25):
    """The course's points (GU x, y, z) less those its line passes within
    flat metres across and rise metres in height of (Douglas-Peucker), the
    gates' and the first kept; (points, gates on them)."""
    keep = {0, len(pts) - 1} | set(gates)

    def off(p, a, b):
        dx, dz = b[0] - a[0], b[2] - a[2]
        l2 = dx * dx + dz * dz
        t = 0.0 if l2 < 1e-9 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[2] - a[2]) * dz) / l2))
        across = math.hypot(p[0] - a[0] - dx * t, p[2] - a[2] - dz * t)
        return max(across / flat, abs(p[1] - (a[1] + (b[1] - a[1]) * t)) / rise)
    marks = sorted(keep)
    todo = list(zip(marks, marks[1:]))
    while todo:
        i, j = todo.pop()
        if j - i < 2:
            continue
        k = max(range(i + 1, j), key=lambda m: off(pts[m], pts[i], pts[j]))
        if off(pts[k], pts[i], pts[j]) > 1:
            keep.add(k)
            todo += [(i, k), (k, j)]
    order = sorted(keep)
    where = {old: new for new, old in enumerate(order)}
    return [pts[k] for k in order], sorted(where[g] for g in gates)


class CourseProgress:
    """Distance along a course's polyline (game X, Y) of the nearest point,
    searched near a hint (the course can cross itself)."""

    def __init__(self, pts, circuit):
        self.p = [(q[2], q[0]) for q in pts]
        if circuit:
            self.p.append(self.p[0])
        self.cum = [0.0]
        for a, b in zip(self.p, self.p[1:]):
            self.cum.append(self.cum[-1] + math.dist(a, b))
        self.length, self.circuit = self.cum[-1], circuit

    def at(self, s):
        if self.circuit:
            s %= self.length
        s = max(0.0, min(self.length, s))
        i = max(0, min(len(self.p) - 2, next((k for k in range(len(self.cum) - 1) if self.cum[k + 1] >= s), len(self.p) - 2)))
        l = self.cum[i + 1] - self.cum[i]
        t = (s - self.cum[i]) / l if l > 1e-9 else 0.0
        return tuple(self.p[i][c] + (self.p[i + 1][c] - self.p[i][c]) * t for c in range(2)), i

    def project(self, q, hint, back=60.0, fwd=250.0):
        """(progress, distance off) of q, progress unwrapped near hint."""
        best = None
        for i in range(len(self.p) - 1):
            lo, hi = self.cum[i], self.cum[i + 1]
            shift = 0.0
            if self.circuit:
                shift = round((hint - (lo + hi) / 2) / self.length) * self.length
            if hi + shift < hint - back or lo + shift > hint + fwd:
                continue
            a, b = self.p[i], self.p[i + 1]
            dx, dy = b[0] - a[0], b[1] - a[1]
            l2 = dx * dx + dy * dy
            t = max(0.0, min(1.0, ((q[0] - a[0]) * dx + (q[1] - a[1]) * dy) / l2)) if l2 > 1e-9 else 0.0
            off = math.hypot(a[0] + dx * t - q[0], a[1] + dy * t - q[1])
            if best is None or off < best[1]:
                best = (lo + shift + (hi - lo) * t, off)
        return best or (hint, 1e9)


def walk_course(walk, pts, circuit, centre=False, reach=12.0):
    """The racing line the original game's racers drive along a course:
    [(game X, game Y, progress m, heading X, Y)], with None where the walk lost the
    course and started again. centre: the routes' own line instead (the
    road's middle, where a drag's lanes are centred), each point with the
    walk's state (route, point, forward) last."""
    at = walk.xy if centre else walk.line
    prog = CourseProgress(pts, circuit)

    def seed(s):
        p, i = prog.at(s)
        q, _ = prog.at(s + 15)
        h = (q[0] - p[0], q[1] - p[1])
        hl = math.hypot(*h) or 1.0
        best = None
        for r, rp in walk.routes.items():
            for k, (x, y, _, dx, dy) in enumerate(rp):
                d = math.hypot(x - p[0], y - p[1])
                if d < reach and (best is None or d < best[0]):
                    c = (dx * h[0] + dy * h[1]) / hl
                    if abs(c) > .7:
                        best = (d, (r, k, c > 0))
        return best[1] if best else None

    out, s, state = [], 0.0, seed(0.0)
    total = prog.length
    steps = 0
    while s < total - 5 and steps < 5000:
        steps += 1
        if state is None:
            s += 20.0
            out.append(None)
            state = seed(s)
            continue
        here = walk.xy(state)
        ps, off = prog.project(here, s)
        if off > WALK_LOST or ps < s - 40:
            s += 20.0
            out.append(None)
            state = seed(s)
            continue
        s = max(s, ps)
        way = walk.way(state)
        out.append((*at(state), ps, *way) + ((state,) if centre else ()))
        # the ways on from here: the route (or its "always" sign) and the
        # optional exits, judged WALK_AHEAD m down each
        default = walk.step(state)
        options = [(default, True)]
        if default is not None and default[0] != state[0]:
            options.append((walk.step(state, stay=True), False))
        options += [(walk.enter(state[:2], sg), False) for sg in walk.departing(state) if sg[4] == SIGN_OPTIONAL]
        judged = []
        for nxt, is_default in options:
            if nxt is None:
                continue
            q = walk.ahead(nxt, here)
            qs, qoff = prog.project(q, s)
            judged.append((nxt, is_default, qs, qoff))
        near = [j for j in judged if j[3] <= WALK_NEAR]
        best = max((j[2] for j in near), default=None)
        pick = next((j for j in near if j[1] and j[2] >= best - WALK_MARGIN), None)
        if pick is None and near:
            pick = max(near, key=lambda j: j[2])
        if pick is None:
            pick = min(judged, key=lambda j: j[3]) if judged else None
        nxt = pick[0] if pick else None
        if nxt is not None and nxt[0] == state[0] and pick is not None:
            # densify along the route (points can be 50 m apart)
            a, b = at(state), at(nxt)
            n = int(math.dist(a, b) // 5)
            for m in range(1, n + 1):
                t = m / (n + 1)
                p = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                out.append((*p, prog.project(p, s)[0], *way) + ((state,) if centre else ()))
        state = nxt
    return out


def follow_walk(pts, gates, circuit, walked, city, walk):
    """The course along the racers' walk (walk_course) where it lies over
    3 m off the walk towards the other carriageway of a dual road
    (RacerWalk.far_side) or with a wall between the two: each such run
    of course points, out to the points the walk passes within 3 m of,
    gives way to the walk's line between them (runs under WALK_JOIN m
    apart as one, where the course zigzags within 3 m of the line), when
    that mends the course (replace). Where it would only cost nothing, it
    changed 42 courses and moved the rivals' repositions between races.
    Returns the points, the gates and how many came from the walk."""
    if not walked:
        return pts, gates, 0
    runs, cur = [], []
    for w in walked:
        if w is None:
            if len(cur) > 1:
                runs.append(cur)
            cur = []
        else:
            cur.append(w)
    if len(cur) > 1:
        runs.append(cur)

    def nearest(p, run):
        best = None
        for a, b in zip(run, run[1:]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            l2 = dx * dx + dy * dy
            t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2)) if l2 > 1e-9 else 0.0
            q = (a[0] + dx * t, a[1] + dy * t, a[2] + (b[2] - a[2]) * t, a[3], a[4])
            d = math.hypot(q[0] - p[0], q[1] - p[1])
            if best is None or d < best[0]:
                best = (d, q)
        return best

    def apart(p, q, h):
        """A wall on the straight between a course point and the walk."""
        n = max(1, int(math.dist(p, q) * 2))
        hits = [city._walled(p[0] + (q[0] - p[0]) * k / n, p[1] + (q[1] - p[1]) * k / n, h) for k in range(1, n)]
        return any(hits)

    def cost(seq):
        segs = list(zip(seq, seq[1:]))
        return (sum(city.crosses_barrier(a[2], a[0], a[1], b[2], b[0], b[1]) for a, b in segs) +
                sum(city.crosses_wall(a[2], a[0], a[1], b[2], b[0], b[1]) or city.crosses_step(a[2], a[0], a[1], b[2], b[0], b[1])
                    for a, b in segs),
                sum(sum(city.corridor(a[2], a[0], a[1], b[2], b[0], b[1])) for a, b in segs))
    def replace(pts, gates, s_of, run, i0, i1):
        """pts[i0..i1] give way to the walk's line between them, when that
        mends the course there (fewer barriers, walls and steps crossed, or
        as many and fewer blocked metres: cost); None otherwise."""
        if i1 - i0 < 2:
            return None
        mid = [w for w in run if s_of[i0] < w[2] < s_of[i1]]
        if len(mid) < 2:
            return None
        new = []
        for x, y, ws, *_ in mid:
            h = pts[min(range(i0, i1 + 1), key=lambda j: abs(s_of[j] - ws))][1]
            ls = city.layers(x, y)
            new.append((y, min(ls, key=lambda v: abs(v - h)) if ls else h, x))
        if cost([pts[i0]] + new + [pts[i1]]) >= cost(pts[i0:i1 + 1]):
            return None
        shift = len(new) + 2 - (i1 + 1 - i0)

        def gate(g):
            if g <= i0:
                return g
            if g >= i1:
                return g + shift
            return i0 + 1 + min(range(len(new)), key=lambda j: math.dist(new[j], pts[g]))
        return (pts[:i0 + 1] + new + pts[i1:], sorted(set(gate(g) for g in gates)),
                s_of[:i0 + 1] + [w[2] for w in mid] + s_of[i1:], len(new))
    prog = CourseProgress(pts, circuit)
    s_of = []
    for k in range(len(pts)):
        s_of.append(prog.cum[k])
    taken = 0
    for run in runs:
        lo, hi = run[0][2], run[-1][2]
        flags = []
        for k, p in enumerate(pts):
            g = (p[2], p[0])
            if not lo <= s_of[k] <= hi:
                flags.append(None)
                continue
            d, q = nearest(g, run)
            flags.append((d, d > 3 and (apart(g, q[:2], p[1]) or walk.far_side(g, q, p[1], city))))
        spans, k = [], 0
        while k < len(pts):
            if not (flags[k] and flags[k][1]):
                k += 1
                continue
            i0 = k
            while i0 > 0 and flags[i0 - 1] and flags[i0 - 1][0] > 3:
                i0 -= 1
            i1 = k
            while i1 + 1 < len(pts) and flags[i1 + 1] and (flags[i1 + 1][1] or flags[i1 + 1][0] > 3):
                i1 += 1
            i0, i1 = max(0, i0 - 1), min(len(pts) - 1, i1 + 1)
            k = i1 + 1
            if flags[i0] and flags[i1]:
                spans.append([(i0, i1)])
        # spans less than WALK_JOIN m apart go as one (between them the
        # course zigzags within 3 m of the line), or else one by one
        joined = []
        for sp in spans:
            if joined and s_of[sp[0][0]] - s_of[joined[-1][-1][1]] < WALK_JOIN:
                joined[-1] += sp
            else:
                joined.append(sp)
        for group in reversed(joined):
            for i0, i1 in [(group[0][0], group[-1][1])] + (group[::-1] if len(group) > 1 else []):
                result = replace(pts, gates, s_of, run, i0, i1)
                if result:
                    pts, gates, s_of, n = result
                    taken += n
                    if (i0, i1) == (group[0][0], group[-1][1]):
                        break
    return pts, gates, taken


DRAG_OFF_AXIS = 12.0     # m a drag's course may run off its routes' line (the lanes span 18 m)
DRAG_STRAIGHT = 2.0      # m: a start or finish this close to the axis's line, however far along it, is on it
DRAG_ALL_ON_ROUTES = True   # every closed-track drag on its routes' line and lanes, not only those over DRAG_OFF_AXIS off (the airport's, 4221-4223: the course crossed the thin walls between the runway's channels, the cars ground along one at 7 m/s)
NO_LANE = -128           # a drag lane's offset where the lane is not there (drag lanes file)
LANE_UNITS = 4           # a drag lane's offset in the file: 1/4 m (to 31.75 m: the Bayview Bridge drag's other road runs 25 m off)
DRAG_EXTEND = 80.0       # m the routes' line may go on straight to a drag's start or finish
DRAG_WALL_ROOM = 30.0    # m between a drag's finish and a wall across its lanes
DRAG_GRID_ROOM = 8.0     # m behind its start (the grid's cars stand 6 m behind it)


def drag_on_routes(ev, pts, gates, walk, info, city, guide=None):
    """A closed track's drag along its routes' line, as the original drives
    it: its race nodes run along the edge of the lanes (L4RB's rail yard
    drag, 4211-4213: on the wall, and from there the course went on
    between the trains outside it, 10-45 m off). The routes' line, the
    walk's centre (walk_course) guided by the event's outline from the
    course's start to its finish, takes its place (DRAG_ALL_ON_ROUTES; else
    only where the course runs over DRAG_OFF_AXIS m off it). Returns (points, gates, [(x, y, state)]
    of the line, for the lanes) or None."""
    guide = guide or [(y, 0.0, x) for x, y in ev['outline']]
    # (the walk loses the outline for a point where one route hands over
    # to the next, and at the ends: a jump of over 40 m is no line)
    axis = [a for a in walk_course(walk, guide, False, centre=True, reach=DRAG_OFF_AXIS * 2) if a]
    if len(axis) < 2 or any(math.dist(a[:2], b[:2]) > 40 for a, b in zip(axis, axis[1:])):
        return None
    # the walk starts and ends some way in from the course's start and
    # finish (the routes begin past the outline's first point): the line
    # goes on straight to them, up to DRAG_EXTEND m; a course whose start or
    # finish lies further, or across, is not on these routes (L4RB's other
    # drags: the walk covers a stretch of them only)
    for end in (0, -1):
        a = axis[end]
        b = next((q for q in (axis if end == 0 else axis[::-1]) if math.dist(q[:2], a[:2]) >= 20), None)
        if b is None:
            return None
        ux, uy = a[0] - b[0], a[1] - b[1]
        l = math.hypot(ux, uy)
        ux, uy = ux / l, uy / l
        q = (pts[end][2], pts[end][0])
        t = (q[0] - a[0]) * ux + (q[1] - a[1]) * uy
        side = abs((q[0] - a[0]) * uy - (q[1] - a[1]) * ux)
        if (t > DRAG_EXTEND and side > DRAG_STRAIGHT) or side > DRAG_OFF_AXIS * 2:
            return None   # (straight on the axis any length: the airport's runway goes on past its last route)
        # not through a wall: the rail yard drag's event finish lies past the
        # end of its lanes, beyond a wall across them (the world's edge, 13 m
        # past the routes' end): its finish comes DRAG_WALL_ROOM m before
        # the first wall or void straight on, the line cut back to it
        h = pts[end][1]
        wall, room = None, DRAG_WALL_ROOM if end == -1 else DRAG_GRID_ROOM
        for k in range(1, int((max(t, 0) + room) * 2) + 1):
            x, y = a[0] + ux * k / 2, a[1] + uy * k / 2
            if not city.layers(x, y) or city._walled(x, y, h):
                wall = k / 2
                break
        if wall is not None and t > wall - room:
            t = wall - room   # (the start: the routes' own, the grid clear of the wall behind)
        if t > 1:
            p = (a[0] + ux * t, a[1] + uy * t, a[2], a[3], a[4], a[5])
            axis = [p] + axis if end == 0 else axis + [p]
        elif t < 0:   # back along the line by -t
            axis = axis[::-1] if end == 0 else axis
            back = -t
            while len(axis) > 2 and back > 0:
                d = math.dist(axis[-1][:2], axis[-2][:2])
                if d > back:
                    f = back / d
                    q = axis[-1]
                    axis[-1] = (q[0] + (axis[-2][0] - q[0]) * f, q[1] + (axis[-2][1] - q[1]) * f, *q[2:])
                    break
                back -= d
                axis.pop()
            axis = axis[::-1] if end == 0 else axis
    line = [(a[0], a[1]) for a in axis]

    def project(p):
        best = None
        for k, (a, b) in enumerate(zip(line, line[1:])):
            dx, dy = b[0] - a[0], b[1] - a[1]
            l2 = dx * dx + dy * dy
            t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2)) if l2 > 1e-9 else 0.0
            d = math.hypot(a[0] + dx * t - p[0], a[1] + dy * t - p[1])
            if best is None or d < best[0]:
                best = (d, k, t)
        return best
    if not DRAG_ALL_ON_ROUTES and max(project((q[2], q[0]))[0] for q in pts) <= DRAG_OFF_AXIS:
        return None
    # the whole line, from the start to the finish set above
    k0, t0, k1, t1 = 0, 0.0, len(line) - 2, 1.0
    def lerp(k, t):
        a, b = line[k], line[k + 1]
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, axis[k + (t > .5)][5])
    samples = [lerp(k0, t0)] + [(*line[k], axis[k][5]) for k in range(k0 + 1, k1 + 1)] + [lerp(k1, t1)]
    out, h = [], pts[0][1]
    for x, y, _ in samples:
        if out and math.dist((out[-1][2], out[-1][0]), (x, y)) < 2:
            continue
        ls = city.layers(x, y)
        h = min(ls, key=lambda v: abs(v - h)) if ls else h
        out.append((y, h, x))
    if len(out) < 2:
        return None
    out = unkink_drag(out, city)
    def near(q):
        return min(range(len(out)), key=lambda j: math.dist((out[j][2], out[j][0]), (q[2], q[0])))
    new_gates = sorted({0, len(out) - 1} | {near(pts[g]) for g in gates})
    return out, new_gates, samples




LANE_GAP = 30.0          # m: a lane open for less, between two shut stretches, is shut
LANE_CLEAR = 1.2         # m a drag lane keeps off its event's barriers (half a car and a margin)



DRAG_KINK = 8.0          # degrees a stretch of a straight drag's line may turn off it before it is a kink (unkink_drag)
DRAG_KINK_ROOM = 25.0    # m each side of a kink the line is laid straight over


def unkink_drag(out, city):
    """The drag's line (y, h, x points) straight through its kinks: where
    the walk hands over from one route to the next their centres step aside
    (Bayview Bridge, 4201-4203: 5.5 m in 16 m at s = 960, a 21 degree kink;
    4202 and 4203 also double back 6-7 m), and the AI's corner speed took
    the 40 m/s rivals down to 21 there (the PS2's go through flat out). A
    kink: a stretch turned over DRAG_KINK degrees off a road that runs the
    same way 5-35 m before and after it (within 5 degrees); the points
    DRAG_KINK_ROOM m either side go onto the straight between. The lanes
    stay on the routes (drag_lanes: offsets from the routes' own line)."""
    pts = [(q[2], q[0]) for q in out]
    dist = [0.0]
    for a, b in zip(pts, pts[1:]):
        dist.append(dist[-1] + math.dist(a, b))
    def heading(s0, s1):
        ks = [k for k in range(len(pts) - 1) if s0 <= dist[k] and dist[k + 1] <= s1]
        if not ks:
            return None
        a, b = pts[ks[0]], pts[ks[-1] + 1]
        return math.atan2(b[1] - a[1], b[0] - a[0])
    def off(x, y):
        return abs((x - y + math.pi) % (2 * math.pi) - math.pi)
    spans = []
    for k in range(len(pts) - 1):
        if dist[k + 1] - dist[k] < .5:
            continue
        hk = math.atan2(pts[k + 1][1] - pts[k][1], pts[k + 1][0] - pts[k][0])
        hb, ha = heading(dist[k] - 35, dist[k] - 5), heading(dist[k + 1] + 5, dist[k + 1] + 35)
        if hb is None or ha is None or off(hb, ha) > math.radians(5) or off(hk, hb) < math.radians(DRAG_KINK):
            continue
        s0, s1 = dist[k] - DRAG_KINK_ROOM, dist[k + 1] + DRAG_KINK_ROOM
        if spans and s0 <= spans[-1][1]:
            spans[-1] = (spans[-1][0], s1)
        else:
            spans.append((s0, s1))
    out = list(out)
    for s0, s1 in spans:
        i0 = max(k for k in range(len(out)) if dist[k] <= max(s0, 0) or k == 0)
        i1 = min(k for k in range(len(out)) if dist[k] >= min(s1, dist[-1]) or k == len(out) - 1)
        a, b = out[i0], out[i1]
        for k in range(i0 + 1, i1):
            t = (k - i0) / (i1 - i0)
            x, y = a[2] + (b[2] - a[2]) * t, a[0] + (b[0] - a[0]) * t
            ls = city.layers(x, y)
            out[k] = (y, min(ls, key=lambda v: abs(v - out[k][1])) if ls else out[k][1], x)
    return out


def route_lat(route, k, p, r):
    """How far right of course point p (game X, Y; right r) a route's line
    runs, at p's foot on its segments either side of point k (past its
    ends, point k's own)."""
    best = None
    for j in (k - 1, k):
        if 0 <= j < len(route) - 1:
            a, b = route[j], route[j + 1]
            sx, sy = b['x'] - a['x'], b['y'] - a['y']
            l2 = sx * sx + sy * sy
            if l2 < 1e-9:
                continue
            t = ((p[0] - a['x']) * sx + (p[1] - a['y']) * sy) / l2
            if not 0 <= t <= 1:
                continue
            c = (a['x'] + sx * t, a['y'] + sy * t)
            d = math.dist(c, p)
            if best is None or d < best[0]:
                best = (d, c)
    if best is None:   # off its segments: the point's own, if alongside (past the route's end the line goes on straight, drag_on_routes)
        c = (route[k]['x'], route[k]['y'])
        if abs((c[0] - p[0]) * r[1] - (c[1] - p[1]) * r[0]) > 15.0:
            return 0.0
    else:
        c = best[1]
    return (c[0] - p[0]) * r[0] + (c[1] - p[1]) * r[1]


def drag_on_routes_any(ev, pts, gates, walk, info, city):
    """drag_on_routes guided by the event's outline, else (DRAG_ALL_ON_ROUTES)
    by the course's own points: the airport's outline ends some 300 m short
    of its finish, the walk with it, and its runway's routes run on straight
    to it (4221-4223, x = 2455 from y = -1595 to 666)."""
    got = drag_on_routes(ev, pts, gates, walk, info, city)
    if got is None and DRAG_ALL_ON_ROUTES:
        got = drag_on_routes(ev, pts, gates, walk, info, city, [tuple(q) for q in pts])
    return got

def barrier_shapes(segs):
    """The event's barriers (n2routes.read_event_barriers, the lines the
    game tests its racers' ways against): their segments, and the closed
    ones among their chains (the rail yard's trains: a thin triangle over
    each, three segments end to end) as polygons."""
    polys, chain = [], []
    for a, b in segs:
        if chain and math.dist(chain[-1], a) < .05:
            chain.append(b)
        else:
            chain = [a, b]
            polys.append(chain)
    return segs, [c[:-1] for c in polys if len(c) > 3 and math.dist(c[0], c[-1]) < .05]


def lane_shut(shapes, p):
    """A drag lane shut at p (game X, Y): within LANE_CLEAR m of one of its
    event's barriers, or inside one (a train: the route's own lanes give
    way to it some 60 m before, but not every one where it ends)."""
    segs, polys = shapes
    for a, b in segs:
        dx, dy = b[0] - a[0], b[1] - a[1]
        l2 = dx * dx + dy * dy
        t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2)) if l2 > 1e-9 else 0.0
        if math.hypot(a[0] + dx * t - p[0], a[1] + dy * t - p[1]) < LANE_CLEAR:
            return True
    for poly in polys:
        inside = False
        for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1]):
            if (y0 > p[1]) != (y1 > p[1]) and p[0] < x0 + (x1 - x0) * (p[1] - y0) / (y1 - y0):
                inside = not inside
        if inside:
            return True
    return False


LANE_GAP_REACH = 3.5     # m: walls both sides of a lane within this put it in the middle of the gap
GAP_BEFORE, GAP_AFTER = 25.0, 10.0   # m before and after a gap its middle holds


def route_levels(route, city):
    """A route's ground height at each of its points: the layer nearest the
    one before (L4RB's Bayview Bridge drag: its right-hand road dips under
    the middle one and comes back over it, two layers where they cross)."""
    out, h = [], None
    for p in route:
        ls = city.layers(p['x'], p['y'])
        if ls:
            h = ls[0] if h is None else min(ls, key=lambda v: abs(v - h))
        out.append(h)
    first = next((v for v in out if v is not None), 0.0)
    return [first if v is None else v for v in out]


DRAG_OTHER_ROUTES = False   # the other routes' lanes (other_lanes): off until the AI follows a road that leaves the course (Bayview Bridge's right-hand one: 15-25 m off and under it; in the sim the rival on it stalled at s = 1045)
DRAG_OTHER_REACH = 30.0  # m off the course another of the event's routes may give a drag lane from (drag_lanes)
DRAG_SAME_LANE = 2.0     # m: another route's lane this close to one of the nearest route's, on its level, is that lane
DRAG_SAME_LEVEL = 3.0    # m of height: on the same road
DRAG_OTHER_ROAD = 5.0    # m: another route's centre this far off the course, or on another level, is another road


def other_lanes(p, h, u, r, others, near_route):
    """The lanes of the event's other routes alongside course point p
    (game X, Y; height h; way u, right r): (offset, height) of each, the
    routes running the way the racers drive (or against it, their slots
    4-7), within DRAG_OTHER_REACH m, on another road (DRAG_OTHER_ROAD m off,
    or another level)."""
    got = []
    for num, route, levels in others:
        if num == near_route:
            continue
        for j in range(len(route) - 1):
            a, b = route[j], route[j + 1]
            sx, sy = b['x'] - a['x'], b['y'] - a['y']
            l2 = sx * sx + sy * sy
            if l2 < 1e-6:
                continue
            t = ((p[0] - a['x']) * sx + (p[1] - a['y']) * sy) / l2
            if not 0 <= t <= 1:
                continue
            l = math.sqrt(l2)
            dot = (sx * u[0] + sy * u[1]) / l
            if abs(dot) < .85:
                continue
            cx, cy = a['x'] + sx * t, a['y'] + sy * t
            lat = (cx - p[0]) * r[0] + (cy - p[1]) * r[1]
            if abs(lat) > DRAG_OTHER_REACH:
                continue
            base = 0 if dot > 0 else 4
            lv = levels[j] + (levels[j + 1] - levels[j]) * t
            if abs(lat) < DRAG_OTHER_ROAD and abs(lv - h) < DRAG_SAME_LEVEL:
                continue   # (the same road: a copy of the route, or the next one where they meet; the rail yard's 4212 masks a lane before a train, and its neighbour had it back)
            for s_ in range(4):
                if a['lanes'][base + s_]:
                    o = a['lines'][2 + base + s_] + (b['lines'][2 + base + s_] - a['lines'][2 + base + s_]) * t
                    got.append((lat + o, lv))
    return got


def drag_lanes(pts, samples, info, shapes, city, others=()):
    """Per course point, the drag's four lanes (left to right): their
    offsets (m, right of the way) or None where the lane is not there,
    from the route point nearest along the line (the routes' traffic lanes
    along the way the racers drive: slots 0-3, or 4-7 against it), and not
    where it meets its event's barriers (lane_shut, or its line crossing
    one between two points: the rail yard drag's 4211 boxes one in at the
    station's gate, a lane its routes have; the game keeps the cars off a
    lane change through a barrier, FuturePoint::CanChangeDrivingLine).
    Where the nearest route has fewer than four, the event's other routes
    alongside give the rest (other_lanes; others: (number, points,
    route_levels) of each): the Bayview Bridge drag (4201-4203) splits at
    s = 950 into a middle road of two lanes and a right-hand one that
    dips under it and comes back over, 15-25 m off; on the PS2 a rival
    takes it, the others the middle's two (the port had the four cars in
    the two, down to 22-30 m/s). Also the lanes' positions (game X, Y,
    height) at each point, for drag_plan."""
    out, where, gaps, raw, j = [], [], [], [], 0
    for i, q in enumerate(pts):
        p = (q[2], q[0])
        while j + 1 < len(samples) and math.dist(samples[j + 1][:2], p) <= math.dist(samples[j][:2], p):
            j += 1
        r, k, fwd = samples[j][2]
        rp = info[r][k]
        base = 0 if fwd else 4
        a, b = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
        dx, dy = b[2] - a[2], b[0] - a[0]
        l = math.hypot(dx, dy) or 1.0
        rx, ry = dy / l, -dx / l     # right of the way (game X, Y)
        lat = route_lat(info[r], k, p, (rx, ry))
        line = [rp['lines'][2 + base + s] + lat for s in range(4)]
        row, at = [], []
        for s in range(4):
            o = line[s] if rp['lanes'][base + s] else None
            if o is not None and lane_shut(shapes, (p[0] + rx * o, p[1] + ry * o)):
                o = None
            row.append(o)
            # (the gap's middle, if walled both sides near: below)
            o = line[s]
            def wall_off(sign):
                for k in range(1, int(LANE_GAP_REACH * 4) + 1):
                    d = sign * k / 4
                    if city._walled(p[0] + rx * (o + d), p[1] + ry * (o + d), q[1]):
                        return d
                return None
            lw, rw = wall_off(-1), wall_off(1)
            at_gap = (lw + rw) / 2 if lw is not None and rw is not None else None
            row.append(at_gap)
            o = line[s]
            at.append((p[0] + rx * o, p[1] + ry * o))
        o_row, g_row = [row[2 * k] for k in range(4)], [row[2 * k + 1] for k in range(4)]
        r_row = [line[s] if rp['lanes'][base + s] else None for s in range(4)]
        at = [(x, y, q[1]) for x, y in at]
        if others and sum(v is not None for v in r_row) < 4:
            have = [v for v in r_row if v is not None]
            extra = []
            for o, lv in sorted(other_lanes(p, q[1], (-ry, rx), (rx, ry), others, r), key=lambda e: min([abs(e[0] - v) for v in have] or [0])):
                if abs(lv - q[1]) < DRAG_SAME_LEVEL and any(abs(o - v) < DRAG_SAME_LANE for v in have + [e[0] for e in extra if abs(e[1] - q[1]) < DRAG_SAME_LEVEL]):
                    continue
                if any(abs(o - e[0]) < DRAG_SAME_LANE and abs(lv - e[1]) < DRAG_SAME_LEVEL for e in extra):
                    continue
                extra.append((o, lv))
            for o, lv in extra:
                if None not in r_row:
                    break
                s = r_row.index(None)
                r_row[s] = o
                o_row[s] = None if lane_shut(shapes, (p[0] + rx * o, p[1] + ry * o)) else o
                g_row[s] = None
                at[s] = (p[0] + rx * o, p[1] + ry * o, lv)
        out.append(o_row)
        gaps.append(g_row)
        where.append(at)
        raw.append(r_row)
    lane_slots(raw, out, gaps, where)
    # Through a gap (the station's gates), its middle: the port's 1 m grid
    # walls a gate's thin bars 2-3 m wide, and the game's lanes pass 0.5-1
    # m off the middle of what is left. The shift holds from GAP_BEFORE m
    # before the gap (the car is in the middle when it gets there) to
    # GAP_AFTER m after.
    dist = [0.0]
    for a, b in zip(pts, pts[1:]):
        dist.append(dist[-1] + math.dist((a[2], a[0]), (b[2], b[0])))
    for s in range(4):
        shift = [g[s] for g in gaps]
        for i in range(len(out)):
            if out[i][s] is None:
                continue
            near = [(abs(dist[k] - dist[i]), shift[k]) for k in range(len(out))
                    if shift[k] is not None and -GAP_AFTER <= dist[k] - dist[i] <= GAP_BEFORE]
            if near:
                out[i][s] += min(near)[1]
    for i in range(len(out) - 1):   # a lane's line through a barrier between two points
        for s in range(4):
            if out[i][s] is not None and out[i + 1][s] is not None and \
                    any(segments_cross(where[i][s], where[i + 1][s], a, b) for a, b in shapes[0]):
                out[i][s] = out[i + 1][s] = None
    # a lane open for under LANE_GAP m between two shut stretches is shut
    # all along (the station gate's box: two lines across it 24 m apart)
    for s in range(4):
        i = 0
        while i < len(out):
            if out[i][s] is not None:
                j = i
                while j + 1 < len(out) and out[j + 1][s] is not None:
                    j += 1
                if i > 0 and j + 1 < len(out) and dist[j + 1] - dist[i - 1] < LANE_GAP:
                    for k in range(i, j + 1):
                        out[k][s] = None
                i = j + 1
            else:
                i += 1
    return out, where, dist


LANE_JUMP = 8.0          # m: a slot's lane this far from where it was is another lane, two lanes over (lane_slots; 4202 narrows to two with a step of 6.1 m, 4401 jumps 12.2)


def lane_slots(raw, *rows):
    """Keeps each of the four slots on one lane where the routes change:
    a route point's lanes are numbered from its own left, and where a road
    of two goes on into one of four (L4RD's 4401, at s = 185: two lanes,
    then four with the first two on the far side of a divider) slot 0 went
    from the right side's first lane to the left side's, 12 m over, and
    the car in it across into the divider. Each point's lanes (raw: the
    routes', before lane_shut) go into the slots nearest the last offsets
    those slots had, in order, from the point with the most lanes outward;
    rows (out, gaps, where) are reordered the same way, in place. Only
    where a slot would jump over LANE_JUMP m: realigned where the road
    narrows from four lanes to two (4201-4203's end), the sim's rivals
    crashed more and the player finished a 4203 in 77 s, not 52."""
    if not raw:
        return
    from itertools import permutations
    first = max(range(len(raw)), key=lambda i: (sum(o is not None for o in raw[i]), -i))
    def place(i, mem):
        got = [(s, o) for s, o in enumerate(raw[i]) if o is not None]
        if all(mem[s] is None or abs(o - mem[s]) <= LANE_JUMP for s, o in got):
            to = [s for s, _ in got]   # (a lane's step over a few m where the road narrows, 4201-4203's end: kept, the cars' plan moves them)
        else:
            to = None
        best = None
        for slots in permutations(range(4), len(got)) if to is None else ():
            cost = sum(abs(o - mem[t]) if mem[t] is not None else 30.0 for (_, o), t in zip(got, slots))
            cost += 0.01 * sum(t != s for (s, _), t in zip(got, slots))   # (a tie: as the routes number them)
            if best is None or cost < best:
                best, to = cost, slots
        perm = list(range(4))
        if any(s != t for (s, _), t in zip(got, to)):
            rest = [s for s in range(4) if s not in [s for s, _ in got]]
            free = [t for t in range(4) if t not in to]
            perm = [None] * 4
            for (s, _), t in zip(got, to):
                perm[t] = s
            for s, t in zip(rest, free):
                perm[t] = s
            for r in (raw,) + rows:
                r[i] = [r[i][perm[t]] for t in range(4)]
        for t in range(4):
            if raw[i][t] is not None:
                mem[t] = raw[i][t]
    for step in (1, -1):
        mem = list(raw[first])
        i = first + step
        while 0 <= i < len(raw):
            place(i, mem)
            i += step


DRAG_MOVE = 50.0         # m a drag car's move to the next lane is planned over (drag_plan; the routes end a lane some 60 m before a train in it)
DRAG_KEEP = 120.0        # m: a lane kept to within this of its end, then the move (four cars for two lanes: 4212)


def drag_plan(lanes, where, dist, city, pts, shapes):
    """Per course point and lane (the one a car is in), the lane to head
    for: that one, or a neighbour (one lane at a time, as the game's
    DragController::ChangeLaneNumber, and only where both lanes are there
    over the DRAG_MOVE m the move takes, with no wall or barrier between
    them all along: never through the station's gates). The plan going furthest (where its lane ends is where a car
    meets the train in it), fewest moves, and each lane kept until DRAG_KEEP
    m before its end, the move then as early as it can be (a move at speed
    takes more than DRAG_MOVE m to settle). Moving as early as it could,
    every lane went over to the one going furthest: 4212's four cars in one
    lane by s = 341, three of them on the one lane past 423."""
    n = len(lanes)
    there = [[v is not None for v in row] for row in lanes]
    def crossable(i, j):   # between lane j and j+1 at point i
        a, b = where[i][j], where[i][j + 1]
        if abs(a[2] - b[2]) > DRAG_SAME_LEVEL:
            return False   # (one under the other's road)
        h = pts[i][1]
        m = max(1, int(math.dist(a, b) * 2))
        if any(city._walled(a[0] + (b[0] - a[0]) * k / m, a[1] + (b[1] - a[1]) * k / m, h) for k in range(m + 1)):
            return False
        return not any(segments_cross(a, b, p, q) for p, q in shapes[0])
    gap = [[there[i][j] and there[i][j + 1] and crossable(i, j) for j in range(3)] for i in range(n)]
    end, e = [0] * n, 0
    for a in range(n):
        while e < n - 1 and dist[e] - dist[a] < DRAG_MOVE:
            e += 1
        end[a] = e
    left = [[0.0] * 4 for _ in range(n)]   # metres each lane stays there ahead
    for j in range(4):
        shut = dist[-1] + DRAG_KEEP
        for a in range(n - 1, -1, -1):
            if not there[a][j]:
                shut = dist[a]
            left[a][j] = shut - dist[a]
    value = [[(0.0, 0)] * 4 for _ in range(n)]
    plan = [[j for j in range(4)] for _ in range(n)]
    for a in range(n - 1, -1, -1):
        for j in range(4):
            if not there[a][j]:
                value[a][j] = (dist[a] - 1e-3, 0)
                continue
            if a == n - 1:
                value[a][j] = (dist[a] + 1, 0)
                continue
            best, go = (value[a + 1][j] if there[a + 1][j] else (dist[a], 0)), j
            for jj in (j - 1, j + 1):
                if 0 <= jj < 4 and end[a] > a and all(gap[c][min(j, jj)] for c in range(a, end[a] + 1)):
                    cand = (value[end[a]][jj][0], value[end[a]][jj][1] - 1)
                    if cand > best or (cand == best and left[a][j] <= DRAG_KEEP):
                        best, go = cand, jj   # (as good as staying: now only within DRAG_KEEP m of the lane's end)
            value[a][j] = best
            plan[a][j] = go
    return plan


def string_hash(s):
    """The game's bStringHash (the markers' names)."""
    h = 0xffffffff
    for c in s.encode('ascii'):
        h = (h * 33 + c) & 0xffffffff
    return h


MARKER_NAMES = ('ForwardStartLine', 'ForwardFinishLine', 'BackwardStartLine', 'BackwardFinishLine',
                'ForwardStartingPosition', 'BackwardStartingPosition')
GRID_SLOTS = 8          # vagas do grid por sentido (CalculateRacerPositions pede até 6)


def read_markers(game, track):
    """The events' position markers: Tracks\\Routes<track>\\TrackPosMarkersAll.bin,
    chunk 0x34146 (LoaderTrackPositionMarkers), 48-byte records 16-aligned:
    two list links, the name's hash, its index, the position (game x, y, z,
    w), a u16 angle (65536 a turn) and the event's number. The start and
    finish lines and the grid's slots, by event: {id: {name: {index: (GU x,
    y, z, yaw)}}}, yaw the GU heading (atan2(dx, dz)): the slots' angle is
    the cars' heading, the lines' a quarter turn less (across the road)."""
    try:
        d = archived(game, f'Tracks\\Routes{track}\\TrackPosMarkersAll.bin')
    except ValueError:
        return {}
    names = {string_hash(n): n for n in MARKER_NAMES}
    out = collections.defaultdict(lambda: collections.defaultdict(dict))
    for t, p, n in chunks(d):
        if t != 0x34146:
            continue
        q = (p + 15) & ~15
        for k in range((n - (q - p)) // 0x30):
            h, idx, x, y, z, _, ang, _, eid = struct.unpack_from('<IIffffHHI', d, q + 0x30 * k + 8)
            if h in names:
                out[eid][names[h]][idx] = (y, z, x, ang * 2 * math.pi / 65536)
    return out


def marker(marks, name):
    """The first of an event's markers by that name (the game's
    GetTrackPositionMarker(name, 0)), or None."""
    m = marks.get(name)
    return m[min(m)] if m else None


def heading_of(m, line):
    """A marker's way along the road (GU x, z): a line's angle runs across it."""
    yaw = m[3] + (math.pi / 2 if line else 0)
    return math.sin(yaw), math.cos(yaw)


APPROACH = 40.0   # m of road laid behind a sprint's start line (the grid's back row is ~9 m behind it)
RUNOUT = 100.0    # m of course kept past a sprint's finish line, to slow down on (the original's path runs ~5% past it)


def poly_project(pts, closed, x, z, lo=0.0, hi=1e18):
    """Nearest point of the polyline (GU, horizontally) to (x, z) whose
    distance along it lies in [lo, hi]: (distance along, distance off,
    segment, its parameter)."""
    best, s = (1e18, 0.0, 0, 0.0), 0.0
    n = len(pts)
    for i in range(n if closed else n - 1):
        a, b = pts[i], pts[(i + 1) % n]
        dx, dz = b[0] - a[0], b[2] - a[2]
        l = math.hypot(dx, dz)
        ulo = max(0.0, (lo - s) / l) if l > 1e-6 else 0.0
        uhi = min(1.0, (hi - s) / l) if l > 1e-6 else 1.0
        if ulo <= uhi and s <= hi:
            u = ((x - a[0]) * dx + (z - a[2]) * dz) / (l * l) if l > 1e-6 else 0.0
            u = max(ulo, min(uhi, u))
            off = math.hypot(a[0] + dx * u - x, a[2] + dz * u - z)
            if off < best[0]:
                best = (off, s + l * u, i, u)
        s += l
    return best[1], best[0], best[2], best[3]


def mark_outline(outline, start, finish):
    """A sprint's outline (game x, y) laid from APPROACH m behind its start
    line to past its finish line: the outline's points between the two
    lines (each line found where it can be, the start in the first 40% of
    the outline, the finish in the last 40%: 4127's outline passes its
    finish line halfway), the outline's own points past the finish (its
    run-out road), or one RUNOUT m on along the finish's way."""
    pts = [(gy, 0.0, gx) for gx, gy in outline]    # GU, for poly_project
    total = sum(math.hypot(b[0] - a[0], b[2] - a[2]) for a, b in zip(pts, pts[1:]))
    s0 = poly_project(pts, False, start[0], start[2], 0, .4 * total)[0]
    s1 = poly_project(pts, False, finish[0], finish[2], max(s0, .6 * total))[0]
    along, s = [], 0.0
    for k, p in enumerate(pts):
        if k:
            s += math.hypot(p[0] - pts[k - 1][0], p[2] - pts[k - 1][2])
        along.append(s)
    hx, hz = heading_of(start, True)
    out = [(start[2] - hz * APPROACH, start[0] - hx * APPROACH), (start[2], start[0])]
    out += [outline[k] for k in range(len(pts)) if s0 + 20 < along[k] < s1 - 20]
    # a line has no way of its own: the finish's is the one away from the
    # outline's point before it (4105's line angle points back up the road)
    fx, fz = heading_of(finish, True)
    if fx * (finish[0] - out[-1][1]) + fz * (finish[2] - out[-1][0]) < 0:
        fx, fz = -fx, -fz
    out.append((finish[2], finish[0]))
    after = [outline[k] for k in range(len(pts)) if along[k] > s1 + 20]
    out += after or [(finish[2] + fz * RUNOUT * .6, finish[0] + fx * RUNOUT * .6)]
    return out


def circuit_from_line(outline, start):
    """A circuit's closed outline (game x, y; the last point the first)
    from its start line round to it again, through APPROACH m of road on
    either side of the line (the outline's points nearer the line go)."""
    loop = [(gy, 0.0, gx) for gx, gy in outline[:-1]]
    total = sum(math.hypot(b[0] - a[0], b[2] - a[2]) for a, b in zip(loop, loop[1:] + loop[:1]))
    s0 = poly_project(loop, True, start[0], start[2])[0]
    along = horizontal_along(loop)
    hx, hz = heading_of(start, True)
    order = sorted(range(len(loop)), key=lambda k: (along[k] - s0) % total)
    mid = [outline[k] for k in order if APPROACH + 20 < (along[k] - s0) % total < total - APPROACH - 20]
    line = (start[2], start[0])
    return [line, (start[2] + hz * APPROACH, start[0] + hx * APPROACH)] + mid + \
        [(start[2] - hz * APPROACH, start[0] - hx * APPROACH), line]


NEAR_LINE = 600.0   # m of a circuit, on either side of its start line, a course laid again from the line may change


def changed_near_start(old, new, apart=10.0):
    """True when the new circuit (both from the start line) keeps within
    apart metres of the old everywhere but its NEAR_LINE m either side of
    the line."""
    along = horizontal_along(new)
    total = along[-1] + math.hypot(new[-1][0] - new[0][0], new[-1][2] - new[0][2])
    return all(poly_project(old, True, p[0], p[2])[1] <= apart for p, s in zip(new, along)
               if NEAR_LINE < s < total - NEAR_LINE)


def cut_at(pts, at, closed):
    """The polyline's point at at metres along it (horizontally) inserted:
    (points, index of that point)."""
    s = 0.0
    n = len(pts)
    for i in range(n if closed else n - 1):
        a, b = pts[i], pts[(i + 1) % n]
        l = math.hypot(b[0] - a[0], b[2] - a[2])
        if s + l >= at:
            t = (at - s) / l if l > 1e-6 else 0.0
            if t < 1e-3:
                return pts, i
            if t > 1 - 1e-3:
                return pts, (i + 1) % n
            p = tuple(a[c] + (b[c] - a[c]) * t for c in range(3))
            return pts[:i + 1] + [p] + pts[i + 1:], i + 1
        s += l
    return pts, (0 if closed else n - 1)


def along3(pts, upto):
    """The 3D length of the polyline up to point upto (race.c's dist)."""
    return sum(math.dist(pts[k], pts[k + 1]) for k in range(upto))


def horizontal_along(pts):
    """Each point's distance along the polyline, horizontally."""
    out, s = [0.0], 0.0
    for a, b in zip(pts, pts[1:]):
        s += math.hypot(b[0] - a[0], b[2] - a[2])
        out.append(s)
    return out


def at_markers(pts, gates, circuit, marks):
    """The course from its start line (point 0, the forward start line's
    place on it) and, for a sprint, to RUNOUT m past its finish line, a
    point and the last gate there. As the original's Race::FigureOutBoundaries:
    a circuit's finish is its start; with no line, a sprint's start would be
    at 5% of the way and its finish at 95%. The other gates keep their
    places (those within 15 m of the start or the finish go). Returns the
    points, the gates, the finish's distance (3D along the course, as
    race.c's; 0 on circuits), how far off the course each line lies and,
    on a circuit, where along it (horizontally) the course laid began: the
    racing line is laid from there (racing_line.py), as before the lines
    moved the start."""
    start, finish = marker(marks, 'ForwardStartLine'), marker(marks, 'ForwardFinishLine')
    along = horizontal_along(pts)
    total = along[-1] + (math.hypot(pts[-1][0] - pts[0][0], pts[-1][2] - pts[0][2]) if circuit else 0.0)
    at = [along[g] for g in gates]
    if circuit:
        if not start:
            return pts, gates, 0.0, (None, None), 0.0
        s0, off0, _, _ = poly_project(pts, True, start[0], start[2])
        pts, k = cut_at(pts, s0, True)
        pts = pts[k:] + pts[:k]
        at = [(d - s0) % total for d in at]
        s1, off1, kf = total, None, None
    else:
        s0, off0 = poly_project(pts, False, start[0], start[2], 0, .4 * total)[:2] if start else (.05 * total, None)
        s1, off1 = poly_project(pts, False, finish[0], finish[2], max(s0, .6 * total))[:2] if finish else (.95 * total, None)
        pts, k = cut_at(pts, s0, False)
        pts = pts[k:]
        s1 -= s0
        at = [d - s0 for d in at]
        pts, kf = cut_at(pts, s1, False)
        pts, ke = cut_at(pts, s1 + RUNOUT, False)
        pts = pts[:ke + 1]
    along = horizontal_along(pts)
    def index(d):
        return min(range(len(pts)), key=lambda i: abs(along[i] - d))
    keep = sorted({index(d) for d in at if 15 < d < s1 - 15} - {0})
    if circuit:
        return pts, [0] + keep, 0.0, (off0, None), (total - s0) % total
    keep = [g for g in keep if g < kf]
    return pts, [0] + keep + [kf], along3(pts, kf), (off0, off1), 0.0


def marks_record(eid, pts, circuit, finish, marks, anchor=0.0):
    """The event's marks.n2mk record: u16 id, u8 forward slots, u8 backward
    slots, f32 finish (the sprint's race distance, 0 on a circuit), f32
    backward start and finish (their distances along the forward course,
    -1 if none), f32 the racing line's anchor (at_markers), then GRID_SLOTS
    forward and GRID_SLOTS backward slots of f32 GU x, y, z and yaw (unused
    ones zero)."""
    def dist_of(m):
        if not m:
            return -1.0
        s, _, i, u = poly_project(pts, circuit, m[0], m[2])
        a, b = pts[i], pts[(i + 1) % len(pts)]
        return along3(pts, i) + math.dist(a, b) * u
    def on_ground(m):
        # the markers' heights are not the ground's (the airport's runway
        # markers are 11 m above it): the game puts each car on the
        # topology below (CalculateRacerPositions); here the course's height
        # where the slot projects onto it (the PSP looks for the ground
        # within 4 m of that)
        _, _, i, u = poly_project(pts, circuit, m[0], m[2])
        a, b = pts[i], pts[(i + 1) % len(pts)]
        return m[0], a[1] + (b[1] - a[1]) * u, m[2], m[3]
    out = bytearray()
    slots = []
    for name in ('ForwardStartingPosition', 'BackwardStartingPosition'):
        got = marks.get(name, {})
        # the game's slot k is the marker of index k; a gap ends the grid
        run = []
        while len(run) < GRID_SLOTS and len(run) in got:
            run.append(on_ground(got[len(run)]))
        slots.append(run)
    out += struct.pack('<HBBffff', eid, len(slots[0]), len(slots[1]), finish,
                       dist_of(marker(marks, 'BackwardStartLine')), dist_of(marker(marks, 'BackwardFinishLine')), anchor)
    for run in slots:
        for k in range(GRID_SLOTS):
            out += struct.pack('<4f', *(run[k] if k < len(run) else (0, 0, 0, 0)))
    return out


def lay_course(ev, nodes, links, usable, city, free_count, city_track, barriers=False, median=True, walls=False):
    """The course over the race's road graph and its clean-up passes, or
    None when no road joins its outline. barriers: edges through the
    event's barriers count as closed. Also returns its blocked metres,
    how many segments cross the barriers, its length and the outline's."""
    check = EdgeCheck(city, free_count, barriers, median, walls)
    found = course(nodes, links, usable, ev['outline'], ev['circuit'], free_count, check)
    if not found and not city_track:
        # L4RB's drags on the airport runway: three events (a lane each,
        # 20 m apart) share one run of race nodes along a wall the game
        # draws between lanes; the course is that run, nudged off the wall
        own = [k >= free_count for k in range(len(nodes))]
        found = course(nodes, links, own, ev['outline'], ev['circuit'], free_count)
        channel = True
    else:
        channel = False
    if not found:
        return None
    route, gates = prune_spurs(*found)
    if ev['circuit']:
        if route[-1] == route[0]:
            route.pop()
        gates = [g for g in gates if g < len(route)]
    raw = [nodes[i] for i in route]
    route_crossings = sum(city.crosses_barrier(a[2], a[0], a[1], b[2], b[0], b[1])
                          for a, b in zip(raw, raw[1:] + (raw[:1] if ev['circuit'] else [])))
    pts, gates, cut = unkink(raw, gates, ev['circuit'], city)
    pts, gates, pulled = straighten(pts, gates, city)
    pts, gates, moved = nudge(pts, gates, ev['circuit'], city, walls)
    if channel:
        pts, centred = centre_in_channel(pts, city)
        moved += centred
        pts, gates, trimmed = trim_walled_ends(pts, gates, city)
        cut += round(trimmed)
    relayered = 0
    if city_track:   # the closed tracks follow the ground below
        pts, relayered = relayer(pts, ev['circuit'], city)
    segs = [(pts[k], pts[(k + 1) % len(pts)]) for k in range(len(pts) if ev['circuit'] else len(pts) - 1)]
    blocked = sum(sum(city.corridor(a[2], a[0], a[1], b[2], b[0], b[1])) for a, b in segs)
    crossings = sum(city.crosses_barrier(a[2], a[0], a[1], b[2], b[0], b[1]) for a, b in segs)
    faults = sum(city.crosses_wall(a[2], a[0], a[1], b[2], b[0], b[1]) or city.crosses_step(a[2], a[0], a[1], b[2], b[0], b[1])
                 for a, b in segs)
    return dict(pts=pts, gates=gates, cut=cut, pulled=pulled, moved=moved, relayered=relayered, blocked=blocked,
                crossings=crossings, faults=faults, route_crossings=route_crossings, length=sum(math.dist(a, b) for a, b in segs),
                outline=sum(math.dist(ev['outline'][k], ev['outline'][k + 1]) for k in range(len(ev['outline']) - 1)))


_CTX = {}


def load_context(args, only):
    """What every event reads: the track's roads, its collision, its routes
    and markers (in each process of the pool once)."""
    global TRACK
    TRACK = args.track
    city_track = args.track == 'L4RA'
    free_nodes, free_links = read_roads(args.out / ('roads.n2rd' if city_track else f'roads-{args.track}.n2rd'))
    city = CityCollision(args.out / ('city.n2cy' if city_track else f'track-{args.track}.n2cy'))
    base_city = city
    net = read_route_net(args.game, args.track)
    info = n2routes.read_routes(args.game, args.track)
    route_events = n2routes.route_events(args.game, args.track)
    all_marks = read_markers(args.game, args.track)
    _CTX.update(args=args, only=only, city_track=city_track, free_nodes=free_nodes, free_links=free_links, city=city,
                base_city=base_city, net=net, info=info, route_events=route_events, all_marks=all_marks)


def lay_event_record(ev):
    """One event of the catalog: (its report, its course's record in the
    races file, its marks' record, its lanes' record or None); no course:
    (why, None, None, None). Each event on its own: the events run side by
    side in processes (main), and the files keep the catalog's order."""
    (args, only, city_track, free_nodes, free_links, city, base_city, net, info, route_events,
     all_marks) = (_CTX[k] for k in ('args', 'only', 'city_track', 'free_nodes', 'free_links', 'city', 'base_city', 'net', 'info',
                                    'route_events', 'all_marks'))
    marks = all_marks.get(ev['id'], {})
    catalog_ev = ev
    if not ev['circuit'] and marker(marks, 'ForwardStartLine') and marker(marks, 'ForwardFinishLine'):
        # laid from just behind the start line to past the finish line
        # (the catalog's outline runs on either side: 4201's 236 m
        # before its grid; 4107's starts 170 m after its start line)
        ev = dict(ev, outline=mark_outline(ev['outline'], marker(marks, 'ForwardStartLine'), marker(marks, 'ForwardFinishLine')))
    if not path_exists(args.game, args.track, ev['id']):
        return dict(id=ev['id'], error='no paths file'), None, None, None
    # the event's own barriers over the world (convert_city.py's bar-<id>.n2cy)
    event_city = base_city.with_overlay(args.out / f"bar-{ev['id']}.n2cy")
    def lay_event(ev):
        """The event's course over its roads and the clean-up passes:
        (points, gates, report fields), or why there is none."""
        city = event_city
        nodes, links = race_roads(args.game, ev['id'], free_nodes, free_links, city)
        # The city's graph is one network (its main component); a closed
        # track's is several islands, one per course: any of them will do
        # (the course still has to join its points by road).
        usable = components(nodes, links) if city_track else [True] * len(nodes)
        def choose(walls):
            laid = lay_course(ev, nodes, links, usable, city, len(free_nodes), city_track, median=False, walls=walls)
            if laid and not walls and (laid['crossings'] or laid['blocked']):
                # The race roads off their medians (EdgeCheck): kept only where
                # they leave fewer barrier crossings, or as many and fewer
                # blocked metres, with no detour. Everywhere, they changed
                # courses that drove fine (4126's took a race road over a deck,
                # wedging where it joined and left it).
                med = lay_course(ev, nodes, links, usable, city, len(free_nodes), city_track, walls=walls)
                if med and (med['crossings'], med['blocked']) < (laid['crossings'], laid['blocked']) \
                        and laid['outline'] * .95 <= med['length'] <= laid['length'] * 1.1:
                    laid = med
            if laid and (laid['crossings'] or laid['route_crossings']):
                # Through one of the event's barriers (the game's invisible
                # PLAYER_BARRIERS_<id>, a 1 m wall: on the diagonal a metre or
                # two of a car-wide corridor, under the edges' tolerance): the
                # course driver wedges there (4126, Black Tusk, twice). Again
                # with those edges closed, kept when that costs no walls and no
                # detour (closing them can send the course the long way round,
                # or skip a stretch of the outline).
                # Also when the route crossed one and nudge took the course round
                # the barrier's end: that can cost more walls than the route
                # that keeps off it (4124, 4145).
                other = lay_course(ev, nodes, links, usable, city, len(free_nodes), city_track, barriers=True, median=False, walls=walls)
                # (a barrier closing a road calls for the detour: up to 1.3 times
                # the outline, the 4144's 1.2)
                if other and not other['crossings'] and other['blocked'] <= laid['blocked'] \
                        and laid['outline'] * .95 <= other['length'] <= longest_detour(laid, other):
                    laid = other
            return laid
        laid = choose(False)
        if laid and laid['faults']:
            # Through a thin wall across the road or off a step (more than
            # DROP): the 4083's course went through two walls of a single
            # cell, the 4043's dropped 1.9 m off a terrain block. Again with
            # those closed (EdgeCheck, nudge), kept when that leaves fewer
            # faults and crossings, and no detour. Everywhere, they moved
            # courses that drove fine (4084, 4105).
            alt = choose(True)
            if alt and (alt['faults'] + alt['crossings'], alt['blocked']) < (laid['faults'] + laid['crossings'], laid['blocked']) \
                    and laid['outline'] * .95 <= alt['length'] <= longest_detour(laid, alt):
                laid = alt
        if not laid:
            return 'no road path'
        pts, gates, cut, pulled, moved, relayered = (laid[k] for k in ('pts', 'gates', 'cut', 'pulled', 'moved', 'relayered'))
        followed = walked = 0
        if city_track:
            # Where the original game's racers drive (RacerWalk): on a dual
            # road, the carriageway its signs keep them to (the 4083's ramp
            # and the road after it: the east one; the course took the west).
            walk = RacerWalk(*net, n2routes.read_event_barriers(args.game, args.track, ev['id']))
            pts, gates, walked = follow_walk(pts, gates, ev['circuit'], walk_course(walk, pts, ev['circuit']), city, walk)
        if str(ev['id']) in REFERENCE:
            # Where the original game's rivals drive (race_reference.json): the
            # 4083's course took the west carriageway of a dual ramp and the
            # road after it, through the median twice; theirs keeps east.
            pts, gates, followed = follow_reference(pts, gates, REFERENCE[str(ev['id'])], city)
        axis = None
        if not city_track and (4200 <= ev['id'] < 4300 or 4400 <= ev['id'] < 4500):
            # only the routes that stand in the event (n2routes.route_events)
            mine = {r: p for r, p in net[0].items() if n2routes.route_for(route_events, r, ev['id'])}
            signs = {key: [sg for sg in v if sg[0] in mine] for key, v in net[1].items() if key[0] in mine}
            walk = RacerWalk(mine, signs, n2routes.read_event_barriers(args.game, args.track, ev['id']))
            axis = drag_on_routes_any(ev, pts, gates, walk, info, city)
            if axis:
                pts, gates, _ = axis
                walked = len(pts)
        if not city_track and ev['circuit'] and not axis:
            # The lap along the event's own routes where the course misses
            # its outline (the map's drawing of the race) and the routes
            # keep to it: the Parkade's 4306, laid over the roads of the
            # floor below, cut a corner of the map 104 m off it, through a
            # wall on the decks; its routes pass within 9 m of every point.
            own = f"ROUTES{ev['id']}F.BIN"
            # (judged on the course without its backtracks, as it will be
            # raced)
            clean = drop_backtracks(list(pts), list(gates), ev['circuit'])[0]
            if route_file_exists(args.game, args.track, own) and off_outline(ev, [(q[2], q[0]) for q in clean]) > OUTLINE_MISS:
                lap = event_lap(RacerWalk(*read_route_net(args.game, args.track, own),
                                          n2routes.read_event_barriers(args.game, args.track, ev['id'])),
                                pts, ev['circuit'], city)
                if lap and off_outline(ev, [(q[2], q[0]) for q in lap]) <= OUTLINE_KEEP:
                    gates = sorted(set(min(range(len(lap)), key=lambda j: math.dist(lap[j], pts[g])) for g in gates))
                    pts, walked = lap, len(lap)
        if not city_track:
            pts, gates, back = drop_backtracks(pts, gates, ev['circuit'])
            start = marker(marks, 'ForwardStartLine') or marker(marks, 'ForwardStartingPosition')
            anchor = start_anchor(pts, start, city) if start else None
            ground = pts
            pts, step = follow_ground(ground, ev['circuit'], city, anchor)
            if step > 5 and anchor:
                # the course's points cut the Parkade's spiral ramp on the way
                # down (the 4302, the 4306): no ground joins them on its floor,
                # so it stays on the ground floor, as before
                pts, step = follow_ground(ground, ev['circuit'], city)
            elif anchor:
                # the lines across the spiral's well, round the ramp instead
                pts, gates, threaded = thread_ramps(pts, gates, ev['circuit'], city)
            if walked and not axis:
                # Drag lanes/availability are sampled at these points too.
                # Keep their dense route axis: simplifying only its centre
                # discards short lane closures and stretches lane changes.
                # the lap from its routes (event_lap), a point every 5 m:
                # only its turns and its changes of slope kept (the 4306's
                # 453 points, 4.5 KB more of races-L4RC.n2rc)
                pts, gates = simplify_course(pts, gates)
            cut += back
            # On the ground, every closed track's course again: the runway
            # drags now lay out on the free roam's links (not the channel
            # fallback), from a race node 45 m behind the wall that closes
            # the lane channel, and the walls show only at the ground's height
            if not ev['circuit']:
                pts, gates, trimmed = trim_walled_ends(pts, gates, city)
                cut += round(trimmed)
            if step > 5:
                # the ground under the line drops away (a deck whose
                # collision ends over the road beneath): no car makes it
                return f'ground step {step:.0f} m'
        return pts, gates, dict(kinks_cut=cut, pulled=pulled, nudged=moved, relayered=relayered, walked=walked,
                                followed=followed, barrier_crossings=laid['crossings'], axis=axis and axis[2])
    got = lay_event(ev)
    if isinstance(got, str) and ev is not catalog_ev:
        # no road to the lines' points (L4RB's runways: the lane channel
        # closes behind the grid and past the finish): the catalog's
        # outline, cut at the lines afterwards (at_markers)
        ev = catalog_ev
        got = lay_event(ev)
    if isinstance(got, str):
        return dict(id=ev['id'], error=got), None, None, None
    pts, gates, fields = got
    cut_pts = at_markers(pts, gates, ev['circuit'], marks)
    def worst(cut):
        return max(o for o in cut[3] if o is not None) if any(o is not None for o in cut[3]) else 0.0
    if ev is not catalog_ev and worst(cut_pts) > 5:
        # laid to the lines' points, the course still misses one (4141's
        # start line, 16 m off the road its approach point took): the
        # catalog's outline cut at the lines, when it passes nearer them
        other = lay_event(catalog_ev)
        if not isinstance(other, str):
            alt = at_markers(other[0], other[1], False, marks)
            if worst(alt) < worst(cut_pts):
                cut_pts, fields, ev = alt, other[2], catalog_ev
    if ev['circuit'] and cut_pts[3][0] is not None and cut_pts[3][0] > 5:
        # the circuit's start line on another road than its course (4002,
        # 22 m off, on the stadium road's other carriageway; L4RC's 4304,
        # 63 m): again from the line, kept when that brings the line onto
        # the course and changes it only near the line (4083's laid again
        # took another way 2 km on)
        again = lay_event(dict(ev, outline=circuit_from_line(ev['outline'], marker(marks, 'ForwardStartLine'))))
        if not isinstance(again, str):
            alt = at_markers(again[0], again[1], True, marks)
            if alt[3][0] < cut_pts[3][0] and changed_near_start(cut_pts[0], alt[0]):
                cut_pts, fields = alt, dict(again[2], from_line=round(cut_pts[3][0], 1))
    pts, gates, finish, line_off, anchor = cut_pts
    cut, pulled, moved, relayered, walked, followed = (fields[k] for k in ('kinks_cut', 'pulled', 'nudged', 'relayered', 'walked', 'followed'))
    crossings = fields['barrier_crossings']
    length = sum(math.dist(pts[k], pts[k + 1]) for k in range(len(pts) - 1))
    if ev['circuit']:
        length += math.dist(pts[-1], pts[0])
    marks_out = marks_record(ev['id'], pts, ev['circuit'], finish, marks, anchor)
    outline_len = sum(math.dist(ev['outline'][k], ev['outline'][k + 1]) for k in range(len(ev['outline']) - 1))
    out = bytearray(struct.pack('<HBBHHf', ev['id'], ev['circuit'], ev['hint'], len(pts), len(gates), length))
    for p in pts:
        out += struct.pack('<3f', *p)
    out += struct.pack(f'<{len(gates)}H', *gates)
    out += bytes((-len(out)) % 4)
    lanes_out = None
    if fields.get('axis'):   # (on the course as cut at the lines: at_markers)
        shapes = barrier_shapes(n2routes.read_event_barriers(args.game, args.track, ev['id']))
        others = [(num, rt, route_levels(rt, event_city)) for num, rt in info.items()
                  if DRAG_OTHER_ROUTES and n2routes.route_for(route_events, num, ev['id']) and len(rt) > 1]
        lanes, where, dist = drag_lanes(pts, fields['axis'], info, shapes, event_city, others)
        plan = drag_plan(lanes, where, dist, event_city, pts, shapes)
        lanes_out = bytearray(struct.pack('<2H', ev['id'], len(pts)))
        for row, go, at, q in zip(lanes, plan, where, pts):
            lanes_out += struct.pack('<4bB4b', *(NO_LANE if v is None else max(-127, min(127, round(v * LANE_UNITS))) for v in row),
                                     sum(g << (2 * k) for k, g in enumerate(go)),
                                     *(max(-127, min(127, round((a[2] - q[1]) * 8))) for a in at))
        lanes_out += bytes((-len(lanes_out)) % 4)
    entry = dict(id=ev['id'], circuit=ev['circuit'], points=len(pts), gates=len(gates), kinks_cut=cut, pulled=pulled, nudged=moved,
                 relayered=relayered, walked=walked, followed=followed, barrier_crossings=crossings, from_line=fields.get('from_line'),
                 length=round(length), outline=round(outline_len), finish=round(finish),
                 start_off=None if line_off[0] is None else round(line_off[0], 1),
                 finish_off=None if line_off[1] is None else round(line_off[1], 1))
    return entry, bytes(out), marks_out, lanes_out and bytes(lanes_out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--track', default='L4RA', help='L4RA the city (races.n2rc); another: races-<track>.n2rc')
    ap.add_argument('--only', help='comma-separated event ids (a trial run: the output has only those)')
    ap.add_argument('--jobs', type=int, default=1, help='events laid side by side, one process each (the output is the same)')
    args = ap.parse_args()
    only = {int(e) for e in args.only.split(',')} if args.only else None
    load_context(args, only)
    city_track, info, route_events = _CTX['city_track'], _CTX['info'], _CTX['route_events']
    events = [ev for ev in read_catalog(args.game, args.track) if not only or ev['id'] in only]
    if args.jobs > 1 and len(events) > 1:
        import concurrent.futures
        with concurrent.futures.ProcessPoolExecutor(args.jobs, initializer=load_context, initargs=(args, only)) as ex:
            done = list(ex.map(lay_event_record, events))
    else:
        done = [lay_event_record(ev) for ev in events]
    out, marks_out, lanes_out, report, kept, lanes_kept = bytearray(), bytearray(), bytearray(), [], 0, 0
    for entry, record, marks_record_, lanes_record in done:
        report.append(entry)
        if record is None:
            continue
        out += record
        marks_out += marks_record_
        kept += 1
        if lanes_record:
            lanes_out += lanes_record
            lanes_kept += 1
    name = 'races' if city_track else f'races-{args.track}'
    (args.out / f'{name}.n2rc').write_bytes(struct.pack('<4s2I', b'N2RC', 1, kept) + out)
    if not city_track:
        # drag-lanes-<track>.n2dl: magic, version, events; per event u16 id,
        # u16 points (its course's), then per point its four lanes' offsets,
        # s8 in 1/LANE_UNITS m right of the way (version 2; 1/8 m in
        # version 1, which held lanes to 15.9 m), NO_LANE where it is not there,
        # and a u8: the lane to head for from each lane (2 bits each,
        # drag_plan), then (version 2) each lane's height over the course's,
        # s8 in 1/8 m (another road's lane: under or over it); padded to 4 bytes
        lanes_path = args.out / f'drag-lanes-{args.track}.n2dl'
        lanes_path.unlink(missing_ok=True)   # (a link into another dist: never written through)
        if lanes_kept:
            lanes_path.write_bytes(struct.pack('<4s2I', b'N2DL', 2, lanes_kept) + lanes_out)
    (args.out / f'{name}.json').write_text(json.dumps(report, indent=1) + '\n')
    mark = 'marks' if city_track else f'marks-{args.track}'
    (args.out / f'{mark}.n2mk').write_bytes(struct.pack('<4s2I', b'N2MK', 1, kept) + marks_out)
    print(kept, 'events;', [r for r in report if 'error' in r])


if __name__ == '__main__':
    main()
