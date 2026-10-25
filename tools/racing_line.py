#!/usr/bin/env python3
"""Racing lines for the race AI: races.n2rc + city.n2cy -> lines.n2rl.

The course of each event (tools/convert_races.py) follows the road graph,
which runs down the road's centre or a lane and turns at junctions far
tighter than the arcade drive model can: it turns the heading at most
PHYS_TURN rad per tick, scaled down below 9 m/s (PHYS_TURN_RAMP), so no
car turns tighter than ~6.6 m. A driver aiming at the course hits walls.

Here the course is resampled every STEP metres, the free road on each side
is measured on the collision grid (walls, voids and steps over 0.4 m end
it, as in the game's wall test; the car's half width and a margin come
off), and the line is pulled tight inside that corridor - an elastic band
relaxed coarse to fine to minimise its curvature. Per point the file
keeps the line, its signed curvature and the corridor around it, so the
driver can plan speed (psp-viewer/race.c) and move aside to pass.

lines.n2rl: 'N2RL', u32 version 1, u32 count, count x (u16 event id,
u16 circuit, u32 points, u32 offset); at each offset, points x 20 bytes:
f32 GU x, y (ground under the line), z; f32 curvature (1/m, + = turning
left); s8 room left, s8 right (decimetres from the line, >= 0); u16 crest
speed (0.1 m/s; 0 = none): over a crest a car leaves the ground above
sqrt(g / vertical curvature), and in the air it cannot steer.
"""
import argparse
import json
import math
from pathlib import Path
import struct

import mac_slots
from n2city import CityCollision

ROOT = Path(__file__).resolve().parents[1]
STEP = 3.0          # m between line points
REACH = 12.0        # m searched on each side of the course
HALF_CAR = 1.0      # m, half the widest body (+ mirrors)
MARGIN = 0.8        # m kept clear of walls (the driver tracks the line within ~0.7 m rms)
LEDGE = 0.4         # m: a step this high ends the road (vehicle.c)
TURN = 0.024 * 60   # rad/s at full steering lock, low-speed ramp aside


def read_courses(path):
    d = Path(path).read_bytes()
    magic, version, count = struct.unpack_from('<4s2I', d, 0)
    assert magic == b'N2RC'
    at, out = 12, []
    for _ in range(count):
        eid, circuit, hint, npts, ngates, length = struct.unpack_from('<HBBHHf', d, at)
        at += 12
        pts = [struct.unpack_from('<3f', d, at + 12 * k) for k in range(npts)]
        at += 12 * npts + 2 * ngates
        at += (-at) % 4
        out.append(dict(id=eid, circuit=circuit, pts=pts))
    return out


def read_anchors(path):
    """{event: where its circuit's line is laid from} (marks.n2mk's anchor,
    tools/convert_races.py), {} without the file."""
    if not Path(path).exists():
        return {}
    d = Path(path).read_bytes()
    count, at, out = struct.unpack_from('<I', d, 8)[0], 12, {}
    for _ in range(count):
        eid = struct.unpack_from('<H', d, at)[0]
        out[eid] = struct.unpack_from('<f', d, at + 16)[0]
        at += 20 + 2 * 8 * 16
    return out


def from_anchor(pts, anchor):
    """A circuit's points from its anchor round (the course laid before it
    was turned to start at its start line): the vertex there first, and the
    start line's own point dropped where it only splits a straight, so the
    line comes out as it was laid from that course (the optimiser's answer
    depends on where the loop is cut)."""
    if anchor <= 0:
        return pts
    s, k = 0.0, 0
    for k in range(1, len(pts)):
        s += math.hypot(pts[k][0] - pts[k - 1][0], pts[k][2] - pts[k - 1][2])
        if s >= anchor - .01:
            break
    a, b, c = pts[-1], pts[0], pts[1]
    cross = (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])
    lin = abs(cross) / max(1e-6, math.hypot(c[0] - a[0], c[2] - a[2])) < 1e-3
    rot = pts[k:] + pts[:k]
    if lin:
        rot.remove(b)
    return rot


def resample(pts, circuit, step=STEP):
    """Points every step metres along the polyline (closed for circuits)."""
    loop = list(pts) + ([pts[0]] if circuit else [])
    out, carry = [loop[0]], 0.0
    for a, b in zip(loop, loop[1:]):
        seg = math.hypot(b[0] - a[0], b[2] - a[2])
        d = step - carry
        while d <= seg:
            t = d / seg
            out.append(tuple(a[c] + (b[c] - a[c]) * t for c in range(3)))
            d += step
        carry = seg - (d - step)
    if circuit and math.hypot(out[-1][0] - out[0][0], out[-1][2] - out[0][2]) < step * .5:
        out.pop()
    elif not circuit and out[-1] != loop[-1]:
        out.append(loop[-1])
    return out


def despike(pts, circuit, limit=110.0, city=None):
    """Drop points where the course doubles back (a turn sharper than limit
    degrees): spurs left by snapping it onto the road graph, which the
    corridor would otherwise follow out and back. Not where the shortcut
    runs through a wall (city): there the course really turns back round
    something (the 4083 goes down a slip road and up the next round the
    gore between them, and the line cut straight through the gore's wall
    into it)."""
    cos_lim = math.cos(math.radians(limit))
    out = []
    for p in pts:
        out.append(p)
        while len(out) >= 3:
            a, b, c = out[-3], out[-2], out[-1]
            ux, uz, vx, vz = b[0] - a[0], b[2] - a[2], c[0] - b[0], c[2] - b[2]
            lu, lv = math.hypot(ux, uz), math.hypot(vx, vz)
            if lu > 1e-6 and lv > 1e-6 and (ux * vx + uz * vz) / (lu * lv) < cos_lim and not walled(city, a, c):
                del out[-2]
            else:
                break
    return out


def walled(city, a, c):
    """The straight centre line from a to c (GU) crosses a wall, a void or
    a step (every 0.5 m, on the layer nearest the interpolated height)."""
    if city is None:
        return False
    n = max(1, int(math.hypot(c[0] - a[0], c[2] - a[2]) * 2))
    for k in range(1, n):
        t = k / n
        x, y, z = a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t, a[2] + (c[2] - a[2]) * t
        if city.blocked(z, x, y):
            return True
    return False


def reference(city, pts, circuit, span=4):
    """Course smoothed over +-span points as the corridor's centre line,
    keeping the original point where the average would sit in a wall."""
    n, out = len(pts), []
    for i, p in enumerate(pts):
        win = [pts[(i + d) % n] if circuit else pts[min(max(i + d, 0), n - 1)] for d in range(-span, span + 1)]
        q = (sum(w[0] for w in win) / len(win), p[1], sum(w[2] for w in win) / len(win))
        out.append(p if city.blocked(q[2], q[0], p[1]) else q)
    return out


def normals(pts, circuit):
    """Unit right-hand normals in GU x/z (the viewer's lane convention:
    right of direction (dx, dz) is (-dz, dx))."""
    n = len(pts)
    out = []
    for i in range(n):
        a = pts[(i - 1) % n] if circuit or i > 0 else pts[i]
        b = pts[(i + 1) % n] if circuit or i < n - 1 else pts[i]
        dx, dz = b[0] - a[0], b[2] - a[2]
        l = math.hypot(dx, dz) or 1.0
        out.append((-dz / l, dx / l))
    return out


def free_extent(city, p, nrm, side, half=2.4):
    """Metres of drivable ground from p towards side (+1 right, -1 left),
    on the ground layer nearest the course height (the road graph's
    heights are off the collision by up to a metre and more on bridges)."""
    here = city.layers(p[2], p[0])
    if not here:
        return 0.0
    h = min(here, key=lambda v: abs(v - p[1]))
    tx, tz = nrm[1], -nrm[0]                 # along the course
    # a strip as long as the car (the game's wall test reaches 2.3 m fore
    # and aft of its centre): a post beside the car's nose or tail, or
    # between two measuring lines, must still close the road. Each of its
    # points follows its own ground across: on a steep road (the canyon's
    # downhill drifts, 15%) the strip's ends stand 0.35 m above and below
    # its middle, and taken against the middle's height they read as
    # steps, which left those roads no room at all beside the line.
    strip = (-half, -half / 2, 0.0, half / 2, half)
    ref = {}
    for a in strip:
        layers = city.layers(p[2] + tz * a, p[0] + tx * a)
        ref[a] = min(layers, key=lambda v: abs(v - h)) if layers else h
    free = 0.0
    d = 0.5
    while d <= REACH:
        hit, nxt = False, {}
        for a in strip:
            x = p[0] + nrm[0] * side * d + tx * a
            z = p[2] + nrm[1] * side * d + tz * a
            layers = city.layers(z, x)
            near = min(layers, key=lambda v: abs(v - ref[a])) if layers else None
            if near is None or city.blocked(z, x, ref[a]) or abs(near - ref[a]) > LEDGE:
                hit = True
                break
            nxt[a] = near
        if hit:
            break
        ref, free = nxt, d
        d += 0.5
    return free


def corridor(city, pts, nrms):
    """Per point (lo, hi): allowed offsets from the course, right +."""
    keep = HALF_CAR + MARGIN
    out = []
    for p, n in zip(pts, nrms):
        right, left = free_extent(city, p, n, 1), free_extent(city, p, n, -1)
        lo, hi = -(left - keep), right - keep
        if hi < lo:                      # narrower than a car + margins: the middle
            lo = hi = (hi + lo) / 2
        out.append((lo, hi))
    return out


def band(pts, nrms, bounds, circuit, iterations=(600, 500, 400, 300), levels=(8, 4, 2, 1)):
    """Offsets that minimise the line's curvature inside the bounds, coarse
    to fine: each point moves sideways towards the spot that zeroes the
    gradient of sum |p[i-1] - 2 p[i] + p[i+1]|^2, the stencil
    (-p[i-2] + 4 p[i-1] + 4 p[i+1] - p[i+2]) / 6. (Plain neighbour
    averaging pulls the shortest path instead, which wraps the inside of
    every corner and puts the whole turn on one point.)"""
    n = len(pts)
    off = [min(max(0.0, lo), hi) for lo, hi in bounds]
    def pos(i):
        return pts[i][0] + nrms[i][0] * off[i], pts[i][2] + nrms[i][1] * off[i]
    for s, its in zip(levels, iterations):
        idx = list(range(0, n, s))
        if not circuit and idx[-1] != n - 1:
            idx.append(n - 1)
        m = len(idx)
        if m < 5:
            continue
        # A coarse point stands for the s points around it: give it their
        # tightest corridor, or a post narrowing the road for a metre or two
        # is only seen at the finest level, too late to plan the swerve.
        lvl = {}
        for i in idx:
            win = [bounds[(i + d) % n] if circuit else bounds[min(max(i + d, 0), n - 1)] for d in range(-(s // 2), s // 2 + 1)]
            lo, hi = max(w[0] for w in win), min(w[1] for w in win)
            lvl[i] = (lo, hi) if lo <= hi else ((lo + hi) / 2,) * 2
        for _ in range(its):
            for k in range(m):
                if not circuit and (k < 2 or k > m - 3):
                    continue
                i = idx[k]
                a2, a1, b1, b2 = (pos(idx[(k + d) % m]) for d in (-2, -1, 1, 2))
                px, pz = pos(i)
                tx = (-a2[0] + 4 * a1[0] + 4 * b1[0] - b2[0]) / 6
                tz = (-a2[1] + 4 * a1[1] + 4 * b1[1] - b2[1]) / 6
                delta = (tx - px) * nrms[i][0] + (tz - pz) * nrms[i][1]
                lo, hi = lvl[i]
                off[i] = min(max(off[i] + delta, lo), hi)
        # fill the points between with straight interpolation, then clamp
        for k in range(m if circuit else m - 1):
            a, b = idx[k], idx[(k + 1) % m]
            span = (b - a) % n if circuit else b - a
            for j in range(1, span):
                i = (a + j) % n
                off[i] = min(max(off[a] + (off[b] - off[a]) * j / span, bounds[i][0]), bounds[i][1])
    return off


def optimise_tph(city, pts, circuit, kappa_max=1 / 8.0, step=6.0, iters=10, trust=2.5, pin_to=None, pin_from=None):
    """Minimum-curvature line from TUM's trajectory_planning_helpers
    (LGPL-3.0; Heilmeier et al., "Minimum Curvature Trajectory Planning and
    Control for an Autonomous Racecar", 2019): a QP over offsets along the
    reference's normals, inside the measured corridor, curvature held under
    kappa_max (the drive model's tightest turn, with a margin). The QP is
    linearised about its reference, so - as their iterative handler does,
    which only takes closed tracks - it is solved repeatedly with each step
    limited to +-trust metres, moving the reference onto the solution, here
    for sprints too. Solved every `step` metres (dense QP), returned as GU
    points. Works in the game's X/Y plane (standard orientation: normals
    point right of travel, as ours). pin_to and pin_from (m along) hold the
    ends in place. None without the helpers or a solution."""
    try:
        import numpy as np
        import trajectory_planning_helpers as tph
    except ImportError:
        return None
    coarse = resample(pts, circuit, step)
    P = np.array([[p[2], p[0]] for p in coarse])
    ys = [p[1] for p in coarse]

    def splines(Q):
        if circuit:
            _, _, A, nv = tph.calc_splines.calc_splines(path=np.vstack([Q, Q[:1]]))
            return A, nv, None, None
        def head(a, b):   # tph heading: 0 = north (+Y), counter-clockwise
            return math.atan2(-(b[0] - a[0]), b[1] - a[1])
        ps, pe = head(Q[0], Q[1]), head(Q[-2], Q[-1])
        _, _, A, nv = tph.calc_splines.calc_splines(path=Q, psi_s=ps, psi_e=pe)
        if len(nv) == len(Q) - 1:                   # open path: one normal per spline; the end takes the last
            nv = np.vstack([nv, nv[-1:]])
        return A, nv, ps, pe

    A, nv, ps, pe = splines(P)
    keep = HALF_CAR + MARGIN
    # the room at each point and halfway to the next: each point takes the
    # tightest of its own and the two halfway (a post between two points
    # 6 m apart was missed, and the line ran past it within the margin: the
    # 4083's at 3.2 km, where the rivals scraped it in every run)
    def room(g, nn):
        return free_extent(city, g, nn, 1), free_extent(city, g, nn, -1)
    m = len(P)
    at_pt, half = [], []
    for k in range(m):
        q, n, y = P[k], nv[k], ys[k]
        at_pt.append(room((float(q[1]), y, float(q[0])), (float(n[1]), float(n[0]))))   # GU (x, z) = game (Y, X)
        if k + 1 < m or circuit:
            q2, y2 = P[(k + 1) % m], ys[(k + 1) % m]
            mid = ((float(q[1]) + float(q2[1])) / 2, (y + y2) / 2, (float(q[0]) + float(q2[0])) / 2)
            half.append(room(mid, (float(n[1]), float(n[0]))))
    wr, wl = [], []
    for k in range(m):
        near = [at_pt[k]] + [half[j] for j in ((k - 1) % m if circuit else k - 1, k) if 0 <= j < len(half)]
        right, left = min(r for r, _ in near), min(l for _, l in near)
        lo, hi = -(left - keep), right - keep
        if hi < lo:                                # narrower than a car and margins: its middle
            lo = hi = (hi + lo) / 2
        wr.append(hi + .01)
        wl.append(-lo + .01)
    wr, wl = np.array(wr), np.array(wl)
    moved = False
    if pin_to is not None or pin_from is not None:
        # held where they are: the coarse points up to pin_to metres along,
        # and from pin_from on (a fork's line, tools/line_forks.py, keeps its
        # ends on the racing line it leaves and rejoins)
        along = np.concatenate(([0.0], np.cumsum(np.hypot(*np.diff(P, axis=0).T))))
        held = np.zeros(len(P), bool)
        if pin_to is not None:
            held |= along <= pin_to + .1
        if pin_from is not None:
            held |= along >= pin_from - .1
        wr[held], wl[held] = .01, .01
    for it in range(iters):
        track = np.column_stack((P, np.clip(wr, -trust, trust), np.clip(wl, -trust, trust)))
        alpha = None
        for kb in (kappa_max, 1.0):
            try:
                alpha, _ = tph.opt_min_curv.opt_min_curv(reftrack=track, normvectors=nv, A=A, kappa_bound=kb, w_veh=0.0,
                                                         closed=bool(circuit), psi_s=ps, psi_e=pe)
                break
            except ValueError:
                continue                           # inconsistent: relax the curvature, then give up
        if alpha is None:
            break
        P = P + nv * alpha[:, None]
        wr, wl = wr - alpha, wl + alpha            # the boundaries stay where they are
        moved = True
        A, nv, ps, pe = splines(P)
        if np.max(np.abs(alpha)) < .05:
            break
    if not moved:
        return None
    return resample([(float(q[1]), y, float(q[0])) for q, y in zip(P, ys)], circuit)


class ArcProbe:
    """Metadata probes on the ORIGINAL polyline; never replaces its vertices."""
    def __init__(self, line, circuit):
        import bisect
        self.bisect = bisect.bisect_right
        line = list(line)
        self.line, self.circuit = line, circuit
        self.s = [0.0]
        for a, b in zip(line, line[1:] + ([line[0]] if circuit else [])):
            d = math.hypot(b[0]-a[0], b[2]-a[2])
            if d <= 1e-6: raise ValueError('degenerate line arc')
            self.s.append(self.s[-1] + d)
        self.length = self.s[-1]
    def distance(self, i):
        n = len(self.line)
        return self.s[i % n] + (i // n)*self.length if self.circuit else self.s[min(max(i,0),n-1)]
    def point(self, s):
        if self.circuit: s %= self.length
        else: s = min(max(s,0.0),self.length)
        i = min(self.bisect(self.s,s)-1, len(self.line)-1 if self.circuit else len(self.line)-2)
        t = (s-self.s[i])/(self.s[i+1]-self.s[i]);a=self.line[i];b=self.line[(i+1)%len(self.line)]
        return tuple(a[k]+(b[k]-a[k])*t for k in range(3))
    def smooth_y(self, s):
        return sum(self.point(s+d*3.0)[1] for d in range(-2,3))/5.0


def curvature_arc(line, circuit):
    """Same 6m geometric stencil / 3m smoothing in metres, not node count."""
    arc=ArcProbe(line,circuit)
    def raw(s):
        if not circuit and (s<6 or s>arc.length-6): return 0.0
        a,p,b=(arc.point(s+d) for d in (-6,0,6))
        ux,uz=p[0]-a[0],p[2]-a[2];vx,vz=b[0]-p[0],b[2]-p[2]
        den=math.hypot(ux,uz)*math.hypot(vx,vz)*math.hypot(b[0]-a[0],b[2]-a[2])
        return -2*(ux*vz-uz*vx)/den if den>1e-6 else 0.0
    return [sum(raw(arc.distance(i)+d*3.0) for d in range(-2,3))/5.0 for i in range(len(line))]


def curvature(line, circuit, span=2):
    """Signed curvature (1/m, + = turning right) over +-span points, smoothed."""
    n = len(line)
    raw = []
    for i in range(n):
        if not circuit and (i < span or i >= n - span):
            raw.append(0.0)
            continue
        a, p, b = line[(i - span) % n], line[i], line[(i + span) % n]
        ux, uz, vx, vz = p[0] - a[0], p[2] - a[2], b[0] - p[0], b[2] - p[2]
        cross = ux * vz - uz * vx            # GU x/z: right turn is negative
        la, lb, lc = math.hypot(ux, uz), math.hypot(vx, vz), math.hypot(b[0] - a[0], b[2] - a[2])
        raw.append(-2 * cross / (la * lb * lc) if la * lb * lc > 1e-6 else 0.0)
    out = []
    for i in range(n):
        win = [raw[(i + k) % n] if circuit else raw[min(max(i + k, 0), n - 1)] for k in range(-2, 3)]
        out.append(sum(win) / 5)
    return out


def ground_heights(city, line):
    """Collision ground under each line point (layer nearest the course)."""
    out = []
    for p in line:
        layers = city.layers(p[2], p[0])
        out.append(min(layers, key=lambda v: abs(v - p[1])) if layers else p[1])
    return out


AIR_TOP = 100       # m/s: the fastest class's top speed (line_speeds), the highest crest speed weighed
AIR_MARGIN = .5     # m of the room kept in the air: the driver holds the line within ~0.7 m rms
AIR_SNAP = .1       # m over the road a car still rides (the grid's kinks)
AIR_HEIGHT = 1.5    # m over the road at most: higher, the drive model's wall test in the air (vehicle.c
                    # solid_for) reads the ground beside as a step and the car hits it


def crest_speeds(line, side, circuit, span=2):
    """Speed (m/s) a car keeps under over each point's crest; 0 where none
    is needed. Past sqrt(g / vertical curvature) the car leaves the ground,
    and in the air it cannot steer: it flies on along its heading until the
    road comes back under it. That matters only where the line bends away
    from that heading, or the room runs out, before the landing; over a
    crest on a straight the original's racers fly on at their line's speed
    (the 4001 runs over the top of a ramp at 2.4 km, lift-off at ~28 m/s,
    where the PS2's rivals pass at 40-45). And here a flight must stay low:
    higher than AIR_HEIGHT, the drive model's wall test in the air reads the
    ground beside as a step and the car hits it (the 4042's at 2 km threw
    the rivals 2-3 m up). So each crest (a run of convex points of the
    profile smoothed over +-span points) gets the highest speed, up to
    AIR_TOP, at which the car keeps to the room and low (flight, below); 10%
    short of lift-off where it does not even at that, as before."""
    n = len(line)
    ys = [p[1] for p in line]
    def at(i):
        return ys[i % n] if circuit else ys[min(max(i, 0), n - 1)]
    sm = [sum(at(i + d) for d in range(-span, span + 1)) / (2 * span + 1) for i in range(n)]
    def sat(i):
        return sm[i % n] if circuit else sm[min(max(i, 0), n - 1)]
    def pt(i):
        return line[i % n] if circuit else line[min(max(i, 0), n - 1)]
    h = STEP * span
    lift = []
    for i in range(n):
        d2 = (sat(i + span) - 2 * sat(i) + sat(i - span)) / (h * h)
        lift.append(math.sqrt(9.81 / -d2) if d2 < -1e-4 else None)
    def flight(crest, v):
        """Whether the car over this crest at v keeps to the room: on the
        road as it is (not the smoothed profile: it rounds a kerb-sharp
        edge and spreads a short ramp over 12 m), from before the crest,
        the car follows the ground while its fall at v keeps it there
        (within AIR_SNAP: the 1 m grid's kinks), leaves it from the last
        point it touched along that point's heading and climb, and must
        come back down within AIR_HEIGHT of the road and AIR_MARGIN inside
        the room."""
        lp = crest[0] - 2                       # the last point on the ground
        def launch(j):
            a, b = pt(j - 1), pt(j + 1)
            tx, tz = b[0] - a[0], b[2] - a[2]
            run = math.hypot(tx, tz) or 1.0
            back = math.dist((pt(j - 1)[0], pt(j - 1)[2]), (pt(j)[0], pt(j)[2])) or 1.0
            return tx / run, tz / run, (at(j) - at(j - 1)) / back
        tx, tz, climb = launch(lp)
        flying = False
        for j in range(lp + 1, lp + n if circuit else n):
            q, o = pt(j), pt(lp)
            dx, dz = q[0] - o[0], q[2] - o[2]
            s = dx * tx + dz * tz
            if s <= 0 or s > 400:
                return True
            above = at(lp) + climb * s - 9.81 * s * s / (2 * v * v) - at(j)
            if above <= AIR_SNAP:
                if flying:
                    return True                 # down again: steering
                if j > crest[-1] + 2:
                    return True                 # it never left the road
                lp = j
                tx, tz, climb = launch(j)
                continue
            flying = True
            if above > AIR_HEIGHT:
                return False
            off = dx * tz - dz * tx             # the car from the line, right + (the line right of it: left)
            left, right = side[j % n]
            if not -left + AIR_MARGIN <= off <= right - AIR_MARGIN:
                return False
        return True
    # the crests: runs of convex points (on a circuit, from a point that is not)
    first = next((i for i in range(n) if lift[i] is None), None)
    if first is None:
        return [0.0] * n
    order = [(first + k) % n for k in range(n)] if circuit else list(range(n))
    runs, cur = [], []
    for i in order:
        if lift[i] is None:
            if cur:
                runs.append(cur)
            cur = []
        else:
            cur.append(i)
    if cur:
        runs.append(cur)
    out = [0.0] * n
    for crest in runs:
        low = min(lift[i] for i in crest)
        best, v = 0.0, math.floor(low) + 1.0
        while v <= AIR_TOP:
            if not flight(crest, v):
                break
            best = v
            v += 1.0
        if best < AIR_TOP:
            for i in crest:
                out[i] = max(best, .9 * low)
    return out


RAMP = 61 * .15       # m/s: full steering lock from here (PHYS_TURN_RAMP)


def tightest_radius():
    """Smallest radius the drive model turns at any speed (neutral car):
    below RAMP the turn rate scales with speed, so R = RAMP / TURN divided
    by the high-speed falloff (1 - 0.55 v / 61) at that speed."""
    return RAMP / TURN / (1 - .55 * RAMP / 61)


def build(course, city):
    pts = reference(city, resample(despike(course['pts'], course['circuit'], city=city), course['circuit']), course['circuit'])
    pts = despike(pts, course['circuit'], city=city)
    try:
        tph_line = optimise_tph(city, pts, course['circuit'])
    except Exception as e:                    # a degenerate course: the elastic band below
        print('event', course['id'], 'QP failed:', e, flush=True)
        tph_line = None
    if tph_line:
        # the free room around the final line, measured afresh
        line = tph_line
        nrms = normals(line, course['circuit'])
        bounds = corridor(city, line, nrms)
        off = [min(max(0.0, lo), hi) for lo, hi in bounds]
    else:                                     # without the helpers: the elastic band
        nrms = normals(pts, course['circuit'])
        bounds = corridor(city, pts, nrms)
        off = band(pts, nrms, bounds, course['circuit'])
        line = [(p[0] + n[0] * o, p[1], p[2] + n[1] * o) for p, n, o in zip(pts, nrms, off)]
        bounds = [(lo - o, hi - o) for (lo, hi), o in zip(bounds, off)]   # around the line
        off = [0.0] * len(line)
    ys = ground_heights(city, line)
    line = [(p[0], y, p[2]) for p, y in zip(line, ys)]
    kappa = curvature(line, course['circuit'])
    side = [(o - lo, hi - o) for o, (lo, hi) in zip(off, bounds)]   # room left, right of the line
    return line, kappa, side, bounds, crest_speeds(line, side, course['circuit'])


def build_in(city_path, course):
    # the city with the event's own barriers (convert_city.py's bar-<id>.n2cy)
    city = CityCollision(city_path)
    return build(course, city.with_overlay(Path(city_path).parent / f"bar-{course['id']}.n2cy"))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--jobs', type=int, default=12)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--only', type=int, nargs='*', help='event ids (default: all)')
    args = ap.parse_args()
    courses = read_courses(args.out / 'races.n2rc')
    anchors = read_anchors(args.out / 'marks.n2mk')
    for c in courses:
        if c['circuit']:
            c['pts'] = from_anchor(c['pts'], anchors.get(c['id'], 0.0))
    if args.only:
        courses = [c for c in courses if c['id'] in args.only]
    table, blob, report = [], bytearray(), {}
    rmin = tightest_radius()
    # one course per process: the QPs are dense and single-threaded; each
    # course waits for a core in the Mac's queue (tools/mac_slots.py)
    import concurrent.futures
    import functools
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as ex:
        built = list(ex.map(functools.partial(mac_slots.call, 'racing_line', build_in, str(args.out / 'city.n2cy')), courses))
    for c, (line, kappa, side, bounds, crest) in zip(courses, built):
        table.append((c['id'], c['circuit'], len(line), len(blob)))
        for p, k, (l, r), v in zip(line, kappa, side, crest):
            blob += struct.pack('<4f2bH', *p, k, max(0, min(127, round(l * 10))), max(0, min(127, round(r * 10))),
                                min(65535, round(v * 10)))
        tight = sum(1 for k in kappa if abs(k) > 1 / rmin)
        squeezed = sum(1 for lo, hi in bounds if hi - lo < .01)
        crests = [v for v in crest if 0 < v < 45]
        report[c['id']] = dict(points=len(line), min_radius=round(1 / max(1e-4, max(abs(k) for k in kappa)), 1),
                               too_tight=tight, squeezed=squeezed, slowest_crest=round(min(crests), 1) if crests else None)
        print(c['id'], report[c['id']], flush=True)
    if args.only and (args.out / 'lines.n2rl').exists():
        # keep the other events of the existing file
        old = (args.out / 'lines.n2rl').read_bytes()
        n_old = struct.unpack_from('<I', old, 8)[0]
        done = {t[0] for t in table}
        for k in range(n_old):
            eid, circ, n, off = struct.unpack_from('<2H2I', old, 12 + 12 * k)
            if eid not in done:
                table.append((eid, circ, n, len(blob)))
                blob += old[off:off + 20 * n]
        rep = args.out / 'lines.json'
        if rep.exists():
            report = {**json.loads(rep.read_text()), **{str(k): v for k, v in report.items()}}
    head = struct.pack('<4s2I', b'N2RL', 1, len(table))
    base = len(head) + 12 * len(table)
    head += b''.join(struct.pack('<2H2I', eid, circ, n, base + off) for eid, circ, n, off in table)
    (args.out / 'lines.n2rl').write_bytes(head + blob)
    (args.out / 'lines.json').write_text(json.dumps(report, indent=1) + '\n')


if __name__ == '__main__':
    main()


def crest_speeds_arc(line, side, circuit, span=2):
    """Speed (m/s) a car keeps under over each point's crest; 0 where none
    is needed. Past sqrt(g / vertical curvature) the car leaves the ground,
    and in the air it cannot steer: it flies on along its heading until the
    road comes back under it. That matters only where the line bends away
    from that heading, or the room runs out, before the landing; over a
    crest on a straight the original's racers fly on at their line's speed
    (the 4001 runs over the top of a ramp at 2.4 km, lift-off at ~28 m/s,
    where the PS2's rivals pass at 40-45). And here a flight must stay low:
    higher than AIR_HEIGHT, the drive model's wall test in the air reads the
    ground beside as a step and the car hits it (the 4042's at 2 km threw
    the rivals 2-3 m up). So each crest (a run of convex points of the
    profile smoothed over +-span points) gets the highest speed, up to
    AIR_TOP, at which the car keeps to the room and low (flight, below); 10%
    short of lift-off where it does not even at that, as before."""
    n = len(line)
    arc = ArcProbe(line, circuit)
    ys = [p[1] for p in line]
    def at(i):
        return ys[i % n] if circuit else ys[min(max(i, 0), n - 1)]
    def sat(i):
        return arc.smooth_y(arc.distance(i))
    def pt(i):
        return line[i % n] if circuit else line[min(max(i, 0), n - 1)]
    h = STEP * span
    lift = []
    for i in range(n):
        d2 = (arc.smooth_y(arc.distance(i)+h) - 2 * sat(i) + arc.smooth_y(arc.distance(i)-h)) / (h * h)
        lift.append(math.sqrt(9.81 / -d2) if d2 < -1e-4 else None)
    def flight(crest, v):
        """Whether the car over this crest at v keeps to the room: on the
        road as it is (not the smoothed profile: it rounds a kerb-sharp
        edge and spreads a short ramp over 12 m), from before the crest,
        the car follows the ground while its fall at v keeps it there
        (within AIR_SNAP: the 1 m grid's kinks), leaves it from the last
        point it touched along that point's heading and climb, and must
        come back down within AIR_HEIGHT of the road and AIR_MARGIN inside
        the room."""
        lp = crest[0]
        while (circuit or lp > 0) and arc.distance(crest[0])-arc.distance(lp) < 6.0:
            lp -= 1
        # Start at an existing vertex at least6m before crest. No new chord.
        # Unwrap runs across index0 so physical end checks remain ordered.
        end = crest[0] + len(crest) - 1                       # the last point on the ground
        def launch(j):
            a, b = arc.point(arc.distance(j)-3.0), arc.point(arc.distance(j)+3.0)
            tx, tz = b[0] - a[0], b[2] - a[2]
            run = math.hypot(tx, tz) or 1.0
            back = min(3.0, arc.distance(j)) if not circuit else 3.0
            back = back or 1.0
            return tx / run, tz / run, (at(j) - arc.point(arc.distance(j)-3.0)[1]) / back
        tx, tz, climb = launch(lp)
        flying = False
        for j in range(lp + 1, lp + n if circuit else n):
            q, o = pt(j), pt(lp)
            dx, dz = q[0] - o[0], q[2] - o[2]
            s = dx * tx + dz * tz
            if s <= 0 or s > 400:
                return True
            above = at(lp) + climb * s - 9.81 * s * s / (2 * v * v) - at(j)
            if above <= AIR_SNAP:
                if flying:
                    return True                 # down again: steering
                if arc.distance(j) > arc.distance(end) + 6.0:
                    return True                 # it never left the road
                lp = j
                tx, tz, climb = launch(j)
                continue
            flying = True
            if above > AIR_HEIGHT:
                return False
            off = dx * tz - dz * tx             # the car from the line, right + (the line right of it: left)
            left, right = side[j % n]
            if not -left + AIR_MARGIN <= off <= right - AIR_MARGIN:
                return False
        return True
    # the crests: runs of convex points (on a circuit, from a point that is not)
    first = next((i for i in range(n) if lift[i] is None), None)
    if first is None:
        return [0.0] * n
    order = [(first + k) % n for k in range(n)] if circuit else list(range(n))
    runs, cur = [], []
    for i in order:
        if lift[i] is None:
            if cur:
                runs.append(cur)
            cur = []
        else:
            cur.append(i)
    if cur:
        runs.append(cur)
    out = [0.0] * n
    for crest in runs:
        low = min(lift[i] for i in crest)
        best, v = 0.0, math.floor(low) + 1.0
        while v <= AIR_TOP:
            if not flight(crest, v):
                break
            best = v
            v += 1.0
        if best < AIR_TOP:
            for i in crest:
                out[i] = max(best, .9 * low)
    return out

