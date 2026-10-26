#!/usr/bin/env python3
"""What the drift's and the drag's rules need of each car -> modes.n2md.

From the 0x890-byte GLOBALB car record (tools/convert_physics.py): mass
(t, +0x220), the air's drag factor (kN per (m/s)^2, +0x370), idle, red
line and limiter rpm (+0x300), and per gearbox (stock and transmission
levels 1-3: +0x2c0, +0x460, +0x4a0, +0x4e0, 0x40 bytes each, the game's
DriveTrainInfo) the launch's share of the way from idle to the red line
(+0x00), the rear axle's share of the drive (+0x10, which blends the two
axles' final drives at +0x08/+0x0c and the wheels' radii, +0x130 and
+0x190), the gear count (+0x18), the clutch's rate (+0x1c) and the
ratios (+0x20 + 4 x gear, first at +0x28); per ECU package (1-3: +0x560,
+0x5a0, +0x5e0, 0x40 bytes each) what it adds to the idle, red line and
limiter (the game's BuildECUPackage; the 206's +500, +1000 and +1500 on
both).

The game sorts its cars into four classes by the area under their
acceleration over speed (psp-viewer/modes.c modes_class_measure, with the
torque of the packages fitted); the report lists the stock classes.

modes.n2md: 'N2MD', u32 version 1, u32 count, count x 164 bytes:
  char model[16], f32 mass, aero, idle, redline, limiter,
  4 x (f32 launch share, clutch rate, k[6]: final x ratio / radius per
  forward gear, 1/m, 0 past the last);
  then 'N2ME' (older readers stop before it), count x 3 x f32 [3] the ECU
  packages' adds (idle, red line, limiter), the cars in the same order.
"""
import argparse
import json
from pathlib import Path
import struct

import convert_physics as cp
from n2formats import archived
import n2source

ROOT = Path(__file__).resolve().parents[1]
GEARBOX = (0x2c0, 0x460, 0x4a0, 0x4e0)
ECU = (0x560, 0x5a0, 0x5e0)


def f32(r, at):
    return struct.unpack_from('<f', r, at)[0]


def gearbox(r, at):
    rear = f32(r, at + 0x10)
    radius = (1 - rear) * f32(r, 0x130) + rear * f32(r, 0x190)
    final = (1 - rear) * f32(r, at + 0x08) + rear * f32(r, at + 0x0c)
    n = struct.unpack_from('<I', r, at + 0x18)[0]
    k = [final * f32(r, at + 0x28 + 4 * g) / radius for g in range(min(n, 6))]
    return f32(r, at), f32(r, at + 0x1c), k + [0.0] * (6 - len(k))


def stock_class(r):
    """The class with the stock torque and gearbox (modes.c's measure)."""
    idle, red, lim = f32(r, 0x300), f32(r, 0x304), f32(r, 0x308)
    tq = struct.unpack_from('<9f', r, 0x310)
    _, _, k = gearbox(r, GEARBOX[0])
    k = [x for x in k if x > 0]
    mass, aero = f32(r, 0x220), f32(r, 0x370)

    def torque(rpm):
        x = min(8.0, max(0.0, (rpm - idle) / (lim - idle) * 8))
        i = min(int(x), 7)
        return tq[i] + (tq[i + 1] - tq[i]) * (x - i)
    step, acc, g, v = 1.3410818, [], 0, 0.0
    while len(acc) < 92:
        if v * k[g] * 9.549304 >= red - 100:
            g += 1
            if g >= len(k):
                break
            continue
        v += step
        acc.append(torque(v * k[g] * 9.549304) * k[g] / mass)
    m, v = 0.0, 0.0
    for a in acc:
        d = v * v * aero / mass
        if a < d:
            break
        m += (a - d) * step
        v += step
    return m, 0 if m > 500 else 1 if m > 401 else 2 if m > 273 else 3


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    recs = cp.records(archived(args.game, 'GLOBAL/GLOBALB.BUN'))
    cars = cp.playable(recs)
    blob, report = bytearray(struct.pack('<4s2I', b'N2MD', 1, len(cars))), {}
    ext = bytearray(b'N2ME')
    for car in cars:
        r = recs[car]
        blob += struct.pack('<16s5f', car.encode(), f32(r, 0x220), f32(r, 0x370), f32(r, 0x300), f32(r, 0x304), f32(r, 0x308))
        boxes = [gearbox(r, at) for at in GEARBOX]
        for launch, clutch, k in boxes:
            blob += struct.pack('<8f', launch, clutch, *k)
        ecu = [struct.unpack_from('<3f', r, at) for at in ECU]
        ext += b''.join(struct.pack('<3f', *add) for add in ecu)
        m, c = stock_class(r)
        report[car] = dict(measure=round(m, 1), cls=c, launch=[round(b[0], 3) for b in boxes], clutch=[round(b[1], 3) for b in boxes],
                           ecu=[[round(x) for x in e[1:]] for e in ecu])
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'modes.n2md').write_bytes(blob + ext)
    print(json.dumps(report, indent=1))


if __name__ == '__main__':
    main()
