#!/usr/bin/env python3
"""Xbox NFSU2 skies -> PSP packs sky.n2pv (night), sky-sunrise.n2pv and
sky-sunset.n2pv (N2PV v3, unit radius).

The SKYDOME model (instance in section Z0, model in X0) carries the sunrise
dome/cap keys; the time-of-day textures swap at run time (the Xbox's
0x1feed0: in the career's free roam the sky starts at sunrise and turns
sunrise, night, sunset, night, a fade at each turn; races stay at night).
Each pack gives the dome slot SKY_<TIME>_A and the cap SKY_<TIME>_A_CAP from
TRACKS/LOC4DYNTEX.BIN. Vertices are scaled to a unit dome, drawn around the
camera.
"""
import argparse
from pathlib import Path
import struct

import convert_car as c
import convert_world as cw
from n2formats import SlotTPK, archived
import n2world as nw
import n2source

ROOT = Path(__file__).resolve().parents[1]
SKYDOME = 0xbe43edbb
NIGHT = {0x2414a01e: 0x8a9a05cf, 0x5fb8bcd1: 0xb0eb9302}   # sunrise dome/cap -> night
TIMES = {'sky.n2pv': 'NIGHT', 'sky-sunrise.n2pv': 'SUNRISE', 'sky-sunset.n2pv': 'SUNSET'}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    stream = nw.Stream(args.game)
    obj = nw.section_objects(stream.section(cw.object_index(stream)[SKYDOME]))[SKYDOME]
    radius = max(max(abs(v[0]), abs(v[1]), abs(v[2])) for v in obj.verts)
    loc4 = SlotTPK(archived(args.game, 'TRACKS/LOC4DYNTEX.BIN'))
    names = {loc4.name(k): k for k in loc4.slots}
    cap_of = {0x2414a01e: False, 0x5fb8bcd1: True}   # the model's dome and cap slots
    for out, time in TIMES.items():
        meshes, texdata = [], {}
        for ids, tex in obj.subs:
            key = names[f'SKY_{time}_A' + ('_CAP' if cap_of[tex] else '')]
            verts = []
            remap = {v: i for i, v in enumerate(sorted(set(ids)))}
            for vi in remap:
                x, y, z, _, u, v = obj.verts[vi]
                # game (X fwd, Y left, Z up) -> GU (Y, Z, X), unit radius
                verts.append(struct.pack('<8f', u, v, 0, 1, 0, y / radius, z / radius, x / radius))
            meshes.append((verts, [remap[i] for i in ids], key, 0xffb0b0b0, c.F_EMISSIVE, 1.0))
            texdata[key] = c.to_clut8(loc4.get(key), 256)
        info = c.save_pack(args.out / out, meshes, texdata, 3)
        print(out, info['triangles'], 'triangles,', info['bytes'], 'bytes')

if __name__ == '__main__':
    main()
