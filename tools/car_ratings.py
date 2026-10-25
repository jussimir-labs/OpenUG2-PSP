#!/usr/bin/env python3
"""The game's three bars (acceleration, top speed, handling) of a car with
its performance parts: the reference the PSP's psp-viewer/bars.c follows,
and the stock bars of cars.n2cl (tools/convert_cars.py).

Read in the GameCube Speed.elf (the class and function names below are its
symbols; the rules are restated here, nothing copied) and checked against
the PS2's RAM: the quick race car select's three StatisticsBar values for
the Peugeot 206, the Ford Focus and the Toyota Corolla, and the performance
shop's for the 206 with the Street Package of the engine, the transmission,
the suspension and the nitrous, came out equal to these to six places.

The car record (GLOBALB chunk 0x34600, 0x890 bytes) holds the car's
"compleat" physics from +0x110: the first 0x2B0 bytes are the stock
physics the game copies and then changes package by package
(CompleatCarPhysicsInfo::BuildCarPhysicsInfo); the rest are the upgrade
values the packages reach. Offsets below are from +0x110 ("p" for the built
physics, "c" for the compleat block).

A package category's level is the sum, over the parts fitted, of the part's
weights (one to three "dimensions", GLOBALB 0x34A15, the part's +0x30; -1 a
dimension the part leaves alone), averaged over the dimensions and kept to
0..1 (PhysicsUpgradeSpecification::ComputeUpgradeLevel). Fitting a part
takes off the parts it replaces (its +0x40 list: Stage 2 Turbo takes off
Stage 1). The categories: 0 weight, 1 drivetrain (3 dimensions), 2
nitrous, 3 engine, 4 turbo, 5 suspension (3), 6 brakes, 7 ECU, 8 tyres, 9
the body's aero (2; parts the player never buys: they follow the body
parts fitted, aero_parts below).

What each category changes, at level x (a linear blend from the stock value
to the compleat block's, or an amount added):
  weight: the mass times 1 + (c+0x340 - 1) x, p+0x160 towards c+0x348;
  drivetrain: the gearbox of level round(3 x dim2) (c+0x310 + 0x40 L: the
    final drives, the drive split p+0x1C0, the ratios; the gear count from
    the ratios), p+0x1C4/0x1CC towards c+0x3E4/0x3EC by dim0;
  nitrous: p+0x2A0 towards c+0x580, p+0x2AC to c+0x58C;
  engine: the torque curve plus c+0x420 x;
  turbo (always, also stock): the boost curve p+0x224 towards c+0x510, then
    added to the torque;
  suspension: the springs (p+0x170.., each axle 0x20 apart) towards
    c+0x5C0/0x5E0 by dim0, the dampers by dim1, the ride height p+0x08 down
    by c+0x600 dim0;
  brakes: p+0x274..0x27C towards c+0x5B0..0x5B8;
  ECU: the idle, redline and limiter rpm plus the level's c+0x410 + 0x40 L
    (L = round(3 x)), the torque plus c+0x4E0 x; the top speed limiter
    (p+0x290) off with the Remove Top Speed Limiter, the Performance Chip
    or the Engine Management Unit;
  tyres: the grip p+0xD0..0xE0 per axle towards c+0x540/0x560;
  aero: p+0x264 and 0x268 plus (c+0x590 + c+0x5A0) dim0 and (c+0x594 +
    c+0x5A4) dim1.

The bars (RidePhysicsInfo::GetAccelRating, GetTopSpeedRating,
GetHandlingRating), each kept to 0.03..1, normalised in fixed ranges: the
fifteen min/max pairs of GLOBALB chunk 0x34A32 (LoaderPerformanceConfigChunk,
"Extrema"; the ranges span the fleet fully upgraded, so stock bars are
short):
  acceleration: 0.82 of the drive's surplus over the drag, m/s^2 summed in
    3 mph steps up to 100 mph (stopping where the drag wins), in range 1;
    0.1 of the nitrous (p+0x2A0 x p+0x2AC, range 4); 0.08 of the
    drivetrain's round(3 x) over 3;
  top speed: 0.915 of where the drag meets the drive (range 0), within the
    limiter (mph); 0.085 of the nitrous;
  handling: nine terms weighted 0.14 0.10 0.15 0.15 0.10 0.15 0.15 0.08
    0.08 (HandlingVariableWeights): the tyres' grip over the mass (range
    7), the brakes over the mass (8), the springs over the mass (6), less
    mass (9), the drive split (1 at 0.6, falling to 0.25 at rear drive and
    0 at front drive), less ride height (10), less p+0x160 (11), the aero
    (12, 13).
The drive (MakeAccelerationTable): from 0 in 3 mph steps, the gear at
which the engine is below its shift point (the redline less 100 rpm; the
top gear the limiter plus 500), the torque at that rpm (the curve from
idle to the limiter, held flat past either end) times the gear and the
final drive over the wheel radius and the mass; the step that shifts takes
the old gear's rpm with the new gear's ratio; the last sample is zeroed,
and the samples resampled into 64 from 0 to the last speed.
"""
import struct

STEP = 1.3410818576812744        # 3 mph in m/s
TO_100MPH = 44.702728271484375
RPM = 9.549304                   # rad/s to rpm
WEIGHTS = (.14, .10, .15, .15, .10, .15, .15, .08, .08)
NOS_SHARE = (.1, .085)           # acceleration, top speed
DIMS = {1: 3, 5: 3, 9: 2}        # the categories' dimensions (else 1)
LIMITER_PARTS = (45, 46, 51)     # their fitting removes the top speed limiter
PHYS, COMPLEAT = 0x2B0, (0x340, 0x610)   # what the bars read: p[0, 0x2B0), c[0x340, 0x610)


def ranges(blob):
    """The fifteen (min, max) pairs of chunk 0x34A32 (its 120 bytes)."""
    if len(blob) != 120:
        raise ValueError('GLOBALB bar ranges (0x34A32) mismatch')
    v = struct.unpack('<30f', blob)
    return [(v[2 * k], v[2 * k + 1]) for k in range(15)]


def packages(table):
    """GLOBALB 0x34A15 -> {(category, level): [(id, weights[4], replaces[5], needs[2])]}."""
    out = {}
    for k in range(len(table) // 0x17C):
        cat, level, n = struct.unpack_from('<3I', table, 0x17C * k)
        parts = []
        for e in range(min(n, 4)):
            at = 0x17C * k + 0xC + 0x5C * e
            parts.append((struct.unpack_from('<I', table, at)[0], struct.unpack_from('<4f', table, at + 0x30),
                          [x for x in struct.unpack_from('<5i', table, at + 0x40) if x >= 0],
                          [x for x in struct.unpack_from('<2i', table, at + 0x54) if x >= 0]))
        out[(cat, level)] = parts
    return out


def f32(x):
    """x in single precision, as the game sums the levels."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def level(pk, fitted, cat):
    """(level 0..1, per-dimension sums) of a category with these parts fitted."""
    n = DIMS.get(cat, 1)
    dims = [0.0] * n
    for lv in (1, 2, 3):
        for pid, w, _, _ in pk.get((cat, lv), []):
            if pid in fitted:
                for d in range(n):
                    if w[d] != -1:
                        dims[d] = f32(dims[d] + w[d])
    total = 0.0
    for d in dims:
        total = f32(total + d)
    x = f32(total / n)
    if x < 0 or x > 1:
        dims = [min(1.0, max(0.0, d)) for d in dims]
        total = 0.0
        for d in dims:
            total = f32(total + d)
        x = f32(total / n)
    return x, dims


def fit(pk, fitted, pid, needs_ok=False):
    """Fit one part (its replaced ones off); False when it is on already or
    lacks a part it needs."""
    part = next((p for parts in pk.values() for p in parts if p[0] == pid), None)
    if part is None or pid in fitted:
        return False
    if any(n not in fitted for n in part[3]) and not needs_ok:
        return False
    for r in part[2]:
        fitted.discard(r)
    fitted.add(pid)
    return True


# The aero parts the body parts bring, by the part's level 1-3 (the game's
# RideInfo::SyncVisualPartsWithPhysics with HookupVisualAeroPartsToPhysics
# on, as the PS2 measures: a stock body has none; the GameCube's default,
# off, matches them to the mean of the tyres, the weight and the
# suspension instead, MatchUpgradeLevel, which the PS2's shop does not
# show): the front bumper and the skirts the first dimension, the rear
# bumper and the spoiler the second; a wide body kit every aero part.
AERO_PARTS = {'front_bumper': (56, 57, 58), 'skirt': (59, 60, 61), 'rear_bumper': (62, 63, 64), 'spoiler': (65, 66, 67)}


def aero_parts(pk, body):
    """The aero parts fitted for the body parts' levels ({slot: level},
    'wide': its level)."""
    fitted = set()
    if body.get('wide', 0) > 0:
        for lv in (1, 2, 3):
            for p in pk.get((9, lv), []):
                fit(pk, fitted, p[0])
        return fitted
    for slot, ids in AERO_PARTS.items():
        lv = body.get(slot, 0)
        if 1 <= lv <= 3:
            fit(pk, fitted, ids[lv - 1])
    return fitted


class Car:
    """A car record's two blocks, by their offsets."""
    def __init__(self, record):
        self.p = bytearray(record[0x110:0x110 + PHYS])
        self.c = bytes(record[0x110:0x110 + COMPLEAT[1]])

    def f(self, o):
        return struct.unpack_from('<f', self.p, o)[0]

    def set(self, o, v):
        struct.pack_into('<f', self.p, o, v)

    def cf(self, o):
        return struct.unpack_from('<f', self.c, o)[0]


def build(rec, pk, fitted, body=None):
    """The physics with these parts (ids) fitted and these body parts
    (aero_parts); returns (Car, the drivetrain's integer level)."""
    car = Car(rec)
    lv = {cat: level(pk, fitted, cat) for cat in range(9)}
    blend = lambda o, to, x: car.set(o, (to - car.f(o)) * x + car.f(o))
    x = lv[0][0]   # weight
    if x:
        car.set(0, car.f(0) + car.cf(0x344) * x)
        blend(0x160, car.cf(0x348), x)
        car.set(0x110, car.f(0x110) * ((car.cf(0x340) - 1) * x + 1))
    x, d = lv[1]   # drivetrain
    if x:
        g = int(d[2] * 3 + .5)
        if 1 <= g <= 3:
            at = 0x310 + 0x40 * g
            for o in (0x8, 0xC, 0x10, 0x0, 0x4):
                car.set(0x1B0 + o, car.cf(at + o))
            car.p[0x1D0:0x1F0] = car.c[at + 0x20:at + 0x40]
            n = 0
            while n < 6 and car.cf(at + 0x28 + 4 * n) != 0:
                n += 1
            struct.pack_into('<i', car.p, 0x1C8, n)
        blend(0x1CC, car.cf(0x3EC), d[0])
        blend(0x1C4, car.cf(0x3E4), d[0])
        car.set(0x1FC, car.f(0x1FC) + car.cf(0x41C) * d[1])
    x = lv[2][0]   # nitrous
    if x:
        blend(0x2A0, car.cf(0x580), x)
        car.set(0x2AC, car.cf(0x58C))
    x = lv[3][0]   # engine
    if x:
        for i in range(9):
            car.set(0x200 + 4 * i, car.f(0x200 + 4 * i) + car.cf(0x420 + 4 * i) * x)
    x = lv[4][0]   # turbo, also stock
    for i in range(9):
        boost = (car.cf(0x510 + 4 * i) - car.f(0x224 + 4 * i)) * x + car.f(0x224 + 4 * i)
        car.set(0x224 + 4 * i, boost)
        car.set(0x200 + 4 * i, car.f(0x200 + 4 * i) + boost)
    x, d = lv[5]   # suspension
    if x:
        for axle in (0, 1):
            src, o = (0x5C0, 0x5E0)[axle], 0x20 * axle
            blend(0x18C + o, car.cf(src + 0x1C), d[0])
            blend(0x170 + o, car.cf(src), d[0])
            blend(0x174 + o, car.cf(src + 4), d[0])
            for i in (0, 1):
                blend(0x184 + o + 4 * i, car.cf(src + 0x14 + 4 * i), d[0])
                blend(0x178 + o + 4 * i, car.cf(src + 8 + 4 * i), d[0])
            blend(0x180 + o, car.cf(src + 0x10), d[1])
        for o in (0x18, 0x48):
            car.set(o, car.f(o) + car.cf(0x604) * d[0])
        for o in (0x78, 0xA8):
            car.set(o, car.f(o) + car.cf(0x608) * d[0])
        car.set(0x8, car.f(0x8) - car.cf(0x600) * d[0])
        blend(0x270, car.cf(0x5BC), d[2])
    x = lv[6][0]   # brakes
    if x:
        for o, to in ((0x27C, 0x5B8), (0x278, 0x5B4), (0x274, 0x5B0)):
            blend(o, car.cf(to), x)
    x = lv[7][0]   # ECU
    if any(p in fitted for p in LIMITER_PARTS):
        car.set(0x290, 0.0)
    if x:
        g = int(x * 3 + .5)
        if 1 <= g <= 3:
            at = 0x410 + 0x40 * g
            for i in range(3):
                car.set(0x1F0 + 4 * i, car.f(0x1F0 + 4 * i) + car.cf(at + 4 * i))
        for i in range(9):
            car.set(0x200 + 4 * i, car.f(0x200 + 4 * i) + car.cf(0x4E0 + 4 * i) * x)
    x = lv[8][0]   # tyres
    if x:
        for axle in (0, 1):
            src, o = (0x540, 0x560)[axle], 0x20 * axle
            for i in range(5):
                blend(0xD0 + o + 4 * i, car.cf(src + 4 * i), x)
    ax, (a0, a1) = level(pk, aero_parts(pk, body or {}), 9)
    if ax:
        car.set(0x264, car.f(0x264) + (car.cf(0x5A0) + car.cf(0x590)) * a0)
        car.set(0x268, car.f(0x268) + (car.cf(0x5A4) + car.cf(0x594)) * a1)
        car.set(0x26C, car.f(0x26C) + (car.cf(0x5A8) + car.cf(0x598)) * ax)
    return car, int(lv[1][0] * 3 + .5)


def _table(lo, hi, vals):
    n = len(vals)
    scale = (n - 1) / (hi - lo)

    def get(x):
        u = scale * (x - lo)
        if u < 0:
            return vals[0]
        i = int(u)
        if i >= n - 1:
            return vals[n - 1]
        return vals[i] + (vals[i + 1] - vals[i]) * (u - i)
    return get


def drive(car):
    """(the drive m/s^2 by speed, its last speed)."""
    f = car.f
    split, mass = f(0x1C0), f(0x110)
    radius = (1 - split) * f(0x20) + split * f(0x80)
    final = (1 - split) * f(0x1B8) + split * f(0x1BC)
    idle, redline, limiter = f(0x1F0), f(0x1F4), f(0x1F8)
    torque = _table(idle, limiter, [f(0x200 + 4 * i) for i in range(9)])
    gears = struct.unpack_from('<i', car.p, 0x1C8)[0]
    ratio = lambda g: f(0x1D0 + 4 * g)
    shift = lambda g: limiter + 500 if g == gears + 1 else redline - 100
    samples, v, g = [], 0.0, 2
    while len(samples) < 92:
        rpm = v / radius * final * ratio(g) * RPM
        if rpm >= shift(g):
            g += 1
            if g >= gears + 2:
                break
        v += STEP
        samples.append(torque(rpm) * final * ratio(g) / radius / mass)
    samples[-1:] = [0.0]
    coarse = _table(0.0, v, samples)
    return _table(0.0, v, [coarse(v * i / 63) for i in range(64)]), v


def bars(car, rng, drivetrain=0):
    """(acceleration, top speed, handling), 0.03..1."""
    f = car.f
    mass = f(0x110)
    acc, vmax = drive(car)
    drag = lambda v: v * v * f(0x260) / mass
    surplus, v = 0.0, 0.0
    while v < TO_100MPH and acc(v) >= drag(v):
        surplus += (acc(v) - drag(v)) * STEP
        v += STEP
    top = 0.0
    while top <= vmax and acc(top) >= drag(top):
        top += STEP
    if f(0x290) > 0:
        top = min(top, f(0x290) * .44702727)
    norm = lambda x, k: (x - rng[k][0]) / (rng[k][1] - rng[k][0])
    nos = norm(f(0x2A0) * f(0x2AC), 4)
    keep = lambda x: min(1.0, max(.03, x))
    a = (1 - NOS_SHARE[0] - .08) * norm(surplus, 1) + NOS_SHARE[0] * nos + .08 * drivetrain / 3
    t = (1 - NOS_SHARE[1]) * norm(top, 0) + NOS_SHARE[1] * nos
    s = f(0x1C0)
    terms = (norm((f(0xDC) + f(0xE0) + f(0xFC) + f(0x100)) * .25 / mass, 7),
             norm((f(0x274) * .65 + f(0x278) * .35) / mass, 8),
             norm((f(0x174) * f(0x170) + f(0x180) + f(0x194) * f(0x190) + f(0x1A0)) * .25 / mass, 6),
             1 - norm(mass, 9),
             (s + .2) / .8 if s <= .6 else (1 - s) / .4,
             1 - norm(f(0x8), 10), 1 - norm(f(0x160), 11), norm(f(0x264), 12), norm(f(0x268), 13))
    return keep(a), keep(t), keep(sum(w * x for w, x in zip(WEIGHTS, terms)))


def stock_bars(rec, pk, rng):
    car, dt = build(rec, pk, set())
    return bars(car, rng, dt)
