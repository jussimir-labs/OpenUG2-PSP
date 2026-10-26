#!/usr/bin/env python3
"""The replay's cameras (the original's ICE cameras: DragCameraManager,
IceCameraMover, Player::UpdateIceCameras) -> tvcams.n2tv.

The game's camera sets (DragCameraManager::LoadCameraSet), groups of
tracks of 0x88-byte keys (convert_ending_nis.py's layout):
- 0x8003B200 in TRACKS\\L4RA.BUN, L4RD.BUN, L4RG.BUN: the routes' cameras,
  a group per route and way (its key: the route's number, the bit 16 on
  when the route runs against the race), one track each; a key's
  parameter is the car's place along the route's stretch of the race's
  course (TrackRoute::NormalizedDistanceAlongRoute);
- 0x8003B202 in GLOBAL\\INGAMECOMMON.BUN: the generic cameras on the car,
  a group per category (NOS, Jump, Speed, Corner, Burnout, PowerSlide), a
  track 2 or 3 s long (its duration at +0x0c), the parameter the time
  since the cut over it.
A key: type (0 none, 2 and up a camera), smooth entry, eye and look
cubic, their spaces (0 the car: x ahead, y left, z up; 1 the world; 2
the car's place, the world's axes; 3 a scene's), constrain to the cars
and to the world, ignore orientation, the car's lag; its parameter, the
start and end tangents' lengths, eye and look from and to, the lens (mm)
from and to.

The route of a stretch of the course: the routes of
TRACKS\\ROUTES<world>\\ROUTESFREEROAM.BIN (chunk 0x34121: u16 number at +2,
u16 points at +44, the points' x, y from +132 every 56 bytes) whose points
pass along the race's course (races*.n2rc, both ways round): two or
more of them within 20 m, its way the one their distances along the
course mostly go. The game takes the route the car is on
(Car::GetRoute in DragCameraManager::ChooseGoodCamera): a long route over
a short stretch of the course counts, and the port picks at run time the
one under the car (tvcam.c), so a crossing street's is left out there.
The game's parameter, the car's distance along the course over the
route's stretch of it, is taken here along the route's own line (the
port turns a reversed circuit's start at run time): from its end the race
meets first.

tvcams.n2tv (little endian): 'N2TV', u32 1, then
  u32 n, n x key (128 bytes): f32 parameter; u8 type, eye cubic, look
    cubic, eye space, look space, constrain to cars, to the world, ignore
    orientation, lag, 3 pad; f32 eye from [3] and to [3], look from and
    to (world keys GU, the others the game's axes); f32 slopes [12] (eye
    then look, start then end, the same axes); f32 horizontal field of
    view from and to (rad); f32 the parameter where the run of keys
    shared with this one ends going forward, and where it starts going
    back (the next hard cut, DragCameraManager::TimeUntilNextHardCut);
  u32 n, n x track (u16 first key, u8 keys, u8 category: 0-5 generic, 255
    a route's; f32 its duration, s);
  u32 6, the categories (NOS, Jump, Speed, Corner, Burnout, PowerSlide):
    u16 first track, u16 tracks;
  u32 n, n x route group (u16 route, u8 way bit, u8 world (0 L4RA, 1
    L4RD, 2 L4RG), u16 first track, u16 tracks);
  u32 n, n x route line (u32 first point, u16 points, u16 pad);
  u32 n, n x point (f32 GU x, z);
  u32 n, n x stretch (u16 event, u8 reversed, u8 pad, u16 group, u16 the
    route's line).
"""
import argparse
import math
import struct
from pathlib import Path

import numpy as np

from n2formats import archived, chunks
import convert_nis as nis
import n2source

ROOT = Path(__file__).resolve().parents[1]
WORLDS = (('L4RA', 'races.n2rc'), ('L4RD', 'races-L4RD.n2rc'), ('L4RG', 'races-L4RG.n2rc'))
CATEGORIES = ('NOS', 'Jump', 'Speed', 'Corner', 'Burnout', 'PowerSlide')
NEAR = 20.0     # m from the course


def groups(d, tag):
    out = []
    for t, o, s in chunks(d):
        if t != tag:
            continue
        p = o
        while p < o + s:
            _, size = struct.unpack_from('<II', d, p)
            key, n = struct.unpack_from('<II', d, p + 8)
            q, tracks = p + 16, []
            for _ in range(n):
                dur, = struct.unpack_from('<f', d, q + 0x0c)
                nk = struct.unpack_from('<b', d, q + 0x10)[0]
                tracks.append((dur, [d[q + 0x20 + 0x88 * j:q + 0x20 + 0x88 * (j + 1)] for j in range(nk)]))
                q += 0x20 + 0x88 * nk
            out.append((key, tracks))
            p += 8 + size
    return out


def lens_fov(lens):
    return 2 * math.atan2(15.96, lens)


def decode(r):
    f = struct.unpack_from('<34f', r)
    return dict(type=r[0], eye_cubic=r[2], look_cubic=r[3], space=(r[4], r[5]), flags=tuple(r[6:10]), t=f[3],
                tan=(f[4], f[5]), eye=(np.array(f[6:9]), np.array(f[9:12])), look=(np.array(f[12:15]), np.array(f[15:18])),
                lens=(f[18], f[19]))


def shared(a, ea, b, eb):
    """DragCameraManager::KeysShared: a's side ea meets b's side eb."""
    return (a['type'] == b['type'] and a['space'] == b['space'] and abs(a['tan'][ea] - b['tan'][eb]) < 1e-5
            and abs(a['lens'][ea] - b['lens'][eb]) < 1e-5
            and all(np.abs(a[w][ea] - b[w][eb]).max() <= 1e-5 for w in ('eye', 'look')))


def to_gu(v, space):
    """A world point (space 1) to GU (game y, z, x); the others stay."""
    return np.array([v[1], v[2], v[0]]) if space == 1 else v


def track_keys(raw):
    keys = [decode(r) for r in raw]
    n = len(keys)
    span = lambda j: (keys[j + 1]['t'] if j + 1 < n else 1.) - keys[j]['t']
    out = []
    for j, k in enumerate(keys):
        slopes = []
        for end in (0, 1):   # DragCameraManager::GetSlope
            tl = k['tan'][end]
            dk = {w: k[w][1] - k[w][0] for w in ('eye', 'look')}
            nb = j - 1 if end == 0 else j + 1
            if not k['type']:
                slopes.append({w: np.zeros(3) for w in dk})
            elif 0 <= nb < n and keys[nb]['type'] and shared(k, end, keys[nb], end ^ 1):
                ik, inb = span(j), span(nb)
                f1 = ik / (ik + inb)
                f3 = 1 - f1
                if inb > 1e-6:
                    f1 *= ik / inb
                slopes.append({w: (keys[nb][w][1] - keys[nb][w][0]) * f1 * tl + dk[w] * f3 * tl for w in dk})
            else:
                slopes.append({w: dk[w] * tl for w in dk})
        fwd = j   # the run shared with this key, forward and back (compared with this one, as the game does)
        while fwd + 1 < n and keys[fwd + 1]['type'] and shared(k, 1, keys[fwd + 1], 0):
            fwd += 1
        back = j
        while back > 0 and keys[back - 1]['type'] and shared(k, 0, keys[back - 1], 1):
            back -= 1
        es, ls = k['space']
        eye = [to_gu(v, es) for v in k['eye']]
        look = [to_gu(v, ls) for v in k['look']]
        sl = [to_gu(slopes[e][w], es if w == 'eye' else ls) for e in (0, 1) for w in ('eye', 'look')]
        out.append(struct.pack('<f9B3x', k['t'], k['type'], k['eye_cubic'], k['look_cubic'], es, ls, *k['flags'])
                   + np.concatenate([*eye, *look]).astype('<f4').tobytes()
                   + np.concatenate(sl).astype('<f4').tobytes()
                   + struct.pack('<4f', lens_fov(k['lens'][0]), lens_fov(k['lens'][1]),
                                 keys[fwd + 1]['t'] if fwd + 1 < n else 1., keys[back]['t']))
    return out


def routes(game, world):
    try:
        d = archived(game, f'TRACKS/ROUTES{world}/ROUTESFREEROAM.BIN')
    except ValueError:
        return {}
    leaf = next(d[p:p + n] for t, p, n in chunks(d) if t == 0x34121)
    at, out = 8, {}
    while at + 140 <= len(leaf):
        num, = struct.unpack_from('<H', leaf, at + 2)
        n, = struct.unpack_from('<H', leaf, at + 44)
        out[num] = np.array([struct.unpack_from('<2f', leaf, at + 132 + 56 * k) for k in range(n)])
        at += 140 + 56 * n
    return out


def read_races(path):
    b = path.read_bytes()
    n, = struct.unpack_from('<I', b, 8)
    at, out = 12, {}
    for _ in range(n):
        eid, circ, hint, npts, ng, length = struct.unpack_from('<HBBHHf', b, at)
        at += 12
        pts = np.array(struct.unpack_from(f'<{npts * 3}f', b, at)).reshape(-1, 3)
        at += npts * 12 + ng * 2
        at += -at % 4
        out[eid] = (circ, pts)
    return out


def on_course(circ, pts, route):
    """The route's way along the course (0 with it, 1 against), None when
    it is not on it."""
    P = pts[:, [2, 0]]   # GU (z, x) = the game's (x, y)
    if circ:
        P = np.vstack([P, P[:1]])
    seg = np.diff(P, axis=0)
    L = np.hypot(seg[:, 0], seg[:, 1])
    D = np.concatenate([[0], np.cumsum(L)])
    total = D[-1]
    near = []
    for q in route:
        a = P[:-1]
        t = np.clip(((q - a) * seg).sum(1) / np.maximum(L * L, 1e-9), 0, 1)
        c = a + seg * t[:, None]
        dd = np.hypot(*(c - q).T)
        i = int(np.argmin(dd))
        if dd[i] < NEAR:
            near.append(D[i] + t[i] * L[i])
    if len(near) < 2:
        return None
    step = [((b - a + total / 2) % total - total / 2) if circ else b - a for a, b in zip(near, near[1:])]
    return 1 if sum(1 for v in step if v > 0) < sum(1 for v in step if v < 0) else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    keys, tracks, cats, grps, spans, lines, points, line_of = [], [], [], [], [], [], [], {}
    def add_track(dur, raw, cat):
        tracks.append(struct.pack('<HBBf', len(keys), len(raw), cat, dur))
        keys.extend(track_keys(raw))
    common = groups(archived(args.game, 'GLOBAL/INGAMECOMMON.BUN'), 0x8003B202)
    by_hash = dict(common)
    for c, name in enumerate(CATEGORIES):
        tr = by_hash.get(nis.h33(name), [])
        cats.append(struct.pack('<2H', len(tracks), len(tr)))
        for dur, raw in tr:
            add_track(dur, raw, c)
    for w, (world, races) in enumerate(WORLDS):
        try:
            d = archived(args.game, f'TRACKS/{world}.BUN')
        except ValueError:
            continue
        group_of = {}
        for key, tr in groups(d, 0x8003B200):
            group_of[key] = len(grps)
            grps.append(struct.pack('<HBBHH', key & 0xffff, key >> 16 & 1, w, len(tracks), len(tr)))
            for dur, raw in tr:
                add_track(dur, raw, 255)
        R = routes(args.game, world)
        path = args.out / races
        if not path.exists():
            continue
        for eid, (circ, pts) in sorted(read_races(path).items()):
            for rev in (0, 1):
                p = pts[::-1] if rev else pts
                for num, route in R.items():
                    if not any((num | b << 16) in group_of for b in (0, 1)):
                        continue
                    way = on_course(circ, p, route)
                    if way is None or (num | way << 16) not in group_of:
                        continue
                    if (w, num) not in line_of:
                        line_of[w, num] = len(lines)
                        lines.append(struct.pack('<IHxx', len(points), len(route)))
                        points.extend(struct.pack('<2f', y, x) for x, y in route)   # GU x, z = the game's y, x
                    spans.append(struct.pack('<HBxHH', eid, rev, group_of[num | way << 16], line_of[w, num]))
    blob = struct.pack('<4sI', b'N2TV', 1)
    for table in (keys, tracks, cats, grps, lines, points, spans):
        blob += struct.pack('<I', len(table)) + b''.join(table)
    (args.out / 'tvcams.n2tv').write_bytes(blob)
    print(dict(keys=len(keys), tracks=len(tracks), groups=len(grps), lines=len(lines), stretches=len(spans), bytes=len(blob)))


if __name__ == '__main__':
    main()
