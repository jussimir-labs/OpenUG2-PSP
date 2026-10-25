#!/usr/bin/env python3
"""The city's panoramas -> vista.n2pv (N2PV v3, GU world coordinates).

The PAN_ instances are the skyline's cut-outs 1.5-3 km out (Bayview's
towers, the hills with the BAYVIEW sign, the stadium, the ocean): the
original draws them past the city's far cells, unfogged, behind it all
(the _Z copies, 7-10 km out and sunk ~950 m, are another LOD's and stay
out, as do the hill rings and the glows). convert_city leaves them out of
the cells; the PSP draws this pack after the sky with a far projection,
alpha-tested, unlit.

Each PAN_ instance belongs to a scenery section (A98 = 198, E99 = 599...),
and the original draws it only from the drivable sections whose PVS list
(L4RA.BUN, chunk 0x80034150) holds that section: a district's stand-in
shows from the districts that see it from afar, not from inside it, where
its real buildings stand. Drawn always, the sheets painted 1.2 screens a
frame on the 4101 (tools/vista_fill.py), most under the city.
vista.n2vv beside the pack, for that:
  N2VV v1 (little endian): magic, version, meshes, zones (u32 each);
  meshes x u8: the mesh's section, as an index into the pack's sections
    (255: none, always drawn); padded to 4;
  zones x (s16 drivable section, u16 pad, u32 mask of the pack's
    sections its list holds).
"""
import argparse
from pathlib import Path
import struct

import convert_car as c
import convert_world as cw
import n2world as nw

ROOT = Path(__file__).resolve().parents[1]
# the hill rings round the whole map (up to 1.4 km high): the original's
# haze hides them in the sky; drawn plain they were dark arcs over it
RINGS = ('PAN_HILLSEAST', 'PAN_HILLSWEST', 'PAN_HILLRIDGE')   # (PAN_HILLRIDGEHILL, the BAYVIEW sign's, stays)


def gather(stream):
    found = {}
    for name in stream.sections:
        for inst in nw.section_instances(stream.section(name)):
            n = inst.name.upper()
            if n.startswith('PAN_') and '_Z' not in n and n not in RINGS and inst.keys[0]:
                found.setdefault((inst.keys[0], tuple(round(v, 1) for v in inst.pos)), inst)
        stream.cache.clear()
    return list(found.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--size', type=int, default=128, help='texture limit (pixels)')
    args = ap.parse_args()
    stream = nw.Stream(args.game)
    index = cw.object_index(stream)
    textures = cw.texture_index(stream, args.game)
    meshes, texdata, names, mesh_sec = [], {}, [], []
    for inst in gather(stream):
        key = inst.keys[0]
        if key not in index:
            continue
        obj = nw.section_objects(stream.section(index[key])).get(key)
        if obj is None:
            continue
        names.append(inst.name)
        f = nw.place(inst)
        for ids, tex in obj.subs:
            src = textures.get(tex)
            if src is None or not ids or src.get(tex).name.startswith('SFX_'):   # glows: the original adds them
                continue
            remap = {v: i for i, v in enumerate(sorted(set(ids)))}
            verts, cols = [], []
            for vi in remap:
                x, y, z, col, u, v = obj.verts[vi]
                gx, gy, gz = f(x, y, z)
                verts.append(struct.pack('<8f', u, v, 0, 1, 0, gy, gz, gx))   # game -> GU (y, z, x)
                cols.append(cw.colour(col))
            r, g, b = (min(255, sum(cc[k] for cc in cols) // len(cols)) for k in range(3))
            meshes.append((verts, [remap[i] for i in ids], tex, 0xff000000 | b << 16 | g << 8 | r, c.F_EMISSIVE, 1.0))
            mesh_sec.append(inst.section)
            if tex not in texdata:
                texdata[tex] = c.to_clut8(src.get(tex), args.size)
        stream.cache.clear()
    info = c.save_pack(args.out / 'vista.n2pv', meshes, texdata, 3)
    print('vista.n2pv', len(names), 'panoramas,', info['triangles'], 'triangles,', len(texdata), 'textures,', info['bytes'], 'bytes')
    secs = sorted(set(mesh_sec))
    if len(secs) > 32:
        raise SystemExit(f'{len(secs)} panorama sections: more than a u32 mask holds')
    _, _, visible = nw.drivable_sections(stream.track)
    out = bytearray(struct.pack('<4s3I', b'N2VV', 1, len(meshes), len(visible)))
    out += bytes(secs.index(sec) for sec in mesh_sec)
    out += bytes((-len(out)) % 4)
    for zone, seen in sorted(visible.items()):
        mask = sum(1 << k for k, sec in enumerate(secs) if sec in seen)
        out += struct.pack('<hHI', zone, 0, mask)
    (args.out / 'vista.n2vv').write_bytes(out)
    shown = sum(bin(m).count('1') for m in (sum(1 << k for k, sec in enumerate(secs) if sec in seen) for seen in visible.values()))
    print('vista.n2vv', len(secs), 'sections,', len(visible), 'zones,', shown, 'of', len(secs) * len(visible), 'pairs drawn')


if __name__ == '__main__':
    main()
