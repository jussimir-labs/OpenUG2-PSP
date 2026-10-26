#!/usr/bin/env python3
"""The city's fog and screen tints by region, as the game's: fog.n2fg.

TRACKS/L4RA.BUN chunk 0x8003B600 holds the parameter maps (see
convert_weather.py for the layout). Each map is a quadtree over the city
whose leaves name a set of values; the fog's sets have eight floats
(intensity, start in m, R, G, B 0-255, falloff, falloff x, falloff y), the
tints' seven (R, G, B, intensity and three more). The game (GameCube
FogQuery::CalculateFogInfo) reads "Normal Fog" and "Night Normal Fog" at the
car's place, each eased toward the new place's set by the distance driven
over 50 m (ParameterAccessorBlendByDistance), mixes them by the sun's share,
and over that "Night Raining Fog" by the rain, eased over "Fog Blend
Distance" (itself a map on some tracks). The maps' reading and their
encoding: tools/parammaps.py (psp-viewer/pmap.c reads them). The closed
tracks have maps of their own.

fog.n2fg (the city's, L4RA) and fog-L4RB.n2fg... (the tracks'): 'N2FG', u32
version 1, u32 layers; then per layer in the order of LAYERS: f32 x min,
y min, x max, y max (the game's axes), u32 nodes, u32 sets, u32 fields,
nodes x u16[4], sets x fields f32.
"""
import argparse
from pathlib import Path
import struct

from parammaps import maps, name_hash, write_layer
import n2source

ROOT = Path(__file__).resolve().parents[1]
LAYERS = ('Normal Fog', 'Night Normal Fog', 'Night Raining Fog', 'Sunrise Screen Tint', 'Night Screen Tint', 'Fog Blend Distance')
TRACKS = ('L4RA', 'L4RB', 'L4RC', 'L4RD', 'L4RF', 'L4RG')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    a = ap.parse_args()
    for track in TRACKS:
        m = maps(a.game, track)
        out = struct.pack('<4sII', b'N2FG', 1, len(LAYERS)) + b''.join(write_layer(m[name_hash(n)]) for n in LAYERS)
        target = a.out / ('fog.n2fg' if track == 'L4RA' else f'fog-{track}.n2fg')
        if target.is_symlink():
            target.unlink()
        target.write_bytes(out)
        print(f'{target.name}: {len(out)} bytes, ' + ', '.join(f'{n} {m[name_hash(n)][2]}' for n in LAYERS))

if __name__ == '__main__':
    main()
