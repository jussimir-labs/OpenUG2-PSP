#!/usr/bin/env python3
"""The US release's 29 player cars -> stock car packs and the fleet table.

For each car (tools/convert_physics.py playable(): GLOBALB 0x34600 records
with car index 0-30, less the two the US release hides) a stock preset
(KIT00, the car's own wheels, its stock paint) through tools/convert_car.py:
car-<MODEL>.n2pv (light, the street) and car-<MODEL>-detail.n2pv (the
garage). Then cars.n2cl, what the menus and the career need:

- name: the manufacturer (record +0xC0) and the model label CAR_NAME_<MODEL>
  of the language table (LANGUAGES/ENGLISH.BIN);
- unlock: GLOBALB chunk 0x34A1F (nfsu2-re CarUnlockEntry, 12 bytes: car
  name hash = record +0xD0, US race hash, non-US race hash; race hash 0 or
  no entry: open from the start). The race is a career race of 0x34A11
  (hash at +0x08), a URL tournament: the car opens once the player wins
  that race's row (career.json, a row per career race: need is the row's
  place among the stage's rows, from 1); without that row, once the
  player has finished the same share of the stage's events as the race's
  place among its races (approximated);
- stats for the car lot's bars, 0..1: the game's own (acceleration, top
  speed, handling of the stock car, tools/car_ratings.py: read in the
  GameCube Speed.elf, equal to the PS2's to six places), in the fixed
  ranges of GLOBALB chunk 0x34A32; bars.n2br, what psp-viewer/bars.c
  needs to rate the car with its performance parts the same way:
    'N2BR', u32 version 1, f32 the ranges' 15 (min, max) pairs;
    u32 n, n x 52 bytes, the shop's parts (0x34A15): u8 the game's
      category, u8 level 1-3, u8 its place in the package, u8 pad, u32
      the part's id, f32 weight[4] (-1 none), i32 the parts it replaces
      [5] and needs [2] (-1 none);
    u32 n, n x 1424 bytes: char model[16], the record's stock physics
      (+0x110, 0x2B0 bytes) and its upgrade values (+0x450, 0x2D0 bytes);
    (version 2) then for each of those cars, in order, the original's
      performance packages the race starter matches a rival to the player
      with (GLOBALB 0x34A31, one per car type in the record order,
      LoaderPerformanceConfigChunk; RaceStarter::MatchAICarPerformanceToPlayer):
      u32 n, n x 16 bytes: f32 the performance measure (ascending, the
      first the stock car), u8 unused, u8 the 68 parts' bits [9] (part id
      p at byte p >> 3, mask 0x80 >> (p & 7)), u8 pad[2];
- top speed (m/s) for the physics, the same cube root scaled so the 350Z
  keeps the 61 m/s the port has used.

cars.n2cl: 'N2CL', u32 version 1, u32 count, count x 80 bytes:
  char model[16], char label[28], char short[12] (CAR_NAME_<MODEL>), u32 logo (bin hash of SECONDARY_LOGO_<MODEL>,
  23 chars), u8 unlock stage, u8 events needed in it (flag 2: the row to
  win), u8 flags (1 open from the start, 2 by the unlocking race), u8 the stock paint's place in the paint grid + 1
  (stock_paints), f32 acceleration, top speed, handling (0..1),
  f32 top speed m/s.
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import car_ratings
import convert_career
import convert_hud
import convert_paints
import convert_physics
import n2feng as fe
from n2formats import archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
# One stock paint per car (the preset's base colour; the vinyl-free skin).
FIX_LABEL = {'TIBURON': 'Tiburon'}    # the table's CAR_NAME_TIBURON says "Hyundai"
TOP_350Z = 61.0


def display_name(maker, model):
    """'Nissan' + '350Z' -> 'Nissan 350Z'; a model that already names its
    maker ('Hummer H2') stands alone."""
    return model if model.lower().startswith(maker.lower()) else f'{maker} {model}'


def unlock_need(race_index, stage_races, port_events):
    """Events of the stage the port must see finished: the unlocking race's
    share of its stage (its place among the stage's races), at least one."""
    same = sorted(stage_races)
    return max(1, round((same.index(race_index) + 1) / len(same) * port_events))


def unlocks(globalb):
    races = convert_career.career_races(globalb)
    blob = [globalb[p:p + n] for t, p, n in chunks(globalb) if t == 0x34A11][0]
    by_hash = {struct.unpack_from('<I', blob, k * 0x88 + 8)[0]: k for k in range(len(blob) // 0x88)}
    table = [globalb[p:p + n] for t, p, n in chunks(globalb) if t == 0x34A1F]
    if len(table) != 1 or len(table[0]) % 12:
        raise ValueError('GLOBALB car unlock table (0x34A1F) mismatch')
    out = {}
    for car_hash, us, _ in struct.iter_unpack('<3I', table[0]):
        out[car_hash] = races[by_hash[us]] if us in by_hash else None
    return races, out


def write_bars(globalb, recs, names, path):
    """bars.n2br (module doc): what psp-viewer/bars.c needs to rate a car
    with its performance parts as the game does (tools/car_ratings.py)."""
    sec = convert_career.sections(globalb)
    rng = car_ratings.ranges(sec[0x34A32])
    pk = car_ratings.packages(sec[0x34A15])
    blob = bytearray(struct.pack('<4sI30f', b'N2BR', 2, *(v for pair in rng for v in pair)))
    parts = [(cat, lv, i, p) for (cat, lv), got in sorted(pk.items()) for i, p in enumerate(got)]
    blob += struct.pack('<I', len(parts))
    for cat, lv, i, (pid, w, replaces, needs) in parts:
        blob += struct.pack('<4BI4f5i2i', cat, lv, i, 0, pid, *w, *(replaces + [-1] * (5 - len(replaces))),
                            *(needs + [-1] * (2 - len(needs))))
    blob += struct.pack('<I', len(names))
    lo, hi = car_ratings.COMPLEAT
    for n in names:
        r = recs[n]
        blob += struct.pack('<16s', n.encode()) + r[0x110:0x110 + car_ratings.PHYS] + r[0x110 + lo:0x110 + hi]
    # the performance packages (0x34A31 x 46, in the car records' order)
    tables = [globalb[pp:pp + nn] for t, p, n in chunks(globalb) if t == 0x80034A30
              for tt, pp, nn in chunks(globalb, p, p + n) if tt == 0x34A31]
    order = list(recs)
    if len(tables) < len(order):
        raise ValueError(f'0x34A31: {len(tables)} tables for {len(order)} cars')
    for n in names:
        t = tables[order.index(n)]
        count = struct.unpack_from('<I', t, 0)[0]
        if 4 + 16 * count > len(t):
            raise ValueError(f'0x34A31 {n}: {count} entries in {len(t)} bytes')
        blob += t[:4 + 16 * count]
    path.write_bytes(blob)
    return {n: car_ratings.stock_bars(recs[n], pk, rng) for n in names}


def stock_paints(globalb, recs, names):
    """Each car's stock paint, the game's (RideInfo::SetStockParts: the
    BASE_PAINT slot from the car record's +0x84C, a paint part's hash):
    its place in the paint grid (tools/convert_paints.py, the shop's order)
    and its colour. The 206's is GLOSS_L2_COLOR03, R 53 G 102 B 158, as the
    PS2's quick race shows it."""
    grid = convert_paints.paints(globalb)
    parts = next(globalb[p:p + n] for t, p, n in chunks(globalb) if t == 0x34604)
    where = {struct.unpack_from('<I', parts, 14 * i)[0]: (k, rgb) for k, (_, i, rgb, *_) in enumerate(grid)}
    out = {}
    for n in names:
        h = struct.unpack_from('<I', recs[n], 0x84C)[0]
        if h not in where:
            raise ValueError(f'{n}: stock paint {h:08x} not in the paint grid')
        out[n] = where[h]
    return out


def build_table(game, out):
    globalb = archived(game, 'GLOBAL/GLOBALB.BUN')
    recs = convert_physics.records(globalb)
    cars = convert_physics.playable(recs)
    races, unlock = unlocks(globalb)
    port = json.loads((out / 'career.json').read_text())
    per_stage = {}
    for e in port:
        per_stage[e['stage']] = per_stage.get(e['stage'], 0) + 1
    labels = convert_hud.strings(game)
    stock = stock_paints(globalb, recs, cars)
    rows = []
    for k, name in enumerate(cars):
        r = recs[name]
        maker = r[0xC0:0xD0].split(b'\0')[0].decode().title()
        model = FIX_LABEL.get(name) or labels.get(fe.bin_hash('CAR_NAME_' + name), name)
        race = unlock.get(struct.unpack_from('<I', r, 0xD0)[0])
        if race is None:
            stage, need, flags = 0, 0, 1
        else:
            stage = race['stage']
            # the unlocking race's row (career.json: one per career race, its
            # record; a tournament's, the row it is raced from); won, the car opens
            same = [x for x in port if x['stage'] == stage]
            row = next((k for k, x in enumerate(same) if x.get('record') == race['index'] and not x['hidden']), None)
            if row is not None and row < 255:
                need, flags = row + 1, 2
            else:
                need = unlock_need(race['index'], [x['index'] for x in races if x['stage'] == stage], per_stage.get(stage, 0))
                flags = 0
        pt = convert_physics.powertrain(r)
        prof = convert_physics.profile(r)
        rpm = [pt['idle'] + (pt['limiter'] - pt['idle']) * i / 8 for i in range(9)]
        power = max(t * x for t, x in zip(pt['torque'], rpm))
        length, width, height = prof['body']
        rows.append(dict(model=name, label=display_name(maker, model), short=model, stage=stage, need=need, flags=flags,
                         power_mass=power / pt['mass'], top=(power / (width * height)) ** (1 / 3),
                         grip=prof['lat'] * prof['steer'] / prof['mass'] ** .3, paint='%02x%02x%02x' % stock[name][1], stock_paint=stock[name][0]))
    def norm(key):
        lo, hi = min(x[key] for x in rows), max(x[key] for x in rows)
        return {x['model']: .15 + .85 * (x[key] - lo) / (hi - lo) for x in rows}
    bars = write_bars(globalb, recs, [x['model'] for x in rows], out / 'bars.n2br')
    acc, top, hand = ({m: v[i] for m, v in bars.items()} for i in range(3))
    top_350z = next(x['top'] for x in rows if x['model'] == '350Z')
    blob = bytearray(struct.pack('<4s2I', b'N2CL', 1, len(rows)))
    for x in rows:
        x.update(acceleration=round(acc[x['model']], 3), top_speed=round(top[x['model']], 3),
                 handling=round(hand[x['model']], 3), top_ms=round(TOP_350Z * x['top'] / top_350z, 1))
        logo = fe.bin_hash(('SECONDARY_LOGO_' + x['model'])[:23])
        blob += struct.pack('<16s28s12sI4B4f', x['model'].encode(), x['label'].encode()[:27], x['short'].encode()[:11], logo, x['stage'], x['need'],
                            x['flags'], x['stock_paint'] + 1, x['acceleration'], x['top_speed'], x['handling'], x['top_ms'])
    (out / 'cars.n2cl').write_bytes(blob)
    return rows


def convert(game, out, row):
    with tempfile.TemporaryDirectory(prefix='nfsu2-car-') as tmp:
        preset = Path(tmp) / f"{row['model']}.json"
        preset.write_text(json.dumps({'car': row['model'], 'paint': row['paint'], 'interior': True}))   # the engine bay and the trunk in the garage's pack
        cmd = [sys.executable, str(ROOT / 'tools/convert_car.py'), '--game', str(game), '--out', str(out),
               '--preset', str(preset), '--tag', 'car-' + row['model'], '--detail']
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode:
            raise SystemExit(f"{row['model']}: {res.stderr[-800:]}")
        return row['model']


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--table-only', action='store_true', help='only cars.n2cl (packs already converted)')
    args = ap.parse_args()
    rows = build_table(args.game, args.out)
    if not args.table_only:
        with concurrent.futures.ThreadPoolExecutor(8) as pool:
            list(pool.map(lambda r: convert(args.game, args.out, r), rows))
    (args.out / 'cars.json').write_text(json.dumps(rows, indent=1) + '\n')
    for x in rows:
        print(f"{x['model']:11} {x['label']:24} stage {x['stage']} need {x['need']:2} "
              f"acc {x['acceleration']:.2f} top {x['top_speed']:.2f} hand {x['handling']:.2f} {x['top_ms']} m/s")


if __name__ == '__main__':
    main()
