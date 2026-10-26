#!/usr/bin/env python3
"""The racing line's speed as the original game works it out at a race's
load -> linespeed.n2ls, beside lines.n2rl.

Each event has its own routes: TRACKS/ROUTESL4RA/ROUTES<id>F.BIN for the
course the way it is listed, ROUTES<id>B.BIN for the quick race's other
Track Direction (the routes, signposts and junctions of the free roam's
file, only those the race uses). They keep a speed for the racing line of
every point, the way the route runs (+0x2c) and against it (+0x2d), but
in the data it is a placeholder (100 m/s). Loading the race, the game sets
it afresh (the GameCube's TrackRouteManager::MakeDrivingLines, with
CurvaturePoint and CurvatureInfo):

  - every point starts at the top speed of a car class: the best class
    among the AI's cars (0 the fastest; GLOBALB 0x34A33: 100.6, 97.9, 95.2
    and 91.2 m/s);
  - each point is held to the speed a car of that class corners at,
    sqrt(mu g / k), mu 3.7, 3.4, 3.1 and 2.8 by class, with k the
    curvature of the circle through the point and the nearest points at
    least 30 m before and after it along the line (ten points at most;
    a point at the end of its chain keeps the top speed), on the racing
    line: the route's point moved sideways by its offset, +0x22 the way
    the route runs and +0x23 against it, in 128ths of the wider half
    width;
  - the lines join over the signposts racers take: an "always" sign
    carries the line on to the other route, an optional one adds a way
    out (types 7 and 8, ignored by racers, and the corners' interpolated
    signs, bit 26, join nothing, nor do routes flagged out of the graph or
    closed to racers: bits 9 and 10 of the route's flags, +0x68 in the
    file);
  - then the speeds are scrubbed until they settle: no point faster than
    the car can brake down from to the next one (12, 10.6, 9.3 and 8
    m/s^2 by class), nor than it can speed up to from the one before (the
    class's acceleration by speed: 0x34A33's 64 samples up to its top);
    the speeds are whole m/s at every step (a byte a point).

On the PS2 (the 4014 on Medium, its rivals of class 3), the EE RAM holds
these bytes during the race: 76% of the event's 1,254 recomputed points
match this rule to the m/s, 84% within 2 m/s, and along the race's line
the mean difference is 2.9 m/s.

The AI's driver then aims at the speed where it is, times its skill's
share and the catch-up's (AIGoal::SetDesiredSpeedAsRacer), and the
catch-up measures the player's pace against it (Player::GetSpeedRatio).

Here each point of the port's racing line takes the speed of the route
point nearest it (within 6 m, at its height, running its way), the gaps
between those filled along the line, and where none is near for over 90 m
the same rule run on the port's own line.

linespeed.n2ls: 'N2LS', u32 version 1, u32 count, count x (u16 event id,
u16 0, u32 points, u32 offset); at each offset 8 x points bytes: m/s at
each point of the event's line in lines.n2rl, run the way it is listed
for classes 0-3, then run the other way (still indexed as the file's
points) for classes 0-3.
"""
import argparse
import bisect
import collections
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from n2formats import archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
G = 9.8
FRICTION = (3.7, 3.4, 3.1, 2.8)      # the Speed.elf's fSmoothCurvatureCarFriction, by class
BRAKING = (12.0, 10.6, 9.3, 8.0)     # fSmoothCurvatureCarBraking (m/s^2)
PINCH = 30.0                         # m to the neighbours a curvature is measured against
PINCH_HOPS = 10
MIN_RADIUS = .001
NEAR = 6.0                           # m from a line point to the route point it takes
GAP = 30                             # line points (~90 m) filled between route points


def class_tables(game):
    """[(top m/s, [64 m/s^2 over 0..top])] for the four car classes."""
    d = archived(game, 'GLOBAL/GLOBALB.BUN')
    b = next(d[p:p + n] for t, p, n in chunks(d) if t == 0x34A33)
    out = []
    for c in range(4):
        top, = struct.unpack_from('<f', b, c * 0x10C)
        out.append((top, list(struct.unpack_from('<64f', b, c * 0x10C + 12))))
    return out


def accel_at(table, v):
    top, a = table
    x = max(0.0, min(63.0, v * 63 / top))
    i = min(62, int(x))
    return a[i] + (a[i + 1] - a[i]) * (x - i)


def circle_curvature(p, q, r):
    """1 / the radius of the circle through three points (2D), 0 if in line."""
    ax, ay, bx, by = p[0] - q[0], p[1] - q[1], r[0] - q[0], r[1] - q[1]
    cross = ax * by - ay * bx
    if abs(cross) < 1e-9:
        return 0.0
    a2, b2 = ax * ax + ay * ay, bx * bx + by * by
    ux, uy = (by * a2 - ay * b2) / (2 * cross), (ax * b2 - bx * a2) / (2 * cross)
    return 1.0 / max(MIN_RADIUS, math.hypot(ux, uy))


def speed_for(k, cls, top):
    return min(top, math.sqrt(FRICTION[cls] * G / k)) if k > 1e-9 else top


def pinch(pos, link, nd):
    """The first node at least PINCH m from nd along link (the next or the
    previous one), on the right side of nd's heading, within PINCH_HOPS;
    None where the chain ends first."""
    x, y, hx, hy = pos[nd][:4]
    step = 1 if link[0] else -1
    j = nd
    for _ in range(PINCH_HOPS):
        j = link[1].get(j)
        if j is None:
            return None
        q = pos[j]
        if ((q[0] - x) * hx + (q[1] - y) * hy) * step > 0 and math.hypot(q[0] - x, q[1] - y) >= PINCH:
            break
    return j


class EventRoutes:
    """An event's routes as the game's line graph: a node per route point
    and line (0 the way the route runs, 1 against it), at the racing
    line's place, with its heading and height."""

    def __init__(self, game, eid, suffix):
        d = archived(game, f'TRACKS/ROUTESL4RA/ROUTES{eid}{suffix}.BIN')
        leaf = next(d[p:p + n] for t, p, n in chunks(d) if t == 0x34121)
        signs = next(d[p:p + n] for t, p, n in chunks(d) if t == 0x34122)
        self.pos, count, self.no_racers = {}, {}, set()
        at = 8
        while at + 140 <= len(leaf):
            num, = struct.unpack_from('<H', leaf, at + 2)
            n, = struct.unpack_from('<H', leaf, at + 44)
            bools, = struct.unpack_from('<I', leaf, at + 0x68)
            if bools >> 9 & 1:            # out of the lines' graph
                at += 140 + 56 * n
                continue
            if bools >> 10 & 1:           # racers never take it
                self.no_racers.add(num)
            count[num] = n
            for k in range(n):
                p = at + 132 + 56 * k
                if p + 56 > len(leaf):
                    break
                x, y = struct.unpack_from('<2f', leaf, p)
                h, = struct.unpack_from('<h', leaf, p + 0x12)
                wl, wr, dx, dy = struct.unpack_from('<2H2h', leaf, p + 0x14)
                o0, o1 = struct.unpack_from('<2b', leaf, p + 0x22)
                w, dx, dy = max(wl, wr) / 256 / 128, dx / 32767, dy / 32767
                for line, o in ((0, o0), (1, o1)):
                    hx, hy = (dx, dy) if line == 0 else (-dx, -dy)
                    self.pos[(num, k, line)] = (x + hy * o * w, y - hx * o * w, hx, hy, h / 8)
            at += 140 + 56 * n
        nxt, prv = {}, {}
        for r, k, line in self.pos:
            k2 = k + 1 if line == 0 else k - 1
            if 0 <= k2 < count[r]:
                nxt[(r, k, line)] = (r, k2, line)
                prv[(r, k2, line)] = (r, k, line)
        self.ways = collections.defaultdict(list)   # optional signs' ways out
        for at in range(4, len(signs) - 31, 32):
            newp, oldp, newr, oldr, flags = struct.unpack_from('<4HI', signs, at + 4)
            kind = flags & 15
            if kind in (7, 8) or flags >> 26 & 1 or newr == oldr or newr not in count or oldr not in count \
                    or newr in self.no_racers or oldr in self.no_racers:
                continue
            if oldp >= count[oldr] or newp >= count[newr]:
                continue
            back, flip = flags >> 18 & 1, flags >> 23 & 1
            a, b = (oldr, oldp, back), (newr, newp, back ^ flip)
            if kind == 2:
                nxt[a] = b
                prv[b] = a
            else:
                self.ways[a].append(b)
        self.next, self.prev = nxt, prv

    def speeds(self, cls, table):
        """{node: m/s} by the game's rule for car class cls."""
        top = table[0]
        v = {}
        for nd, p in self.pos.items():
            k = 0.0
            if nd in self.next and nd in self.prev:
                a, b = pinch(self.pos, (False, self.prev), nd), pinch(self.pos, (True, self.next), nd)
                if a and b:
                    k = circle_curvature(self.pos[a], p, self.pos[b])
            v[nd] = float(int(speed_for(k, cls, top)))
        edges = [(a, b) for a, b in self.next.items()] + [(a, b) for a, bs in self.ways.items() for b in bs]
        dist = [math.hypot(self.pos[a][0] - self.pos[b][0], self.pos[a][1] - self.pos[b][1]) for a, b in edges]
        brake = BRAKING[cls]
        for _ in range(400):
            changed = False
            for order in (range(len(edges)), range(len(edges) - 1, -1, -1)):
                for i in order:
                    a, b = edges[i]
                    w = int(math.sqrt(v[b] * v[b] + 2 * brake * dist[i]))
                    if w < v[a]:
                        v[a], changed = w, True
                    w = int(math.sqrt(v[a] * v[a] + 2 * accel_at(table, v[a]) * dist[i]))
                    if w < v[b]:
                        v[b], changed = w, True
            if not changed:
                break
        return v

    def nearest(self, line, reverse):
        """For each port line point (GU x, y, z) the node nearest it within
        NEAR m, at its height and running its way (None if none)."""
        grid = collections.defaultdict(list)
        for nd, p in self.pos.items():
            grid[(int(p[0] // NEAR), int(p[1] // NEAR))].append(nd)
        n, out = len(line), []
        for j, q in enumerate(line):
            a, b = line[j - 1] if j > 0 else q, line[(j + 1) % n] if j < n - 1 else q
            tx, ty = b[2] - a[2], b[0] - a[0]
            if reverse:
                tx, ty = -tx, -ty
            tl = math.hypot(tx, ty) or 1.0
            x, y, best = q[2], q[0], None
            for gi in range(int(x // NEAR) - 1, int(x // NEAR) + 2):
                for gj in range(int(y // NEAR) - 1, int(y // NEAR) + 2):
                    for nd in grid.get((gi, gj), ()):
                        p = self.pos[nd]
                        d = math.hypot(p[0] - x, p[1] - y)
                        if d <= NEAR and abs(p[4] - q[1]) < 4 and (p[2] * tx + p[3] * ty) / tl > .9 and (best is None or d < best[0]):
                            best = (d, nd)
            out.append(best[1] if best else None)
        return out


def own_limits(line, circuit, cls, top):
    """The rule's corner limit on the port's line itself (as a chain)."""
    n = len(line)
    pos, nxt, prv = {}, {}, {}
    for i, p in enumerate(line):
        a, b = line[i - 1] if circuit or i > 0 else p, line[(i + 1) % n] if circuit or i < n - 1 else p
        hx, hy = b[2] - a[2], b[0] - a[0]
        hl = math.hypot(hx, hy) or 1.0
        pos[i] = (p[2], p[0], hx / hl, hy / hl)
        if circuit or i < n - 1:
            nxt[i] = (i + 1) % n
            prv[(i + 1) % n] = i
    out = []
    for i in range(n):
        k = 0.0
        if i in nxt and i in prv:
            a, b = pinch(pos, (False, prv), i), pinch(pos, (True, nxt), i)
            if a is not None and b is not None:
                k = circle_curvature(pos[a], pos[i], pos[b])
        out.append(speed_for(k, cls, top))
    return out


def scrub_line(v, line, circuit, table, brake, reverse, fixed):
    """The same scrub along the port's line (the way it is run), changing
    only the points not fixed (those the routes gave keep theirs)."""
    n = len(v)
    order = list(range(n))[::-1] if reverse else list(range(n))
    v = [v[i] for i in order]
    fixed = [fixed[i] for i in order]
    seg = []
    for a, b in zip(order, order[1:] + order[:1]):
        seg.append(math.hypot(line[b][0] - line[a][0], line[b][2] - line[a][2]))
    m = n if circuit else n - 1
    for _ in range(20):
        changed = False
        for i in list(range(m)) + list(range(m - 1, -1, -1)):
            j = (i + 1) % n
            w = math.sqrt(v[j] * v[j] + 2 * brake * seg[i])
            if w < v[i] - 1e-6 and not fixed[i]:
                v[i], changed = w, True
            w = math.sqrt(v[i] * v[i] + 2 * accel_at(table, v[i]) * seg[i])
            if w < v[j] - 1e-6 and not fixed[j]:
                v[j], changed = w, True
        if not changed:
            break
    out = [0.0] * n
    for k, i in enumerate(order):
        out[i] = v[k]
    return out


def line_speeds(line, circuit, routes, reverse, cls, table):
    """m/s at each port line point, run forwards or reversed."""
    n = len(line)
    v = routes.speeds(cls, table)
    near = routes.nearest(line, reverse)
    got = [v[nd] if nd else None for nd in near]
    own = None
    out = list(got)
    known = [i for i in range(n) if got[i] is not None]
    for i in range(n):
        if got[i] is not None:
            continue
        # the nearest known points either side along the line
        at = bisect.bisect_left(known, i)
        lo = known[at - 1] if at > 0 else None
        hi = known[at] if at < len(known) else None
        if circuit:
            if lo is None and known:
                lo = known[-1] - n
            if hi is None and known:
                hi = known[0] + n
        if lo is not None and hi is not None and hi - lo <= GAP:
            a, b = got[lo % n], got[hi % n]
            out[i] = a + (b - a) * (i - lo) / (hi - lo)
        else:
            if own is None:
                own = own_limits(line, circuit, cls, table[0])
            out[i] = own[i]
    out = scrub_line(out, line, circuit, table, BRAKING[cls], reverse, [g is not None for g in got])
    return [max(1, min(255, int(x))) for x in out], len(known) / n


def read_lines(path):
    d = Path(path).read_bytes()
    magic, version, count = struct.unpack_from('<4s2I', d, 0)
    assert magic == b'N2RL' and version == 1
    out = []
    for k in range(count):
        eid, circ, n, off = struct.unpack_from('<2H2I', d, 12 + 12 * k)
        pts = [struct.unpack_from('<4f', d, off + 20 * i) for i in range(n)]
        out.append(dict(id=eid, circuit=circ, line=[p[:3] for p in pts]))
    return out


def lap_time(line, circuit, v):
    n = len(line)
    t = 0.0
    for i in range(n if circuit else n - 1):
        j = (i + 1) % n
        t += math.hypot(line[j][0] - line[i][0], line[j][2] - line[i][2]) / max(1.0, (v[i] + v[j]) * .5)
    return t


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--only', type=int, nargs='*', help='event ids (a trial: the output has only those)')
    args = ap.parse_args()
    tables = class_tables(args.game)
    lines = read_lines(args.out / 'lines.n2rl')
    if args.only:
        lines = [e for e in lines if e['id'] in args.only]
    table, blob, report = [], bytearray(), {}
    for ev in lines:
        line, circuit = ev['line'], bool(ev['circuit'])
        arrays, rep = [], {}
        for reverse, suffix in ((False, 'F'), (True, 'B')):
            routes = EventRoutes(args.game, ev['id'], suffix)
            for cls in range(4):
                v, cover = line_speeds(line, circuit, routes, reverse, cls, tables[cls])
                arrays.append(v)
                if not reverse:
                    rep[f'class{cls}'] = dict(mean=round(sum(v) / len(v), 1), slowest=min(v), lap_s=round(lap_time(line, circuit, v), 1))
            rep['from_routes' + ('_back' if reverse else '')] = round(cover, 3)
        table.append((ev['id'], len(line), len(blob)))
        for arr in arrays:
            blob += bytes(arr)
        report[ev['id']] = rep
        print(ev['id'], rep, flush=True)
    head = struct.pack('<4s2I', b'N2LS', 1, len(table))
    base = len(head) + 12 * len(table)
    head += b''.join(struct.pack('<2H2I', eid, 0, n, base + off) for eid, n, off in table)
    (args.out / 'linespeed.n2ls').write_bytes(head + blob)
    (args.out / 'linespeed.json').write_text(json.dumps(report, indent=1) + '\n')


if __name__ == '__main__':
    main()
