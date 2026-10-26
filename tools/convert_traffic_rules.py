#!/usr/bin/env python3
"""How much traffic the original puts on the street, of which models and how
far apart -> traffic.n2tf (psp-viewer/tdensity.c).

The rules, as the GameCube Speed.elf reads them (ROADMAP, "Fase 3", L4):

- The number of traffic cars of a race comes from its TrackInfo (GLOBALB
  chunk 0x34201, records of 0x128 bytes, the event id at +0x8A): eight
  signed bytes at +0xEC, two per traffic level (the course's direction:
  forwards, reversed), capped at 16. Level 0 is no traffic at all.
  The level: a career event's own (byte +0x36 of its 0x34A11 record), 3 in
  the career's free roam, the option in a quick race.
- Before the race clock starts no car is placed from the start line to
  +0xF8 metres ahead of it (a float per direction: 500 m, 100 m on the
  drags), unless the byte at +0xF4 (per direction) lets it.
- The gap between traffic cars is drawn in one of 9 (min, max) pairs of
  the difficulty block (GLOBALB 0x34B00 +0xA8C), picked by the traffic zone
  the player is in (the city's trigger pack, chunk 0x3414A, records of type
  0x10: a pair index per level 1-3 at +0x30, the zone's traffic type at
  +0x3C, the polygon's corners from +0x44) and the level. Out of the zones
  no car is placed.
- The models: each traffic model's car record (GLOBALB 0x34600, 0x890
  bytes, type 2 at +0x844) caps how many of it may be out at once by the
  zone's traffic type (5 bytes at +0x854, 0 = never: the bus, the
  ambulance, the fire truck) and keeps it loaded (5 bytes at +0x859: the
  taxis in type 1, the SUV and the minivan in type 2).

traffic.n2tf: 'N2TF', u32 version 1;
  u32 n, n x 20 bytes tracks (by id): u16 event id, s8 count[4 levels][2
    directions], u8 clean start allowed[2 directions], f32 clean start
    metres[2 directions];
  u32 n, n x 4 bytes career levels: u16 event id, u8 stage, u8 level (the
    first career race of the stage that lists the event);
  9 x f32 (min, max) spacing pairs;
  u32 n, n x zones: u8 pair index for levels 1-3, u8 traffic type, u16
    corners, u16 pad, f32 bounds (x min, z min, x max, z max), corners f32
    x z (GU);
  15 x 10 bytes, in psp-viewer's traffic model order (convert_traffic.py's
    MODELS): u8 max[5 traffic types], u8 keep[5].
"""
import argparse
import json
import struct
from pathlib import Path

from n2formats import archived, chunks
import convert_career
from convert_traffic import MODELS
import n2source

ROOT = Path(__file__).resolve().parents[1]
FMT_TRACK = '<H8b2B2f'    # 2 + 8 + 2 + 8 = 20 bytes


def chunk(d, cid):
    found = [d[p:p + n] for t, p, n in chunks(d) if t == cid]
    if len(found) != 1:
        raise ValueError(f'GLOBALB chunk {cid:#x}: {len(found)} copies')
    return found[0]


def tracks(globalb):
    blob = chunk(globalb, 0x34201)
    if len(blob) % 0x128:
        raise ValueError('TrackInfo (0x34201) size mismatch')
    out = {}
    for k in range(len(blob) // 0x128):
        r = blob[k * 0x128:(k + 1) * 0x128]
        eid = struct.unpack_from('<H', r, 0x8A)[0]
        counts = struct.unpack_from('<8b', r, 0xEC)
        clean_ok = tuple(r[0xF4:0xF6])
        clean_m = struct.unpack_from('<2f', r, 0xF8)
        out.setdefault(eid, (counts, clean_ok, clean_m))   # 4400 comes twice, the same
    return out


def career_levels(globalb):
    races = chunk(globalb, 0x34A11)
    out = {}
    for k in range(len(races) // 0x88):
        r = races[k * 0x88:(k + 1) * 0x88]
        for i in range(min(r[0x7E], 4)):
            eid = struct.unpack_from('<H', r, 0x18 + 4 * i)[0]
            out.setdefault((eid, r[0x37]), r[0x36])
    return out


def spacing(globalb):
    block = chunk(globalb, 0x34B00)
    return [struct.unpack_from('<2f', block, 0xA8C + 8 * i) for i in range(9)]


def zones(game):
    leaf, at, out = convert_career.trigger_pack(game), 0, []
    while at + 0x44 <= len(leaf):
        typ, = struct.unpack_from('<I', leaf, at)
        n, step = struct.unpack_from('<HH', leaf, at + 0x40)
        if step < 0x44:
            break
        if typ == 0x10:
            idx = struct.unpack_from('<3i', leaf, at + 0x30)
            kind, = struct.unpack_from('<i', leaf, at + 0x3C)
            pts = [struct.unpack_from('<2f', leaf, at + 0x44 + 8 * i) for i in range(n)]
            out.append(dict(pairs=list(idx), type=kind, corners=[(gy, gx) for gx, gy in pts]))   # GU x = game y
        at += step
    return out


def models(globalb):
    table = chunk(globalb, 0x34600)[8:]
    recs = {}
    for at in range(0, len(table), 0x890):
        r = table[at:at + 0x890]
        name = r[:32].split(b'\0')[0].decode()
        if struct.unpack_from('<i', r, 0x844)[0] == 2:
            recs[name] = (list(r[0x854:0x859]), list(r[0x859:0x85E]))
    missing = [m for m in MODELS if m not in recs]
    if missing:
        raise ValueError(f'traffic models without a car record: {missing}')
    return [recs[m] for m in MODELS]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    g = archived(args.game, 'GLOBAL/GLOBALB.BUN')
    tr, lv, sp, zs, md = tracks(g), career_levels(g), spacing(g), zones(args.game), models(g)
    out = bytearray(b'N2TF') + struct.pack('<II', 1, len(tr))
    for eid, (counts, ok, metres) in sorted(tr.items()):
        out += struct.pack(FMT_TRACK, eid, *counts, *ok, *metres)
    out += struct.pack('<I', len(lv))
    for (eid, stage), level in sorted(lv.items()):
        out += struct.pack('<HBB', eid, stage, level)
    for lo, hi in sp:
        out += struct.pack('<2f', lo, hi)
    out += struct.pack('<I', len(zs))
    for z in zs:
        xs, ys = [p[0] for p in z['corners']], [p[1] for p in z['corners']]
        out += struct.pack('<4BHH4f', *z['pairs'], z['type'], len(z['corners']), 0, min(xs), min(ys), max(xs), max(ys))
        out += b''.join(struct.pack('<2f', *p) for p in z['corners'])
    for mx, keep in md:
        out += bytes(mx) + bytes(keep)
    (args.out / 'traffic.n2tf').write_bytes(out)
    report = dict(
        tracks={eid: dict(counts=list(c), clean_ok=list(o), clean_m=list(m)) for eid, (c, o, m) in sorted(tr.items())},
        career_levels={f'{eid}/{stage}': level for (eid, stage), level in sorted(lv.items())},
        spacing=sp, zones=zs, models={m: dict(max=x, keep=k) for m, (x, k) in zip(MODELS, md)})
    (args.out / 'traffic-rules.json').write_text(json.dumps(report, indent=1) + '\n')
    never = [m for m, (x, _) in zip(MODELS, md) if not any(x)]
    print(f'traffic.n2tf: {len(out)} bytes, {len(tr)} tracks, {len(lv)} career entries, {len(zs)} zones, never out: {never}')


if __name__ == '__main__':
    main()
