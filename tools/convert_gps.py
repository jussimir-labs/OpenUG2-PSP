#!/usr/bin/env python3
"""The street GPS's road network, worked out as the original's free roam
loads it, per career stage -> gpsnet.n2gn beside roads.n2rd.

The original (GameCube Speed.elf: InitTrackRoutes, TrackRouteManager,
Signpost, TrackRoutePoint, PathfindingNode/Manager) finds the GPS's way
over the city's routes (ROUTESFREEROAM.BIN leaf 0x34121) and signposts
(leaf 0x34122), not over a graph of welded street ends:

- A route is driven point by point, either way. A signpost stands at a
  point of one route and leads to a point of another; it serves cars going
  one way along its route (bit 18 set: against the route's points) and can
  put them on the new route against its points (bit 23).
- The player's sign type is the racers' (bits 0-3, restored at each load
  from the copy in bits 8-11): 2 Always and 8 Player Always are taken
  whenever met, 7 None is not there, 5 Don't Take and 10 Never are not
  taken; the others (0 Faster, 1 Slower, 4 Equal, 6, 9) are a choice.
- At each load (free roam: every route kept but those left out of track
  4000, none in the city), with the career stage's barriers up
  (BARRIERS_CAREER<stage>, PATHSFREEROAM.BIN leaf 0x3414d, only for the
  player and the racers):
  - a choice that would cross a barrier within 200 m of road (or on its
    own jump) is closed, or made an Always when going on along the old
    route is what crosses one; an Always that crosses one is closed and
    the road before it made unreachable: the nearest choice back up the
    old route becomes an Always, and the signs leading into that stretch
    the other way are closed (Signpost::EliminateBarrierViolations,
    MakeUnreachable, TrackRoutePoint::WillCrossBarrier); repeated until
    nothing changes;
  - then which points are "posted" for the player is worked out again:
    from each sign that is not None, down its new route, stretch by
    stretch between signs, until a point already posted that way, an
    Always or Stop sign the same way, or the route's end (the last
    stretch before a dead end stays unposted)
    (TrackRouteManager::DeterminePostedness/MarkPostedness).
- The GPS's A* walks only onto points posted forwards for the player (bit
  27, whichever way it drives), takes every Always it meets, branches at
  the choices (each one, or going on), costs metres along the routes (a
  sign's jump: its projection on the way), aims straight at the target, and
  ends at the first point whose stretch the target lies in, within the
  road's widths and 4 m of height (PathfindingNode::Traverse,
  FigureOutSuccessors, CloseEnoughToGoal, EstimateCostToGoal). It starts
  at the posted point under the car, both ways
  (PathfindingManager::BuildInitialOpenList).
- The port's one addition: the original looks for barriers only 200 m on
  from each sign, and a barrier farther down a route is crossed by its
  way (stage 0: two, 206 and 225 m on, into walls the car cannot pass).
  Here each route stretch and each sign's jump across a barrier of the
  stage is cut (tools/gps_check.py counts the ways that cross one).

gpsnet.n2gn (v1, little endian):
  magic 'N2GN', u32 version, u32 points, u32 signs, u32 stages
  points x u8: bit 0 the last point of its route (no next one)
  signs x 6 B: u16 old point, u16 new point (route points, numbered as the
    nodes of roads.n2rd: every route's points in order), u8 flags (bit 0
    the sign serves cars going against the old route's points, bit 1 the
    new route is driven against its points), u8 pad; sorted by old point
  per stage: signs x u8 the player's sign type at that stage (7 = not
    there), then two bitmaps of points / 8 rounded up x u8 (bit k of byte
    k / 8, least significant first): the points posted for the player, and
    the points whose stretch to the next point crosses a barrier
"""
import argparse
import collections
import json
from pathlib import Path
import struct

from n2formats import archived, chunks
from n2routes import name_hash

ROOT = Path(__file__).resolve().parents[1]
STAGES = 6           # bar-free<n>.n2cy's: the career's stages 0-4, 5 the last (and no career)
CHECK_M = 200.0      # WillCrossBarrier looks this far down the road
RACER, TRAFFIC, PLAYER = 0, 1, 2


class Net:
    """The routes and signposts, with the state the original keeps on them."""

    def __init__(self, game, track='L4RA'):
        d = archived(game, f'TRACKS/ROUTES{track}/ROUTESFREEROAM.BIN')
        leaf = [d[p:p+n] for t, p, n in chunks(d) if t == 0x34121][0]
        self.x, self.y, self.flags, self.length, self.wl, self.wr, self.dx, self.dy = ([] for _ in range(8))
        self.route_of, self.base, self.count, self.number, self.excluded, self.bbox = [], [], [], [], [], []
        at = 8
        while at + 140 <= len(leaf):
            num, = struct.unpack_from('<H', leaf, at + 2)
            n, = struct.unpack_from('<H', leaf, at + 44)
            pts = [at + 132 + 56 * k for k in range(n) if at + 132 + 56 * k + 56 <= len(leaf)]
            if len(pts) >= 2:      # as tools/convert_routes.py numbers the road graph's nodes
                r = len(self.base)
                self.base.append(len(self.x))
                self.count.append(len(pts))
                self.number.append(num)
                self.excluded.append(struct.unpack_from('<4H', leaf, at + 0x3c))
                self.bbox.append(struct.unpack_from('<4f', leaf, at + 0x58))
                for p in pts:
                    x, y = struct.unpack_from('<2f', leaf, p)
                    fl, = struct.unpack_from('<I', leaf, p + 0x0c)
                    ln, = struct.unpack_from('<H', leaf, p + 0x10)
                    wl, wr, dx, dy = struct.unpack_from('<2H2h', leaf, p + 0x14)
                    self.x.append(x); self.y.append(y); self.flags.append(fl); self.length.append(ln / 256)
                    self.wl.append(wl / 256); self.wr.append(wr / 256); self.dx.append(dx / 32767); self.dy.append(dy / 32767)
                    self.route_of.append(r)
            at += 140 + 56 * n
        index = {num: r for r, num in enumerate(self.number)}
        sl = [d[p:p+n] for t, p, n in chunks(d) if t == 0x34122][0]
        self.sign_old, self.sign_new, self.sign_back, self.sign_flip, self.sign_flags = [], [], [], [], []
        for a in range(4, len(sl) - 31, 32):
            newp, oldp, newr, oldr, fl = struct.unpack_from('<4HI', sl, a + 4)
            if oldr not in index or newr not in index:
                continue
            self.sign_old.append(self.base[index[oldr]] + oldp)
            self.sign_new.append(self.base[index[newr]] + newp)
            self.sign_back.append(fl >> 18 & 1)
            self.sign_flip.append(fl >> 23 & 1)
            self.sign_flags.append(fl)
        self.npts, self.nsigns = len(self.x), len(self.sign_old)
        self.at_point = collections.defaultdict(list)
        for s in range(self.nsigns):
            self.at_point[self.sign_old[s]].append(s)

    # -- the route's points
    def next_point(self, p, forward):
        """GetRelativePoint: the next point along (forward) or against the
        route, None past its ends."""
        r = self.route_of[p]
        q = p + 1 if forward else p - 1
        return q if self.base[r] <= q < self.base[r] + self.count[r] else None

    # -- the signs' types, per postedness kind
    def sign_type(self, s, kind):
        return self.types[1][s] if kind == TRAFFIC else self.types[0][s]

    def set_type(self, s, kind, t):
        self.types[1 if kind == TRAFFIC else 0][s] = t

    def ignore(self, s, kind):
        t = self.sign_type(s, kind)
        return t in (7, 8) if kind == RACER else t == 7

    def always(self, s, kind):
        t = self.sign_type(s, kind)
        return t in (2, 8) if kind == PLAYER else t == 2

    def dont_take(self, s, kind):
        return self.sign_type(s, kind) in (5, 10)

    def never_take(self, s, kind):
        return self.sign_type(s, kind) == 10

    def serves(self, s, forward):
        """The sign is for cars going this way along its old route."""
        return bool(forward) != bool(self.sign_back[s])

    def signs_at(self, p):
        return [s for s in self.at_point.get(p, ()) if self.alive[s]]

    def twin(self, s):
        """Signpost::GetTwin: the sign back from the new point to the old."""
        o, n = self.sign_old[s], self.sign_new[s]
        if n == o:
            return None
        want = self.sign_back[s] == self.sign_flip[s]   # it serves the way back along the new route
        for t in self.signs_at(n):
            if bool(self.sign_back[t]) == want and self.sign_new[t] == o:
                return t
        return None

    # -- load
    def restore(self):
        """RestoreSignposts and the free roam's FixupDanglers: every route is
        kept (none is left out of track 4000), so every sign stays."""
        self.types = [[f >> 8 & 15 for f in self.sign_flags], [f >> 12 & 15 for f in self.sign_flags]]
        self.alive = [True] * self.nsigns
        self.relevant = [4000 not in ex for ex in self.excluded]
        assert all(self.relevant), 'a route left out of the free roam: FixupDanglers to do'

    def set_barriers(self, segs):
        """Barrier segments ((ax, ay), (bx, by), player_only) up; the routes
        whose box overlaps one's are checked (CheckForBarrierOverlap)."""
        self.barriers = segs
        self.near_barrier = []
        for x0, y0, x1, y1 in self.bbox:
            hit = False
            for (ax, ay), (bx, by), _ in segs:
                if min(ax, bx) <= x1 and max(ax, bx) >= x0 and min(ay, by) <= y1 and max(ay, by) >= y0:
                    hit = True
                    break
            self.near_barrier.append(hit)

    def crosses(self, p, q, kind):
        """CrossesBarrier between two points (traffic sees no player-only one)."""
        ax, ay, bx, by = self.x[p], self.y[p], self.x[q], self.y[q]
        for (cx, cy), (dx, dy), player_only in self.barriers:
            if kind == TRAFFIC and player_only:
                continue
            if segments_cross(ax, ay, bx, by, cx, cy, dx, dy):
                return True
        return False

    def next_on_segment(self, p, forward, kind):
        """GetNextPointOnSegment (posting ignored): an Always the same way
        is taken, else the next point along."""
        for s in self.signs_at(p):
            if self.serves(s, forward) and self.always(s, kind):
                return self.sign_new[s], forward != bool(self.sign_flip[s])
        q = self.next_point(p, forward)
        return q, forward

    def will_cross(self, p, kind, forward, sign, start):
        """TrackRoutePoint::WillCrossBarrier."""
        if start is not None and self.crosses(start, p, kind):
            return True
        if not self.near_barrier[self.route_of[p]]:
            return False
        dist = 0.0
        while p is not None and dist < CHECK_M:
            for s in self.signs_at(p):
                if s != sign and not self.ignore(s, kind) and self.serves(s, forward):
                    if self.route_of[self.sign_old[s]] != self.route_of[self.sign_new[s]]:
                        return False
            q, forward = self.next_on_segment(p, forward, kind)
            if q is None:
                break
            if self.crosses(p, q, kind):
                return True
            dist += ((self.x[p] - self.x[q]) ** 2 + (self.y[p] - self.y[q]) ** 2) ** .5
            p = q
        return False

    def most_fruitful_exit(self, p, forward, kind):
        """TrackRoutePoint::MostFruitfulExit, with no race's distances (all
        0 in free roam): the first of the point's two signs that leaves the
        route the given way."""
        for s in self.signs_at(p)[:2]:
            if not self.ignore(s, kind) and self.route_of[self.sign_old[s]] != self.route_of[self.sign_new[s]] \
                    and self.serves(s, forward):
                return s
        return None

    def make_unreachable(self, sign, kind, depth):
        """Signpost::MakeUnreachable: up the old route from the sign, the
        nearest exit becomes an Always; the signs leading in the other way
        are closed."""
        if depth > 100:
            return
        old_route = self.route_of[self.sign_old[sign]]
        if not self.relevant[old_route]:
            return
        back = self.sign_back[sign]
        p = self.sign_old[sign]
        while p is not None:
            for s in self.signs_at(p):
                if s == sign or not self.serves(s, not back) or self.ignore(s, kind) or self.never_take(s, kind):
                    continue
                new_route = self.route_of[self.sign_new[s]]
                if new_route == old_route:
                    continue
                if self.relevant[new_route]:
                    if self.always(s, kind):
                        return
                    if self.sign_flags[s] >> 26 & 1:
                        s = self.most_fruitful_exit(p, not back, kind)
                    if s is not None:
                        self.set_type(s, kind, 2)
                    return
                self.set_type(s, kind, 7)
            for s in self.signs_at(p):
                if s == sign or self.serves(s, not back):
                    continue
                t = self.twin(s)
                if t is None or self.ignore(t, kind) or self.never_take(t, kind) or self.sign_type(s, kind) == 3:
                    continue
                if self.relevant[self.route_of[self.sign_old[t]]]:
                    was_always = self.always(t, kind)
                    self.set_type(t, kind, 7)
                    if was_always:
                        self.make_unreachable(t, kind, depth + 1)
            p = self.next_point(p, bool(back))

    def eliminate_barrier_violations(self):
        changed = False
        for s in range(self.nsigns):
            if not self.alive[s]:
                continue
            for kind in (RACER, TRAFFIC, PLAYER):
                if self.ignore(s, kind):
                    continue
                old, new = self.sign_old[s], self.sign_new[s]
                fwd_old = not self.sign_back[s]
                fwd_new = fwd_old != bool(self.sign_flip[s])
                t = self.sign_type(s, kind)
                close = always = unreach = False
                if t == 2:
                    if self.will_cross(new, kind, fwd_new, s, old):
                        close = unreach = True
                elif t in (0, 1, 4, 6, 9):
                    stay = self.will_cross(old, kind, fwd_old, s, None)
                    take = self.will_cross(new, kind, fwd_new, s, old)
                    if stay and take:
                        close = unreach = True
                    elif take:
                        close = True
                    elif stay:
                        always = True
                if close:
                    self.set_type(s, kind, 7)
                if always:
                    self.set_type(s, kind, 2)
                if unreach:
                    self.make_unreachable(s, kind, 1)
                changed |= close or always or unreach
        return changed

    def postedness(self, kind):
        """DeterminePostedness/MarkPostedness: {point: (forward, backward)}."""
        posted = [[False, False] for _ in range(self.npts)]
        for s in range(self.nsigns):
            if not self.alive[s] or self.ignore(s, kind):
                continue
            t = self.sign_type(s, kind)
            if t == 3:
                if (self.sign_flags[s] >> 8 & 15) != 3 and kind != TRAFFIC:
                    continue                                   # its old route is no race's (bool 7)
                forward = bool(self.sign_back[s])
            elif t in (0, 1, 2, 4, 5, 8, 9, 10):
                forward = (not self.sign_back[s]) != bool(self.sign_flip[s])
            else:
                continue
            self.mark(posted, self.sign_new[s], forward, kind)
        return posted

    def mark(self, posted, start, forward, kind):
        way = 0 if forward else 1      # route bit 1 (native direction) is 0 in free roam
        last, p, pending = start, start, False
        while True:
            p = self.next_point(p, forward)
            stop = False
            if p is None:
                stop = True
            elif posted[p][way]:
                stop = pending = True
            else:
                for s in self.signs_at(p):
                    if self.ignore(s, kind):
                        continue
                    t = self.sign_type(s, kind)
                    same = self.serves(s, forward)
                    stop = same and (t == 3 or self.always(s, kind))
                    pending = True
                    if t == 3:
                        pending = (self.sign_flags[s] >> 8 & 15) == 3 or kind == TRAFFIC
            if pending:
                pending = False
                lo, hi = sorted((last, p))
                for q in range(lo, hi + 1):
                    posted[q][way] = True
                last = p
            if stop:
                return


def segments_cross(ax, ay, bx, by, cx, cy, dx, dy):
    """DoLinesIntersect: the two segments cross (touching counts)."""
    d = (bx - ax) * (dy - cy) - (by - ay) * (dx - cx)
    if abs(d) < 1e-12:
        return False
    t = ((cx - ax) * (dy - cy) - (cy - ay) * (dx - cx)) / d
    u = ((cx - ax) * (by - ay) - (cy - ay) * (bx - ax)) / d
    return 0 <= t <= 1 and 0 <= u <= 1


def stage_barriers(game, stage, track='L4RA'):
    """The free roam's barriers up at a career stage (TrackLoader::
    InitTopologyAndSceneryGroups): BARRIERS_4000 for everyone,
    PLAYER_BARRIERS_4000 and BARRIERS_CAREER<stage> for the player and
    the racers (none of the 4000 ones exist)."""
    d = archived(game, f'TRACKS/ROUTES{track}/PATHSFREEROAM.BIN')
    leaf = [d[p:p+n] for t, p, n in chunks(d) if t == 0x3414d]
    if not leaf:
        return []
    groups = {name_hash('BARRIERS_4000'): False, name_hash('PLAYER_BARRIERS_4000'): True,
              name_hash(f'BARRIERS_CAREER{stage}'): True}
    out = []
    for at in range(0, len(leaf[0]) - 35, 36):
        ax, ay, bx, by = struct.unpack_from('<4f', leaf[0], at)
        n = min(leaf[0][at + 0x11], 4)
        hit = [groups[h] for h in struct.unpack_from(f'<{n}I', leaf[0], at + 0x14) if h in groups]
        if hit:
            out.append(((ax, ay), (bx, by), all(hit)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    net = Net(args.game)
    order = sorted(range(net.nsigns), key=lambda s: (net.sign_old[s], s))
    out = bytearray(struct.pack('<4s4I', b'N2GN', 1, net.npts, net.nsigns, STAGES))
    for p in range(net.npts):
        out.append(1 if net.next_point(p, True) is None else 0)
    for s in order:
        out += struct.pack('<2H2B', net.sign_old[s], net.sign_new[s],
                           net.sign_back[s] | net.sign_flip[s] << 1, 0)
    report = dict(points=net.npts, signs=net.nsigns, stages={})
    # The bits the data carries (the free roam with no career barrier) as a check.
    net.restore()
    net.set_barriers([])
    while net.eliminate_barrier_violations():
        pass
    base = net.postedness(PLAYER)
    stored = [(f >> 27 & 1, f >> 28 & 1) for f in net.flags]
    report['postedness_vs_data'] = sum(tuple(map(int, b)) != s for b, s in zip(base, stored))
    for stage in range(STAGES):
        net.restore()
        bars = stage_barriers(args.game, stage)
        net.set_barriers(bars)
        rounds = 0
        while net.eliminate_barrier_violations() and rounds < 50:
            rounds += 1
        posted = net.postedness(PLAYER)
        types = [net.sign_type(s, PLAYER) if net.alive[s] else 7 for s in order]
        changed = sum(t != (net.sign_flags[s] >> 8 & 15) for t, s in zip(types, order))
        cut = [False] * net.npts
        for p in range(net.npts):
            q = net.next_point(p, True)
            cut[p] = q is not None and net.crosses(p, q, PLAYER)
        jumps = 0
        for k, s in enumerate(order):
            if types[k] != 7 and net.crosses(net.sign_old[s], net.sign_new[s], PLAYER):
                types[k] = 7
                jumps += 1
        out += bytes(types)
        for flags in (posted, cut):
            bits = bytearray((net.npts + 7) // 8)
            for p in range(net.npts):
                if (flags[p][0] if flags is posted else flags[p]):
                    bits[p >> 3] |= 1 << (p & 7)
            out += bits
        report['stages'][stage] = dict(barriers=len(bars), rounds=rounds + 1, signs_changed=changed,
                                       stretches_cut=sum(cut), jumps_cut=jumps,
                                       posted=sum(1 for p in posted if p[0]),
                                       unposted_vs_free=sum(1 for a, b in zip(posted, base) if b[0] and not a[0]))
    (args.out / 'gpsnet.n2gn').write_bytes(out)
    report['bytes'] = len(out)
    (args.out / 'gpsnet.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
