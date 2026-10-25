#!/usr/bin/env python3
"""The ways a race's rivals may take off its course, as the original's
racers choose them -> lineforks.n2lf, beside lines.n2rl.

At an optional signpost the original's racer picks its way
(AIGoal::DecideSignpostAsRacer, the choice kept for that sign for the
race): never during the start's grace period; a "Slower" sign never; a
"Faster" one with its skill's odds (the difficulty table's skill block
+4: 0 at the lowest skill to 90% at the highest); an "Equal" one half the
time (a third with the sign's bit 27). At the race's load the game turns
each Equal or Faster or Slower sign into Faster or Slower by where 10 s
at the line's speed takes a car down each way (CurvaturePoint::
FindDatAfterTime): 20 m ahead of the other way or more makes it Faster,
20 m behind Slower; otherwise it stays as it is.

Here, over the event's own routes (line_speeds.EventRoutes, ROUTES<id>F
or B), an optional sign whose old point lies on the port's racing line
(line_speeds' nearest route point) is a fork when its way out runs along
the routes' lines, clear of the event's barriers, back to a later point
of the racing line within 2 km, more than 10 m off it on the way. Its
line: 30 m of the racing line before the fork, the route points' racing
line every 3 m, then 60 m of the racing line past the rejoin; its room
measured on the city's collision as tools/racing_line.py does (the line
moved into the room where it runs at its edge; a fork narrower than a
car for over 9 m is closed in the port's world and left out), then
pulled tight by the racing line's minimum-curvature QP inside that room,
its ends held on the racing line; and its speed the routes'
(line_speeds). The gain is weighed as the time either
way to the rejoin at the line's speed, times the speed there, per car
class.

lineforks.n2lf: 'N2LF', u32 version 1, u32 count, count x (u16 event
id, u8 way (0 as listed, 1 the other way), u8 forks, u32 offset); at each
offset, per fork (a 20-byte head): u16 fork point and u16 rejoin point
(indices of the event's line the way it is run: lines.n2rl's, or
reversed as psp-viewer/race.c reverses it), u8 sign type by car class x 4
(0 Faster, 1 Slower, 4 Equal), u8 a third (bit 27), u8 0, u16 points,
u16 enter and u16 leave (the fork's line points where it leaves the
racing line and where it is back on it), u16 0 x 2; then points x 20 bytes as
lines.n2rl's (the way it is driven), then points x 4 bytes: m/s for car
classes 0-3.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import line_speeds as ls
import n2routes
import racing_line as rl
from convert_races import segments_cross
from n2city import CityCollision
from n2formats import archived, chunks

ROOT = Path(__file__).resolve().parents[1]
REACH = 2000.0      # m a way out is followed looking for the course
OFF = 10.0          # m off the line it must go (else it is the same road)
BEFORE, AFTER = 10, 20   # racing line points (3 m) kept before the fork and past the rejoin
GAIN = 20.0         # m ahead (or behind) that makes a sign Faster (or Slower)
BLOCKED = 3         # fork points in a row narrower than a car (and margins) that close it
PIN = 3             # fork line points held on the racing line where the driver turns onto it
BACK = 110          # degrees between 3 m steps: a way that turns back
JOIN = 4            # racing line points past the rejoin the fork's line is pulled tight over


def sign_bits(game, eid, suffix):
    """{(old route, old point, line): [(new node, type, third)]} of the
    optional signs (the bits EventRoutes leaves out)."""
    d = archived(game, f'TRACKS/ROUTESL4RA/ROUTES{eid}{suffix}.BIN')
    signs = next(d[p:p + n] for t, p, n in chunks(d) if t == 0x34122)
    out = {}
    for at in range(4, len(signs) - 31, 32):
        newp, oldp, newr, oldr, flags = struct.unpack_from('<4HI', signs, at + 4)
        kind = flags & 15
        if kind not in (0, 1, 4) or flags >> 20 & 1:   # (bit 20: racers do not take it)
            continue
        back, flip = flags >> 18 & 1, flags >> 23 & 1
        out.setdefault((oldr, oldp, back), []).append(((newr, newp, back ^ flip), kind, flags >> 27 & 1))
    return out


def resample(pts, step=3.0):
    """Points every step m along a polyline (x, y, z)."""
    out = [pts[0]]
    carry = 0.0
    for a, b in zip(pts, pts[1:]):
        l = math.dist((a[0], a[2]), (b[0], b[2]))
        s = step - carry
        while s <= l:
            t = s / l
            out.append(tuple(a[c] + (b[c] - a[c]) * t for c in range(3)))
            s += step
        carry = l - (s - step)
    if math.dist((out[-1][0], out[-1][2]), (pts[-1][0], pts[-1][2])) > .5:
        out.append(pts[-1])
    return out


def read_line_points(path):
    """{event: (circuit, [raw 20-byte points])}"""
    d = Path(path).read_bytes()
    _, _, count = struct.unpack_from('<4s2I', d, 0)
    out = {}
    for k in range(count):
        eid, circ, n, off = struct.unpack_from('<2H2I', d, 12 + 12 * k)
        out[eid] = (circ, [d[off + 20 * i:off + 20 * i + 20] for i in range(n)])
    return out


def find_forks(game, eid, suffix, line, circuit, routes, city, barriers):
    """[(fork index, rejoin index, [route nodes], type, third)] in run order."""
    n = len(line)
    reverse = suffix == 'B'
    run = list(range(n))[::-1] if reverse else list(range(n))
    if reverse and circuit:
        run = [(n - i) % n for i in range(n)]
    runline = [line[i] for i in run]
    near = routes.nearest(runline, False)
    at = {}
    for j, nd in enumerate(near):
        if nd and nd not in at:
            at[nd] = j
    signs = sign_bits(game, eid, suffix)
    found = {}
    for old, outs in signs.items():
        if old not in at or old not in routes.pos:
            continue
        ja = at[old]
        for new, kind, third in outs:
            if new not in routes.pos:
                continue
            path, nd, run_m, far, jr, seen = [old], new, 0.0, 0.0, None, set()
            while nd is not None and run_m < REACH and nd not in seen:
                seen.add(nd)
                p, q = routes.pos[path[-1]], routes.pos[nd]
                run_m += math.hypot(p[0] - q[0], p[1] - q[1])
                path.append(nd)
                if any(segments_cross(p[:2], q[:2], a, b) for a, b in barriers):
                    break
                if nd in at:
                    ahead = (at[nd] - ja) % n if circuit else at[nd] - ja
                    if 5 < ahead < n * .6:
                        jr = at[nd]
                        break
                far = max(far, min(math.hypot(q[0] - runline[j][2], q[1] - runline[j][0]) for j in range(0, n, 2)))
                nd = routes.next.get(nd)
            if jr is None or far <= OFF:
                continue
            key = (ja, jr)
            if key not in found or len(path) < len(found[key][2]):
                found[key] = (ja, jr, path, kind, third)
    return [found[k] for k in sorted(found)], runline


def build_fork(fork, runline, raw_run, circuit, routes, speeds_run, cls_tables, city):
    """The fork's line points (LinePoint bytes) and speeds by class."""
    ja, jr, path, kind, third = fork
    n = len(runline)
    pre = [(ja - BEFORE + k) % n if circuit else max(0, ja - BEFORE + k) for k in range(BEFORE + 1)]
    post = [(jr + k) % n if circuit else min(n - 1, jr + k) for k in range(AFTER + 1)]
    mid_nodes = path[1:-1]
    mid = [(routes.pos[nd][1], routes.pos[nd][4], routes.pos[nd][0]) for nd in mid_nodes]   # GU x, y, z
    poly = [runline[pre[-1]]] + mid + [runline[post[0]]]
    dense = resample(poly)[1:-1]
    if len(dense) < 3:
        return None
    ys = rl.ground_heights(city, dense)
    dense = [(p[0], y, p[2]) for p, y in zip(dense, ys)]
    nrms = rl.normals(dense, False)
    bounds = rl.corridor(city, dense, nrms)
    # narrower than a car and its margins for over BLOCKED points: a wall
    # across the way in the port's world (the event's scenery, a gate)
    run = worst = 0
    for lo, hi in bounds:
        run = run + 1 if hi - lo < .01 else 0
        worst = max(worst, run)
    if worst > BLOCKED:
        return None
    # the line moved into its free room where it runs at the edge (the
    # route's racing line hugs a kerb the collision has a step on)
    shift = [min(max(0.0, lo), hi) for lo, hi in bounds]
    dense = [(p[0] + n[0] * o, p[1], p[2] + n[1] * o) for p, n, o in zip(dense, nrms, shift)]
    bounds = [(lo - o, hi - o) for (lo, hi), o in zip(bounds, shift)]
    full = [runline[i] for i in pre] + dense + [runline[i] for i in post]
    leave = len(full) - AFTER - 1
    # the branch's speeds: the route nodes' (per class), placed by distance
    cum = [0.0]
    for a, b in zip(poly, poly[1:]):
        cum.append(cum[-1] + math.dist((a[0], a[2]), (b[0], b[2])))
    node_s = cum[1:-1]
    times, branch_v = [], []
    for cls in range(4):
        vn = routes.class_speeds[cls]
        vals = [vn[nd] for nd in mid_nodes]
        dv = []
        s = 0.0
        prev = poly[0]
        for p in dense:
            s += math.dist((prev[0], prev[2]), (p[0], p[2]))
            prev = p
            if not vals:
                dv.append(speeds_run[cls][pre[-1]])
                continue
            if s <= node_s[0]:
                dv.append(vals[0])
            elif s >= node_s[-1]:
                dv.append(vals[-1])
            else:
                k = max(i for i in range(len(node_s)) if node_s[i] <= s)
                t = (s - node_s[k]) / max(1e-6, node_s[k + 1] - node_s[k])
                dv.append(vals[k] + (vals[k + 1] - vals[k]) * t)
        vfull = [speeds_run[cls][i] for i in pre] + dv + [speeds_run[cls][i] for i in post]
        branch_v.append(vfull)
        # the time either way from the fork to the rejoin
        tb = sum(math.dist((a[0], a[2]), (b[0], b[2])) / max(1.0, (va + vb) / 2)
                 for a, b, va, vb in zip(full[BEFORE:len(full) - AFTER], full[BEFORE + 1:len(full) - AFTER],
                                         vfull[BEFORE:len(full) - AFTER], vfull[BEFORE + 1:len(full) - AFTER]))
        idx = [(ja + k) % n for k in range(((jr - ja) % n if circuit else jr - ja) + 1)]
        ts = sum(math.dist((runline[a][0], runline[a][2]), (runline[b][0], runline[b][2])) /
                 max(1.0, (speeds_run[cls][a] + speeds_run[cls][b]) / 2) for a, b in zip(idx, idx[1:]))
        times.append((ts - tb) * speeds_run[cls][jr])
    types = bytes(0 if g > GAIN else 1 if g < -GAIN else kind for g in times)
    # The way pulled tight: the routes' line runs metres to the side of the
    # port's racing line, and joined point to point the way turned on radii
    # of 4-30 m where it leaves the line and where it comes back (the
    # 4001's way out at 1 km, 17 m and 39 m: rivals taking it lost 130 m).
    # The racing line's own minimum-curvature QP (rl.optimise_tph) over it,
    # inside the room measured on the collision, from the fork line's start
    # to JOIN metres past the rejoin, with its first PIN points (where the
    # driver turns onto it) and the racing line from the rejoin on (where it
    # turns back) held. The gains above stay the routes' own (the game's).
    along = [0.0]
    for a, b in zip(full, full[1:]):
        along.append(along[-1] + math.dist((a[0], a[2]), (b[0], b[2])))
    end = min(len(full) - 1, leave + JOIN)
    tight = rl.optimise_tph(city, full[:end + 1], False, pin_to=along[PIN - 1], pin_from=along[leave])
    back = None
    if tight:
        back = min(range(len(tight)), key=lambda k: math.dist((tight[k][0], tight[k][2]), (full[leave][0], full[leave][2])))
        if not (PIN < back and math.dist((tight[back - 1][0], tight[back - 1][2]), (full[leave][0], full[leave][2])) < 4.5):
            back = None                 # it does not come back onto the rejoin in step: as it was
    if back is not None:
        new_along = [0.0]
        for a, b in zip(tight, tight[1:back + 1]):
            new_along.append(new_along[-1] + math.dist((a[0], a[2]), (b[0], b[2])))
        # the speeds carried over at the same share of the way
        def carry(vals):
            out = []
            for s in new_along:
                t = s / max(1e-6, new_along[-1]) * along[leave]
                k = max(0, min(leave - 1, next((k for k in range(leave) if along[k + 1] >= t), leave - 1)))
                f = (t - along[k]) / max(1e-6, along[k + 1] - along[k])
                out.append(vals[k] + (vals[k + 1] - vals[k]) * min(1.0, max(0.0, f)))
            return out
        branch_v = [carry(v[:leave + 1])[:-1] + v[leave:] for v in branch_v]
        full = tight[:back] + full[leave:]
        leave = back
    # a way that still doubles back (a turn sharper than BACK degrees between
    # 3 m steps, as racing_line.despike): its route leaves the sign behind
    # the fork, and a racer taking it would turn round (one at the 4005's
    # 4 km, where the QP finds no line)
    for a, b, c in zip(full, full[1:], full[2:leave + 1]):
        ux, uz, vx, vz = b[0] - a[0], b[2] - a[2], c[0] - b[0], c[2] - b[2]
        lu, lv = math.hypot(ux, uz), math.hypot(vx, vz)
        if lu > 1e-6 and lv > 1e-6 and (ux * vx + uz * vz) / (lu * lv) < math.cos(math.radians(BACK)):
            return None
    ys = rl.ground_heights(city, full[PIN:leave])
    full[PIN:leave] = [(p[0], y, p[2]) for p, y in zip(full[PIN:leave], ys)]
    # a step or a drop that stops a car on the way (n2city.DROP, as the
    # courses' test): the 4044's ways over the hill at 3.5 km and 6 km drop
    # 1.4 and 1.9 m off a kerb the original's cars jump, where the port's
    # rivals stuck
    for a, b in zip(full[PIN - 1:leave], full[PIN:leave + 1]):
        if city.crosses_step(a[2], a[0], a[1], b[2], b[0], b[1]):
            return None
    # the room around it, measured afresh; the racing line's own outside
    nrms = rl.normals(full, False)
    room = rl.corridor(city, full[PIN:leave], nrms[PIN:leave])
    side = []
    for i in range(len(full)):
        if PIN <= i < leave:
            lo, hi = room[i - PIN]
            side.append((max(0.0, -lo), max(0.0, hi)))
        else:
            raw = raw_run[pre[i]] if i < PIN else raw_run[post[i - leave]]
            side.append((raw[16] / 10, raw[17] / 10))
    kappa = rl.curvature(full, False)
    crest = rl.crest_speeds(full, side, False)
    out_pts, out_v = bytearray(), bytearray()
    for i, p in enumerate(full):
        if PIN <= i < leave:
            room_l, room_r = side[i]
            out_pts += struct.pack('<4f2bH', p[0], p[1], p[2], kappa[i], min(127, round(room_l * 10)), min(127, round(room_r * 10)),
                                   min(65535, round(crest[i] * 10)))
        else:   # the racing line's own point (its room and crest)
            raw = raw_run[pre[i]] if i < PIN else raw_run[post[i - leave]]
            x, y, z, _ = struct.unpack_from('<4f', raw, 0)
            out_pts += struct.pack('<4f', x, y, z, kappa[i]) + raw[16:20]
    for i in range(len(full)):
        out_v += bytes(max(1, min(255, int(branch_v[c][i]))) for c in range(4))
    head = struct.pack('<2H6B5H', ja, jr, *types, third, 0, len(full), BEFORE, leave, 0, 0)
    return head, out_pts, out_v, [round(g) for g in times]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--only', type=int, nargs='*')
    args = ap.parse_args()
    tables = ls.class_tables(args.game)
    lines = {e['id']: e for e in ls.read_lines(args.out / 'lines.n2rl')}
    raws = read_line_points(args.out / 'lines.n2rl')
    base_city = CityCollision(args.out / 'city.n2cy')
    table, blob, report = [], bytearray(), {}
    for eid in sorted(lines):
        if args.only and eid not in args.only:
            continue
        ev = lines[eid]
        line, circuit = ev['line'], bool(ev['circuit'])
        city = base_city.with_overlay(args.out / f'bar-{eid}.n2cy')
        barriers = n2routes.read_event_barriers(args.game, 'L4RA', eid)
        for way, suffix in ((0, 'F'), (1, 'B')):
            routes = ls.EventRoutes(args.game, eid, suffix)
            routes.class_speeds = [routes.speeds(c, tables[c]) for c in range(4)]
            forks, runline = find_forks(args.game, eid, suffix, line, circuit, routes, city, barriers)
            if not forks:
                continue
            n = len(line)
            order = list(range(n)) if way == 0 else ([(n - i) % n for i in range(n)] if circuit else list(range(n))[::-1])
            raw_run = [raws[eid][1][i] for i in order]
            speeds_run = [ls.line_speeds(runline, circuit, routes, False, c, tables[c])[0] for c in range(4)]
            data, rep = bytearray(), []
            for f in forks:
                built = build_fork(f, runline, raw_run, circuit, routes, speeds_run, tables, city)
                if not built:
                    continue
                head, pts, v, gains = built
                data += head + pts + v
                rep.append(dict(fork=f[0], rejoin=f[1], points=len(pts) // 20, sign=f[3], third=f[4], gain_m=gains))
            if rep:
                table.append((eid, way, len(rep), len(blob)))
                blob += data
                report[f'{eid}{suffix}'] = rep
                print(eid, suffix, rep, flush=True)
    head = struct.pack('<4s2I', b'N2LF', 1, len(table))
    base = len(head) + 8 * len(table)
    head += b''.join(struct.pack('<H2BI', eid, way, nf, base + off) for eid, way, nf, off in table)
    (args.out / 'lineforks.n2lf').write_bytes(head + blob)
    (args.out / 'lineforks.json').write_text(json.dumps(report, indent=1) + '\n')


if __name__ == '__main__':
    main()
