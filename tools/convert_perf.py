#!/usr/bin/env python3
"""Performance of the 29 player cars from their GLOBALB records -> perf.n2pf.

The 0x890-byte car record (chunk 0x34600, layout per OpenUG2's nfsu2.h
n2_global_physics_attr) holds the stock torque curve (nine samples from
idle to the limiter, +0x310), three upgrade gain curves added on top for
power levels 1-3 (+0x570, +0x5b0, +0x5f0; +0x530 is the master curve) and
four gearboxes (stock and transmission levels 1-3: +0x2c0, +0x460, +0x4a0,
+0x4e0; final drive +0x08, gear count +0x18, forward ratios +0x28). These
drive the engine, ECU and turbo packages (power) and the transmission
packages (launch and top speed) on the PSP. Version6 carries original
TireInfo grip/sliding targets from +0x650/+0x670 (fields3/4), per axle.
Suspension, brake and weight scaling retain the port's existing steps.
Nitrous and turbo originals are carried since version5.

The performance shop's parts are GLOBALB's career section 0x34A15: 30
records of 0x17C bytes, u32 category, u32 level (1 Street, 2 Pro, 3
Extreme), u32 parts, then four 0x5C-byte parts (u32 index, u32 name: a
language-table key, "Stage 1 Turbo Kit"; i32 price; u32 count and that
many hashes of what the part changes; at +0x30 f32 its weight). Categories 0 weight reduction, 1
transmission, 2 nitrous, 3 engine, 4 turbo, 5 suspension, 6 brakes, 7
fuel and ECU, 8 tyres (9: empty). A package costs its parts together.

perf.n2pf: 'N2PF', u32 version 5, u32 count, count x 204 bytes:
  char model[16], f32 idle rpm, limiter rpm, f32 torque[9],
  f32 gain[3][9] (power levels 1-3, added to torque), f32 launch[4]
  (first gear x final drive over stock's), f32 top[4] (stock's top gear x
  final drive over the level's: > 1 is a longer gearbox), f32 shift_rpm;
  then u32 9 (the PSP's categories, psp-viewer/perf.h order), 9 x 3 i32
  package prices, 9 x 3 x 4 parts: char name[28] ("" none), i32 price,
  f32 weight (the part's share of the category's upgrade, +0x30: a level's
  parts add up to about a third);
  then u32 count (the cars again), count x 116 bytes: char model[16], u8
  forward gears of the four gearboxes (stock, transmission 1-3), f32 [4][6]
  the speed at the limiter in each gear (m/s; the record's limiter rpm and
  front wheel radius, +0x308 and +0x130);
  then (version 5) u32 count (the cars again), count x 96 bytes: char
  model[16], f32 turbo[9] (the stock car's turbo curve, +0x334: the
  engine's boost gauge reads its peak), f32 turbo_full[9] (the curve with
  every turbo part, +0x620), f32 nitrous_s (one tank with every nitrous
  part, +0x690), f32 nitrous_torque (+0x69c). The Speed.elf of the
  GameCube names them: the record from +0x110 is CompleatCarPhysicsInfo,
  whose BuildTurboPackage and BuildNitrousPackage move the stock values
  toward these by the parts fitted.
"""
import argparse
import json
import math
from pathlib import Path
import struct

import convert_physics as cp
from n2formats import archived
import n2source

ROOT = Path(__file__).resolve().parents[1]
GEARBOX = (0x2c0, 0x460, 0x4a0, 0x4e0)
GAIN = (0x570, 0x5b0, 0x5f0)


def f32(r, at, n=1):
    v = struct.unpack_from(f'<{n}f', r, at)
    return v if n > 1 else v[0]


def gearbox(r, at):
    count = struct.unpack_from('<I', r, at + 0x18)[0]
    return f32(r, at + 0x08), f32(r, at + 0x28, 6)[:count]


def perf(r):
    idle, redline, limiter = f32(r, 0x300), f32(r, 0x304), f32(r, 0x308)
    torque = f32(r, 0x310, 9)
    gains = [f32(r, at, 9) for at in GAIN]
    boxes = [gearbox(r, at) for at in GEARBOX]
    final0, gears0 = boxes[0]
    launch = [(g[0] * f) / (gears0[0] * final0) for f, g in boxes]
    top = [(gears0[-1] * final0) / (g[-1] * f) for f, g in boxes]
    return dict(idle=idle, limiter=limiter, torque=torque, gains=gains, launch=launch, top=top, shift=redline)


def gear_speeds(r):
    """m/s at the limiter in each forward gear of the four gearboxes."""
    radius = f32(r, 0x120 + 0x10)          # the front wheel's
    limiter = f32(r, 0x308)
    out = []
    for at in GEARBOX:
        final, ratios = gearbox(r, at)
        out.append([limiter / 60 * 2 * math.pi * radius / (g * final) for g in ratios[:6] if g > 0])
    return out


# the PSP's categories (perf.h: engine, ECU, turbo, transmission,
# suspension, nitrous, tyres, brakes, weight) in the game's numbering
SHOP_CATEGORY = (3, 7, 4, 1, 5, 2, 8, 6, 0)


def shop_parts(game, globalb):
    """{(category, level): [(name, price)]} from 0x34A15 (module doc)."""
    from convert_career import sections
    from convert_hud import strings
    lang = strings(game)
    d = sections(globalb)[0x34a15]
    out = {}
    for k in range(len(d) // 0x17c):
        cat, level, n = struct.unpack_from('<3I', d, 0x17c * k)
        parts = []
        for e in range(min(n, 4)):
            at = 0x17c * k + 0xc + 0x5c * e
            _, name, price = struct.unpack_from('<IIi', d, at)
            weight = struct.unpack_from('<f', d, at + 0x30)[0]   # its share of the category's upgrade
            if name in lang:
                parts.append((lang[name], price, weight))
        out[(cat, level)] = parts
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    globalb = archived(args.game, 'GLOBAL/GLOBALB.BUN')
    recs = cp.records(globalb)
    cars = cp.playable(recs)
    blob, report = bytearray(struct.pack('<4s2I', b'N2PF', 6, len(cars))), {}
    for car in cars:
        p = perf(recs[car])
        blob += struct.pack('<16s2f9f27f4f4ff', car.encode(), p['idle'], p['limiter'], *p['torque'],
                            *(v for g in p['gains'] for v in g), *p['launch'], *p['top'], p['shift'])
        rpm = [p['idle'] + (p['limiter'] - p['idle']) * i / 8 for i in range(9)]
        peak = lambda add: max((t + a) * x for t, a, x in zip(p['torque'], add, rpm))
        stock = peak([0] * 9)
        report[car] = dict(power_levels=[round(peak(g) / stock, 3) for g in p['gains']],
                           launch=[round(v, 3) for v in p['launch']], top=[round(v, 3) for v in p['top']])
    parts = shop_parts(args.game, globalb)
    blob += struct.pack('<I', len(SHOP_CATEGORY))
    for cat in SHOP_CATEGORY:
        blob += struct.pack('<3i', *(sum(p for _, p, _ in parts.get((cat, level), [])) for level in (1, 2, 3)))
    for cat in SHOP_CATEGORY:
        for level in (1, 2, 3):
            got = parts.get((cat, level), [])[:4]
            blob += b''.join(struct.pack('<28sif', n.encode('latin-1', 'replace')[:27], price, w)
                             for n, price, w in got + [('', 0, 0.0)] * (4 - len(got)))
    report['shop'] = {f'{cat}/{level}': parts.get((cat, level), []) for cat in SHOP_CATEGORY for level in (1, 2, 3)}
    # each car's gears: the speed at the limiter in each, per gearbox (stock,
    # transmission levels 1-3): limiter rpm x wheel circumference over the
    # gear ratio x final drive (the 206's first: 37 mph, the PS2's 36)
    blob += struct.pack('<I', len(cars))
    for car in cars:
        speeds = gear_speeds(recs[car])
        blob += struct.pack('<16s4B', car.encode(), *(len(v) for v in speeds))
        for v in speeds:
            blob += struct.pack('<6f', *(v + [0.0] * (6 - len(v))))
        report[car]['gear_mph'] = [round(x * 2.237) for x in speeds[0]]
    # the turbo and the nitrous (module doc): what the packages move toward
    blob += struct.pack('<I', len(cars))
    for car in cars:
        r = recs[car]
        blob += struct.pack('<16s9f9f2f', car.encode(), *f32(r, 0x334, 9), *f32(r, 0x620, 9), f32(r, 0x690), f32(r, 0x69c))
        report[car]['turbo'] = [round(max(f32(r, 0x334, 9)), 3), round(max(f32(r, 0x620, 9)), 3)]
        report[car]['nitrous_s'] = round(f32(r, 0x690), 2)
    # v6: original per-model TireInfo[3:5], front/rear. No tuning multiplier.
    blob += struct.pack('<I', len(cars))
    for car in cars:
        target = [*f32(recs[car], 0x650 + 12, 2), *f32(recs[car], 0x670 + 12, 2)]
        if not all(math.isfinite(v) and v > 0 for v in target):
            raise ValueError(f'invalid tire target for {car}')
        blob += struct.pack('<16s4f', car.encode(), *target)
        report[car]['tire_target'] = target
    (args.out / 'perf.n2pf').write_bytes(blob)
    (args.out / 'perf.json').write_text(json.dumps(report, indent=1) + '\n')
    for car, r in report.items():
        if car != 'shop':
            print(f"{car:11} power {r['power_levels']} launch {r['launch']} top {r['top']}")
    print('shop', [[sum(p for _, p, _ in parts.get((cat, level), [])) for level in (1, 2, 3)] for cat in SHOP_CATEGORY])


if __name__ == '__main__':
    main()
