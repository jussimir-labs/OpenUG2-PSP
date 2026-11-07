#!/usr/bin/env python3
"""The city's panoramas -> vista.n2pv (N2PV v3, GU world coordinates).

The PAN_ instances are the skyline's cut-outs 1.5-3 km out (Bayview's
towers, the hills with the BAYVIEW sign, the stadium, the ocean): the
original draws them past the city's far cells, behind it all
(the _Z copies, 7-10 km out and sunk ~950 m, are another LOD's and stay
out, as do the hill rings and the glows). convert_city leaves them out of
the cells; the PSP draws this pack after the sky with a far projection,
alpha-tested, unlit, under the region's haze as the original draws every
model of its view (psp-viewer/main.c vista_haze; beta issue 105).

Each mesh keeps its texture's draw mode, as convert_city's: blend 2 is
added (F_VISTA_ADD), usage 2 with blend 1 mixed by its alpha (F_GLASS, the
pack's blended pass), the rest cut out by the alpha test. The street
lights over the hills (ARC_STREETLIGHTS_A_DM: lit dots on black, added in
the original) and the lit office windows were drawn opaque, black boxes
on the hills and the towers (beta issue 115).

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
import n2source

ROOT = Path(__file__).resolve().parents[1]
F_VISTA_ADD = 1 << 16   # added over what is behind (psp-viewer/main.c draw_vista); bits 16-19 are a car decal's alone
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


def jettison_class(section, lod=20):
    """The sections the original's TrackStreamer::ChooseSectionToJettison
    lets go first (priority 999): scenery A-T numbered from twice the level's
    LOD offset up (L4RA: n40-n99), the panoramas among them."""
    return 1 <= section // 100 <= 20 and section % 100 >= 2 * lod


def panorama_support(stream, index):
    """A test (x, y, z) -> whether a point stands on a panorama sheet's
    surface (from 4 m under it to 2 m over it): the island's palms over the
    bay stand on PAN_Parkdrive, and without the sheet they float (beta issue
    #105). The ocean's sheets are the water, not a backdrop: left out."""
    cell = 32.0
    grid = {}
    for inst in gather(stream):
        if 'OCEAN' in inst.name.upper() or inst.keys[0] not in index:
            continue
        obj = nw.section_objects(stream.section(index[inst.keys[0]])).get(inst.keys[0])
        if obj is None:
            continue
        f = nw.place(inst)
        pts = [f(*v[:3]) for v in obj.verts]
        for ids, _ in obj.subs:
            for j in range(0, len(ids) - 2, 3):
                t = (pts[ids[j]], pts[ids[j + 1]], pts[ids[j + 2]])
                xs, ys = [p[0] for p in t], [p[1] for p in t]
                for gx in range(int(min(xs) // cell), int(max(xs) // cell) + 1):
                    for gy in range(int(min(ys) // cell), int(max(ys) // cell) + 1):
                        grid.setdefault((gx, gy), []).append(t)
        stream.cache.clear()

    def height(t, x, y):
        (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = t
        d = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
        if abs(d) < 1e-9:
            return None
        a = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / d
        b = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / d
        return None if min(a, b, 1 - a - b) < -1e-6 else a * z1 + b * z2 + (1 - a - b) * z3

    def on(x, y, z):
        for t in grid.get((int(x // cell), int(y // cell)), ()):
            h = height(t, x, y)
            if h is not None and -4 <= z - h <= 2:
                return True
        return False
    return on


LEVELS = 7   # the mipmaps' levels, down to 16 x 8 (main.c VISTA_LEVELS)


def unswizzle(idx, w, h):
    """convert_car.psp_swizzle undone: 16-byte x 8-row blocks -> rows."""
    out = bytearray(w * h)
    at = 0
    for y in range(0, h, 8):
        for x in range(0, w, 16):
            for row in range(8):
                pos = (y + row) * w + x
                out[pos:pos + 16] = idx[at:at + 16]
                at += 16
    return bytes(out)


def mip_levels(w, h, pal, idx):
    """The levels main.c's vista_make_mips made at load: of each 2 x 2
    texels the one nearest their mean colour (the palette kept, the cut-outs'
    alpha sharp), while the level above is 32 x 16 or more. Swizzled each."""
    import numpy as np
    p = np.frombuffer(pal, dtype=np.uint8).reshape(256, 4).astype(np.int64)
    src = np.frombuffer(unswizzle(idx, w, h), dtype=np.uint8).reshape(h, w)
    out = []
    while len(out) < LEVELS and src.shape[1] >= 32 and src.shape[0] >= 16:
        q = [src[0::2, 0::2], src[0::2, 1::2], src[1::2, 0::2], src[1::2, 1::2]]
        m = sum(p[k] for k in q)
        d = np.stack([((4 * p[k] - m) ** 2).sum(axis=-1) for k in q])
        best = np.argmin(d, axis=0)   # the first of equals, as the C loop's strict <
        src = np.choose(best, q).astype(np.uint8)
        out.append(c.psp_swizzle(src.tobytes(), src.shape[1], src.shape[0]))
    return out


def save_compact(path, meshes, texdata, mesh_sec, secs, visible):
    """vista.n2vs: the pack as the PSP-1000's profile keeps it, already in
    the form main.c's vista_build and vista_make_mips gave it at load (16-bit
    strip vertices, the textures and their mipmaps, the zones' masks), read
    in one block and kept whole: the original streams its panorama sections
    straight into the track streamer's pool, no build after the read.
    N2VS v1 (little endian, blobs 16-aligned, offsets from the file's start):
      header 32 B: magic, version, textures, meshes, zones, file bytes, 2 pad.
      textures x 48 B: u16 w, h; u8 levels, 3 pad; u32 palette (1024 B
        RGBA); u32 x 8 the levels' swizzled indices (0 the top).
      meshes x 80 B: f32 centre xyz, half xyz, origin xyz, scale, u offset,
        v offset, uv scale; u32 vertices offset, count, triangles, colour,
        flags; u16 texture (0xffff none); u8 strip; u8 section (an index
        into the pack's sections as vista.n2vv's, 255 none); 4 pad.
      zones x 8 B: as vista.n2vv's (s16 drivable section, pad, u32 mask).
    A vertex: u16 u, v (u = u16 / 32768 x uv scale + u offset), s16 x y z
    (x = origin + s16 / 32767 x scale)."""
    import math
    import tristrip
    used = c.used_textures(meshes)
    tex_rec, blobs = [], []
    head = 32 + 48 * len(used) + 80 * len(meshes) + 8 * len(visible)
    at = (head + 15) & ~15
    def place(data):
        nonlocal at
        off = at
        blobs.append((off, data))
        at = (at + len(data) + 15) & ~15
        return off
    for ref in used:
        w, h, pal, idx, _ = texdata[ref]
        levels = mip_levels(w, h, pal, idx)
        offs = [place(pal), place(idx)] + [place(lv) for lv in levels]
        tex_rec.append(struct.pack('<2HB3x9I4x', w, h, len(levels), *(offs + [0] * (9 - len(offs)))))
    mesh_rec = []
    for (verts, ids, ref, colour, flags, _), sec in zip(meshes, mesh_sec):
        vs = [struct.unpack('<8f', v) for v in verts]   # u, v, 0, 1, 0, GU x, y, z
        tris = [tuple(ids[j:j + 3]) for j in range(0, len(ids), 3)]
        pieces = (tristrip.in_order if flags & c.F_GLASS else tristrip.stripify)(tris)
        seq = tristrip.join(pieces) if pieces else []
        strip = 3 <= len(seq) <= 65535
        if not strip:
            seq = list(ids)
        pts = [vs[i] for i in seq]
        lo = [min(p[5 + a] for p in pts) for a in range(3)]
        hi = [max(p[5 + a] for p in pts) for a in range(3)]
        ulo, uhi = min(p[0] for p in pts), max(p[0] for p in pts)
        vlo, vhi = min(p[1] for p in pts), max(p[1] for p in pts)
        centre = [(lo[a] + hi[a]) * .5 for a in range(3)]
        half = [(hi[a] - lo[a]) * .5 + 1. for a in range(3)]
        ext = max([1e-3] + [(hi[a] - lo[a]) * .5 for a in range(3)])
        uvs = max(uhi - ulo, vhi - vlo, 1e-3)
        def q16(x):
            return max(-32768, min(32767, round(x)))
        out = bytearray()
        for p in pts:
            out += struct.pack('<2H3h', min(65535, int((p[0] - ulo) / uvs * 32768. + .5)), min(65535, int((p[1] - vlo) / uvs * 32768. + .5)),
                               *(q16((p[5 + a] - centre[a]) / ext * 32767.) for a in range(3)))
        off = place(bytes(out))
        mesh_rec.append(struct.pack('<13f5IHBB4x', *centre, *half, *centre, ext, ulo, vlo, uvs, off, len(pts), len(ids) // 3,
                                    colour, flags, used.index(ref) if ref is not None else 0xffff, strip,
                                    secs.index(sec) if sec in secs else 255))
    zones = b''.join(struct.pack('<hHI', zone, 0, sum(1 << k for k, s in enumerate(secs) if s in seen))
                     for zone, seen in sorted(visible.items()))
    data = bytearray(struct.pack('<4s5I8x', b'N2VS', 1, len(used), len(meshes), len(visible), at))
    data += b''.join(tex_rec) + b''.join(mesh_rec) + zones
    data += bytes(at - len(data))
    for off, blob in blobs:
        data[off:off + len(blob)] = blob
    assert len(data) == at and not any(math.isnan(x) for r in mesh_rec for x in struct.unpack_from('<13f', r))
    path.write_bytes(data)
    return len(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
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
            usage, blend = cw.draw_mode(src, tex)
            mode = F_VISTA_ADD if blend == 2 else c.F_GLASS if usage == 2 and blend == 1 else 0
            meshes.append((verts, [remap[i] for i in ids], tex, 0xff000000 | b << 16 | g << 8 | r, c.F_EMISSIVE | mode, 1.0))
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
    print('vista.n2vs', save_compact(args.out / 'vista.n2vs', meshes, texdata, mesh_sec, secs, visible), 'bytes (the PSP-1000\'s, built)')


if __name__ == '__main__':
    main()
