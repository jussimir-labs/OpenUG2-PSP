#!/usr/bin/env python3
"""Xbox NFSU2 traffic cars -> PSP packs traffic-<MODEL>.n2pv (N2PV v3).

Each model is a body (detail A) plus its front tyre mounted on the four
wheel positions of its GLOBALB record. Every paint variant <MODEL>_SKINn of
CARS/TEXTURES.BIN is stored; the pack header's flags word holds
skin1 texture index | first extra skin << 8 | extra skin count << 16, and
the viewer swaps the skin texture per traffic car.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import convert_car as c
from n2formats import FlatTPK, SlotTPK, archived, chunks, game_hash, lod_family, parts
import n2source

ROOT = Path(__file__).resolve().parents[1]
MODELS = ['4DR_SEDAN', '4DR_SEDAN02', 'COUPE', 'HATCHBACK', 'HATCHBACK02', 'SUV', 'TAXI', 'TAXI02',
          'MINIVAN', 'PICKUP', 'PANELVAN', 'PARCELVAN', 'BUS', 'AMBULANCE', 'FIRETRUCK']


class TrafficTextures:
    def __init__(self, game):
        self.shared = SlotTPK(archived(game, 'CARS/TEXTURES.BIN'))
        self.globalb = FlatTPK(archived(game, 'GLOBAL/GLOBALB.BUN'))
        self.names = {self.shared.name(k): k for k in self.shared.slots}

    def get(self, key):
        return self.shared.get(key) or self.globalb.get(key)


def wheels_for(global_data, name):
    """GLOBALB wheel table without the passenger-car sanity ranges (buses
    and fire trucks are longer and taller)."""
    tables = [global_data[p:p+n] for t, p, n in chunks(global_data) if t == 0x34600]
    table = tables[0][8:]
    for at in range(0, len(table), 0x890):
        if table[at:at+32].split(b'\0')[0] != name.encode():
            continue
        out = []
        for k in range(4):
            x, y, z, _, r, w, i = struct.unpack_from('<6fI', table, at + 0x120 + 0x30 * k)
            if i != k or not (.15 < r < .8 and abs(y) < 2 and abs(x) < 6):
                raise ValueError(f'{name}: wheel record mismatch')
            out.append((x, y, z, r, w))
        return out
    raise ValueError(f'{name}: no GLOBALB record')


def exists(game, name):
    z = (game / 'ZDIR.BIN').read_bytes()
    h = game_hash(name)
    return any(e[0] == h for e in struct.iter_unpack('<6I', z))


def tyre_at(choices, lod):
    """The tyre of a level, or the nearest finer one drawing with the full
    one's textures (4DR_SEDAN's B: a texture of its own, 35 KB, for two
    triangles)."""
    full = {tk for _, tk, _ in c.pick_lod(choices, 0).subs}
    pick = c.far_pick if lod else c.pick_lod   # (a far level by its letter: convert_car.far_pick)
    while lod and not {tk for _, tk, _ in pick(choices, lod).subs} <= full:
        lod -= 1
    return pick(choices, lod) if lod else c.pick_lod(choices, 0)


def build(game, model, textures, global_data, lod=0):
    """lod: the body's level (the street's far ones: B, C), the tyres' up to their last."""
    data = archived(game, f'CARS/{model}/GEOMETRY.BIN')
    all_parts = list(parts(data))
    families = {}
    for p in all_parts:
        families.setdefault(lod_family(p.name), []).append(p)
    body = [p for f, ps in families.items() if 'BODY' in f for p in [(c.far_pick if lod else c.pick_lod)(ps, lod)]]
    tyre = [tyre_at(ps, lod) for f, ps in families.items() if 'TIRE' in f]
    wheels = wheels_for(global_data, model)
    b = c.Builder(textures, (200, 200, 200))
    b.names = sorted(p.name for p in body + tyre)
    for p in body:
        b.add(p, c.identity)
    for p in tyre:
        mid = (min(v[1] for v in p.verts) + max(v[1] for v in p.verts)) / 2
        for k, mount in enumerate(wheels):
            f, mirror = c.wheel_xform(mount, mid)
            b.add(p, f, mirror, wheel=k)
    return b


def save(path, meshes, textures, skins, far=()):
    """far: [(level, meshes)], the body's far levels beside the pack
    (convert_car.save_lods, <pack>.n2pl): their textures after its own."""
    used = c.used_textures([m for _, ms in far for m in ms], c.used_textures(meshes))
    skin1 = skins[0]
    extra = [k for k in skins[1:] if k not in used]
    first_extra = len(used)
    used += extra
    flags = (used.index(skin1) if skin1 in used else 255) | (first_extra << 8) | (len(extra) << 16)
    out = bytearray(struct.pack('<4s6I', b'N2PV', 3, len(used), len(meshes), sum(len(m[1]) // 3 for m in meshes), 2, flags))
    def align():
        out.extend(bytes((-len(out)) % 16))
    align()
    for key in used:
        w, h, pal, idx, vrepeat = c.to_clut8(textures.get(key), 128)
        out.extend(struct.pack('<5If', key, w, h, 1, len(idx), vrepeat))
        align()
        out.extend(pal)
        out.extend(idx)
        align()
    for vertices, ids, ref, colour, fl, uvscale in meshes:
        out.extend(struct.pack('<5If', len(vertices), len(ids), used.index(ref) if ref is not None else 0xffffffff,
                               colour, fl, uvscale))
        align()
        out.extend(b''.join(vertices))
        align()
        out.extend(struct.pack('<' + str(len(ids)) + 'H', *ids))
        align()
    path.write_bytes(out)
    info = dict(bytes=len(out), triangles=sum(len(m[1]) // 3 for m in meshes), skins=1 + len(extra),
                sha256=hashlib.sha256(out).hexdigest())
    if far:
        info['far_levels'] = c.save_lods(path.with_suffix('.n2pl'), far, used, len(out), 0)
    return info


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    textures = TrafficTextures(args.game)
    global_data = archived(args.game, 'GLOBAL/GLOBALB.BUN')
    report = {}
    for model in MODELS:
        if not exists(args.game, f'CARS/{model}/GEOMETRY.BIN'):
            report[model] = 'absent'
            continue
        try:
            b = build(args.game, model, textures, global_data)
        except ValueError as e:
            report[model] = str(e)
            continue
        skins = [textures.names[f'{model}_SKIN{n}'] for n in range(1, 9) if f'{model}_SKIN{n}' in textures.names]
        if not skins:
            report[model] = 'no skins'
            continue
        # the original's traffic: TrafficCarBodyLodSwapSize, the body's B and C (D: none)
        # (a level that needs a texture the pack has not: left out, 4DR_SEDAN's B: 35 KB)
        meshes, far, names = b.batched(), [], b.names
        for level in (1, 2):
            lb = build(args.game, model, textures, global_data, level)
            lm = lb.batched()
            if lb.names != names and set(c.used_textures(lm)) <= set(c.used_textures(meshes)):   # (a model with fewer levels: its last one goes on)
                far.append((level, lm))
                names = lb.names
        report[model] = save(args.out / f'traffic-{model}.n2pv', meshes, textures, skins, far)
    (args.out / 'traffic.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: (v if isinstance(v, str) else (v['triangles'], v['skins'], v['bytes'])) for k, v in report.items()}))


if __name__ == '__main__':
    main()
