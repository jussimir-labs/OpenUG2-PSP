#!/usr/bin/env python3
"""Xbox NFSU2 GLOBALB car records -> per-car physics profiles physics.n2ph.

GLOBALB chunk 0x34600 holds one 0x890-byte record per car (layout from
OpenUG2's nfsu2.h, checked on the USA Xbox disc): mass (t) at +0x220, body
L/W/H at +0x224, four wheels at +0x120+0x30*k (x fwd, y left, z, ?, radius,
width, index), idle/redline/limiter rpm at +0x300, nine torque samples at
+0x310, steering ratio at +0x380, gearboxes at +0x2c0/+0x460/+0x4a0/+0x4e0.

Each profile carries OpenUG2's PhysVehicle multipliers (physics.c
phys_vehicle_from_source, with its geometry fallback when the powertrain
record does not validate) plus mass and wheel contact offsets for the
four-wheel sprung ride.

The record also carries the car's chase cameras (CameraInfo at +0x720,
found by decompiling the Xbox CubicCameraMover: u32 count, u32 count2,
then 16-byte entries at +0x730: s16 view id, u16 pitch (65536 = turn),
f32 forward, height, lateral). View 0 is the far chase, view 1 the close one,
view 2 the bumper, view 4 the hood (the GameCube's Speed.elf, POV types of
CubicCameraMover; their lens is 90 degrees, DriveCameraFovTable) and view 5
the drift's (PlayerSettings::ScrollDriveCam offers it in the drift only; the
far chase's dynamics, CubicDrift, and lens, 71.5 degrees: on every car 5.8 m
behind, 3.45 m up, looking 16.7 degrees down).

physics.n2ph: 'N2PH', u32 count, then count x 132 bytes:
  name[24] (pack basename: car, opp-<preset>, traffic-<MODEL>, car-<MODEL>),
  f32 mass, accel, brake, steer, lat, pitch_load, roll_load,
  f32 wheel_x[4], wheel_y[4] (game axes: +x forward, +y left), f32 radius,
  f32 body length, width, height,
  2 x (f32 eye forward, lateral, height, pitch radians) for chase views 0, 1;
  then 'POWR' and count x f32 power ratio (the stock power over the mass,
  /850, unclamped: the drive step's power cap);
  then 'DRIV' and count x f32 the rear's share of the drive (+0x2d0: 0 front,
  0.5 all four, 1 rear; the game's CarState::IsDriveWheel splits at 0.2 and
  0.8): the rear axle's slide;
  then 'POVS' and count x 2 x (f32 eye forward, lateral, height, pitch
  radians) for the bumper (view 2) and hood (view 4) views;
  then 'POV5' and count x the same for the drift's view (5);
  then 'LIMT' and count x (f32 the speed limiter in mph, +0x3a0, the
  original's RigidBody governor: 0 none, as traffic; f32 the brake balance,
  +0x38c, the rear's share: GetBrakeSettings gives the front 2 x (1 - it)
  and the rear 2 x it);
  then 'GEAR' and count x 26 f32, the drivetrain the drive step pushes
  through (vehicle.c, car_gear_drive), from the record's CarPhysicsInfo at
  +0x110 (the GameCube's Speed.elf: EngineInfo +0x1f0, DriveTrainInfo
  +0x1b0, TireInfo +0xd0, AeroInfo +0x260): idle, red line and top rpm,
  the nine torque samples (kN m, idle to top rpm), the gear count, the
  final drive, six forward ratios, the clutch's rate (a shift takes about
  its inverse in seconds), the tyres' grip and sliding friction (front,
  rear: TireInfo [3] and [4]) and the air's drag (kN per (m/s)^2); all 0
  for a car without a valid powertrain (the traffic: the arcade thrust);
  then 'AERO' and count x 3 f32, the record's AeroInfo (CarPhysicsInfo
  +0x260, record +0x370): the air's drag and the front's and the rear's
  downforce (kN per m/s, Aerodynamics::ResolveForces); 0 for traffic.
"""
import argparse
import json
import math
from pathlib import Path
import struct

from n2formats import archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
TRAFFIC = ['4DR_SEDAN', '4DR_SEDAN02', 'COUPE', 'HATCHBACK', 'HATCHBACK02', 'SUV', 'TAXI', 'TAXI02',
           'MINIVAN', 'PICKUP', 'PANELVAN', 'PARCELVAN', 'BUS', 'AMBULANCE', 'FIRETRUCK']
# OpenUG2 fleet medians (physics.h), the normalisation basis.
FLEET_VOLUME, FLEET_HEIGHT, FLEET_WHEELBASE, FLEET_TRACK, FLEET_TYREW = 13.6194, 1.5520, 2.6608, 1.5100, 0.2253


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def records(global_data):
    tables = [global_data[p:p+n] for t, p, n in chunks(global_data) if t == 0x34600]
    if len(tables) != 1 or tables[0][:8] != b'\x11' * 8 or (len(tables[0]) - 8) % 0x890:
        raise ValueError('GLOBALB car table mismatch')
    table = tables[0][8:]
    return {table[at:at+32].split(b'\0')[0].decode(): table[at:at+0x890] for at in range(0, len(table), 0x890)}


# Region-hidden in the US release (the exe's carsToHideForRegionUS): not on the disc.
NOT_US = {'CORSA', 'PEUGOT106'}


def playable(recs):
    """The US release's 29 player cars in the game's own order (car index
    at +0x840, 0-30 are the player cars)."""
    idx = {name: struct.unpack_from('<I', r, 0x840)[0] for name, r in recs.items()}
    return [n for n in sorted(idx, key=idx.get) if idx[n] <= 30 and n not in NOT_US]


def f32(r, off):
    return struct.unpack_from('<f', r, off)[0]


def from_geometry(body_len, body_wid, body_hgt, wheelbase, track, tyre_w):
    """physics.c phys_vehicle_from_geometry."""
    v = dict(accel=1.0, brake=1.0, steer=1.0, lat=1.0, pitch_load=1.0, roll_load=1.0)
    vol = body_len * body_wid * body_hgt
    if vol > 1e-3:
        m = math.sqrt(FLEET_VOLUME / vol)
        v['accel'] = v['brake'] = clamp(m, .75, 1.25)
    if wheelbase > .5:
        v['steer'] = clamp(FLEET_WHEELBASE / wheelbase, .80, 1.20)
    if body_hgt > .2 and wheelbase > .5:
        v['pitch_load'] = clamp((body_hgt / wheelbase) / (FLEET_HEIGHT / FLEET_WHEELBASE), .70, 1.40)
    if body_hgt > .2 and track > .5:
        v['roll_load'] = clamp((body_hgt / track) / (FLEET_HEIGHT / FLEET_TRACK), .70, 1.40)
    if tyre_w > .02 and track > .5:
        v['lat'] = clamp(math.sqrt(FLEET_TYREW / tyre_w) * (FLEET_TRACK / track) ** .25, .90, 1.10)
    return v


def powertrain(r):
    """nfsu2.h n2_global_physics_attr (stock level only); None if invalid."""
    mass, idle, redline, limiter = f32(r, 0x220), f32(r, 0x300), f32(r, 0x304), f32(r, 0x308)
    torque = struct.unpack_from('<9f', r, 0x310)
    steer = f32(r, 0x380)
    gears = struct.unpack_from('<I', r, 0x2c0 + 0x18)[0]
    final = f32(r, 0x2c0 + 0x08)
    forward = struct.unpack_from('<6f', r, 0x2c0 + 0x28)
    ok = (.3 <= mass <= 20 and idle >= 400 and redline > idle and limiter > redline and limiter <= 20000
          and .05 <= steer <= 3 and all(0 <= t <= 5 for t in torque) and 3 <= gears <= 6
          and .1 <= final <= 15 and all(.1 <= g <= 15 for g in forward[:gears]))
    return dict(mass=mass, idle=idle, limiter=limiter, torque=torque, steer=steer) if ok else None


def gearbox(r):
    """The 'GEAR' record: 26 floats, or zeros without a valid powertrain."""
    if not powertrain(r):
        return [0.0] * 26
    eng = struct.unpack_from('<12f', r, 0x300)   # idle, red line, top, inertia, torque[9]... (+0x110+0x1f0)
    n = struct.unpack_from('<I', r, 0x2c0 + 0x18)[0]
    ratios = list(struct.unpack_from('<6f', r, 0x2c0 + 0x28))
    ratios = [g if k < n else 0.0 for k, g in enumerate(ratios)]
    tf, tr = struct.unpack_from('<8f', r, 0x1e0), struct.unpack_from('<8f', r, 0x200)
    return [eng[0], eng[1], eng[2], *struct.unpack_from('<9f', r, 0x310), float(n), f32(r, 0x2c0 + 0x08), *ratios,
            f32(r, 0x2c0 + 0x1c), tf[3], tr[3], tf[4], tr[4], f32(r, 0x370)]


def profile(r):
    wheels = []
    for k in range(4):
        x, y, z, _, rad, w, i = struct.unpack_from('<6fI', r, 0x120 + 0x30 * k)
        if i != k:
            raise ValueError('wheel record mismatch')
        wheels.append((x, y, rad, w))
    body = struct.unpack_from('<3f', r, 0x224)
    wheelbase = max(w[0] for w in wheels) - min(w[0] for w in wheels)
    track = 2 * abs(wheels[0][1])
    tyre_w = sum(w[3] for w in wheels) / 4
    v = from_geometry(0, 0, body[2], wheelbase, track, tyre_w)   # as phys_vehicle_from_source
    pt = powertrain(r)
    if pt:
        # phys_vehicle_from_source at stock power/transmission: 850 kN.m*rpm/t
        # is OpenUG2's calibration point for the arcade thrust.
        stock = max(pt['torque'][i] * (pt['idle'] + (pt['limiter'] - pt['idle']) * i / 8) for i in range(9))
        v['accel'] = clamp(stock / pt['mass'] / 850.0, .65, 1.35) if stock > 0 else 1.0
        v['power'] = stock / pt['mass'] / 850.0 if stock > 0 else v['accel']   # unclamped: the power cap's
        v['steer'] = clamp(pt['steer'] / 1.10, .70, 1.45)
    else:
        g = from_geometry(*body, wheelbase, track, tyre_w)
        v['accel'], v['brake'] = g['accel'], g['brake']
        v['power'] = v['accel']
    mass = f32(r, 0x220)
    v['rwd'] = clamp(f32(r, 0x2d0), 0, 1)
    return dict(mass=mass if .3 <= mass <= 20 else 1.4, **v, wheels=wheels,
                radius=sum(w[2] for w in wheels) / 4, body=body, source='powertrain' if pt else 'geometry',
                cameras=chase_cameras(r), povs=chase_cameras(r, (2, 4)), drift=chase_cameras(r, (5,))[0])


# the 350Z's, for a record without them
POV_DEFAULT = {0: (-5.8, 0.0, 1.7, 0.0), 1: (-4.7, 0.0, 1.7, 0.0), 2: (0.97, 0.0, 1.04, 2.7 * math.pi / 180),
               4: (0.93, 0.0, 1.3, 0.0), 5: (-5.8, 0.0, 3.45, 16.7 * math.pi / 180)}


def chase_cameras(r, ids=(0, 1)):
    """Views of the record's CameraInfo table: 0 (far) and 1 (close) by
    default; 2 (bumper), 4 (hood), 5 (drift)."""
    count = struct.unpack_from('<i', r, 0x720)[0]
    views = {}
    for k in range(max(0, min(count, 6))):
        vid, pitch = struct.unpack_from('<hH', r, 0x730 + 16 * k)
        fwd, height, lateral = struct.unpack_from('<3f', r, 0x734 + 16 * k)
        views[vid] = (fwd, lateral, height, (pitch - 65536 if pitch > 32767 else pitch) * 2 * math.pi / 65536)
    return [views.get(v, POV_DEFAULT[v]) for v in ids]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--preset', type=Path, default=ROOT / 'presets/350z-showroom.json')
    args = ap.parse_args()
    recs = records(archived(args.game, 'GLOBAL/GLOBALB.BUN'))
    cars = [('car', json.loads(args.preset.read_text())['car'])]
    cars += [('opp-' + p.stem, json.loads(p.read_text())['car']) for p in sorted((ROOT / 'presets/opponents').glob('*.json'))]
    cars += [('traffic-' + m, m) for m in TRAFFIC]
    cars += [('car-' + m, m) for m in playable(recs)]     # the fleet (tools/convert_cars.py)
    out, report = bytearray(), {}
    for pack, car in cars:
        p = profile(recs[car])
        out += struct.pack('<24s7f', pack.encode(), p['mass'], p['accel'], p['brake'], p['steer'], p['lat'],
                           p['pitch_load'], p['roll_load'])
        out += struct.pack('<4f', *(w[0] for w in p['wheels'])) + struct.pack('<4f', *(w[1] for w in p['wheels']))
        out += struct.pack('<4f', p['radius'], *p['body'])
        out += struct.pack('<8f', *p['cameras'][0], *p['cameras'][1])
        report[pack] = {k: (round(v, 3) if isinstance(v, float) else v) for k, v in p.items() if k not in ('wheels', 'body', 'cameras', 'povs', 'drift')}
        report[pack]['cameras'] = [[round(x, 3) for x in c] for c in p['cameras']]
        report[pack]['povs'] = [[round(x, 3) for x in c] for c in p['povs']]
        report[pack]['drift'] = [round(x, 3) for x in p['drift']]
        report[pack]['car'] = car
    # then 'POWR' and each profile's power ratio (stock power / mass / 850,
    # unclamped): the power cap of the drive step
    out += b'POWR' + b''.join(struct.pack('<f', profile(recs[car])['power']) for _, car in cars)
    out += b'DRIV' + b''.join(struct.pack('<f', profile(recs[car])['rwd']) for _, car in cars)
    out += b'POVS' + b''.join(struct.pack('<8f', *sum(profile(recs[car])['povs'], ())) for _, car in cars)
    out += b'POV5' + b''.join(struct.pack('<4f', *profile(recs[car])['drift']) for _, car in cars)
    out += b'LIMT' + b''.join(struct.pack('<2f', 0.0 if pack.startswith('traffic-') else struct.unpack_from('<f', recs[car], 0x3a0)[0],
                                          struct.unpack_from('<f', recs[car], 0x38c)[0]) for pack, car in cars)
    out += b'GEAR' + b''.join(struct.pack('<26f', *([0.0] * 26 if pack.startswith('traffic-') else gearbox(recs[car]))) for pack, car in cars)
    out += b'AERO' + b''.join(struct.pack('<3f', *((0.0, 0.0, 0.0) if pack.startswith('traffic-') else struct.unpack_from('<3f', recs[car], 0x370))) for pack, car in cars)
    (args.out / 'physics.n2ph').write_bytes(struct.pack('<4sI', b'N2PH', len(cars)) + out)
    (args.out / 'physics.json').write_text(json.dumps(report, indent=1) + '\n')
    print(len(cars), 'profiles;', {k: (v['accel'], v['steer'], v['source']) for k, v in report.items() if not k.startswith('traffic')})


if __name__ == '__main__':
    main()
