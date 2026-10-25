#!/usr/bin/env python3
"""The city's far shells -> city-shell.n2cy, beside city.n2cy.

How the original draws the city far away (GameCube VisibleSectionManager::
GetSectionsToLoad, TrackStreamer; L4RA.BUN chunk 0x34151, LOD offset 20):
each scenery section n (1-19 of its letter) has a LOD section n + 20 with
its terrain, roads and big shells, and the PVS lists the LOD section alone
for the areas far from a drivable section (900 entries, ~420 m out at the
median, ~640 m at p90). The port's cells bake every section, and its rings
end at 180 m (OTIMIZACAO.md, "Alcance de desenho e pop-in"). This archive
holds only those LOD sections' instances, for world.c to draw past the
rings, out to ~650 m, where the PVS sees them.

N2CY v2 as city.n2cy (convert_city.py's module doc), with:
  - cells of 256 m (a tile of 4 x 4 of the city's 64 m cells; the header's
    cell size says so), on the city's lattice (i0, j0 in tiles);
  - only the far part of each tile (near and collision empty): the
    instances' _1B_ model (else _1A_), already cut to the tile and
    tessellated (TESSELLATE m) here, nothing cut at load (cutting at load
    cost the CPU 4-14 points of frames at 30 fps in the calibrated PPSSPP,
    the Memory Stick session's measure);
  - the tiles' blocks in Morton order, each aligned to 2 KB (the microSD's
    sector and cache: one read a tile, neighbours next to each other), the
    textures after them. A tile past BLOCK_MAX (the PSP-1000's heap finds
    a contiguous 128 KB hard) splits its batches over the index's other
    slots (near, then collision: the shell has none), the next blocks;
    world.c draws every part of a shell tile;
  - textures at most SHELL_TEX texels (seen past 180 m);
  - each tile's batches simplified by vertex clustering: the vertices of a
    batch falling in the same cell of a grid become one (their mean
    position, UV and colour), the triangles left with two corners in one
    cell go. Walls on a CLUSTER-metre grid (--cluster), the up-facing
    triangles (ground, streets, roofs: 73% of them, edge-on from the street
    that far) on a CLUSTER_UP one (--cluster-up). Drawn whole, the shells
    cost the 4101 28-44 thousand triangles a frame for a few thousand
    pixels; its area's 294,708 triangles became 93,848 at 10 m and 30,896
    at 12/32 m, the pixels off the whole shells' about the same (2026-10-11,
    on the Deck: 220/570/860 at 12/32 m, 160/610/1270 at 10 m).

  tools/convert_shell.py [--bbox X0 Y0 X1 Y1] [--out DIR]
"""
import argparse
import collections
import hashlib
import math
import struct
import time
from pathlib import Path

import convert_city as cc
import convert_world as cw
import n2world as nw
from convert_car import to_clut8

ROOT = Path(__file__).resolve().parents[1]
SHELL = 256.0          # m a tile
TESSELLATE = 1e9       # past 180 m the GE needs no cut: only the tile's own edges cut a triangle
ALIGN = 2048
BLOCK_MAX = 128 * 1024
SHELL_TEX = 32
CLUSTER = 12.0
CLUSTER_UP = 32.0


def lod_section(sec):
    """A LOD section: letters A-T (1-20), numbers 20-39 (offset 20)."""
    return 1 <= sec // 100 <= 20 and 20 <= sec % 100 < 40


def morton(x, y):
    z = 0
    for b in range(16):
        z |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1)
    return z


def bake(insts, tex_flags, bbox):
    tiles = collections.defaultdict(lambda: collections.defaultdict(list))
    for inst in insts:
        x0, y0, x1, y1 = inst.bmin[0], inst.bmin[1], inst.bmax[0], inst.bmax[1]
        if bbox and (x1 < bbox[0] or x0 > bbox[2] or y1 < bbox[1] or y0 > bbox[3]):
            continue
        base = cc._object(inst.keys[0])
        if base is None:
            continue
        obj = (cc._object(inst.keys[1]) if inst.keys[1] else None) or base
        f = nw.place(inst)
        pts = [f(*v[:3]) for v in obj.verts]
        for ids, tex in obj.subs:
            if tex not in tex_flags:
                continue
            key = (tex, tex_flags[tex])
            for j in range(0, len(ids), 3):
                tri = []
                for i in ids[j:j + 3]:
                    x, y, z = pts[i]
                    _, _, _, c, u, v = obj.verts[i]
                    tri.append((x, y, z, u, v, (c >> 16) & 255, (c >> 8) & 255, c & 255))
                xs, ys = [p[0] for p in tri], [p[1] for p in tri]
                for tx in range(math.floor(min(xs) / SHELL), math.floor(max(xs) / SHELL) + 1):
                    for ty in range(math.floor(min(ys) / SHELL), math.floor(max(ys) / SHELL) + 1):
                        bx, by = tx * SHELL, ty * SHELL
                        inside = min(xs) >= bx and max(xs) <= bx + SHELL and min(ys) >= by and max(ys) <= by + SHELL
                        pieces = [tuple(tri)] if inside else nw.clip_square(tuple(tri), bx, by, bx + SHELL, by + SHELL)
                        for piece in pieces:
                            tiles[(tx, ty)][key].extend(nw.tessellate(piece, TESSELLATE))
    return tiles


def facing_up(t):
    """The triangle's normal within ~45 degrees of straight up (ground, streets, roofs)."""
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = [p[:3] for p in t]
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    return nz > .7 * math.sqrt(nx * nx + ny * ny + nz * nz)


def cluster(tris, step, step_up=None):
    """Vertex clustering of one batch's triangles (3 x (x,y,z,u,v,r,g,b));
    the up-facing ones on a grid of step_up (0: dropped), when given."""
    if step_up is not None and step_up != step:
        up = [t for t in tris if facing_up(t)]
        rest = [t for t in tris if not facing_up(t)]
        return cluster(rest, step) + (cluster(up, step_up) if step_up > 0 else [])
    if step <= 0:
        return tris
    cells, sums = {}, {}
    def cell(p):
        k = (math.floor(p[0] / step), math.floor(p[1] / step), math.floor(p[2] / step))
        if k not in sums:
            sums[k] = [0.0] * 9
        acc = sums[k]
        for i in range(8):
            acc[i] += p[i]
        acc[8] += 1
        return k
    keyed = [tuple(cell(p) for p in t) for t in tris]
    mean = {k: tuple(v / acc[8] for v in acc[:8]) for k, acc in sums.items()}
    out = []
    for a, b, c in keyed:
        if a != b and b != c and a != c:
            out.append((mean[a], mean[b], mean[c]))
    return out


def texture_blob(sources, tex_flags, key):
    t = sources[key].get(key)
    if key in cc.KEYED_BLACK:
        t = cc.keyed_black(t)
    w, h, pal, idx, vrepeat = to_clut8(t, SHELL_TEX)
    flags = tex_flags[key]   # no wet mask: past 180 m the street's reflection is not drawn under them
    if not flags:
        pal = bytes(b if i % 4 != 3 else 255 for i, b in enumerate(pal))
    flags |= cw.scroll_flags(sources[key], key)
    return struct.pack('<3If', w, h, flags, vrepeat) + pal + idx


def remap(blob, tindex):
    b = bytearray(blob)
    n = struct.unpack_from('<I', b, 0)[0]
    at = 16
    for _ in range(n):
        key, flags, nv, ni = struct.unpack_from('<4I', b, at)
        struct.pack_into('<I', b, at, tindex[key])
        at += 48 + nv * 12
        at += (-at) % 16
        at += ni * 2
        at += (-at) % 16
    return bytes(b)


def write(blobs, sources, tex_flags, path):
    """blobs: {(tx, ty): ([far, near, collision slot's blob or b''], texture keys)}."""
    used = sorted({k for _, texs in blobs.values() for k in texs})
    tindex = {k: i for i, k in enumerate(used)}
    i0, i1 = min(t[0] for t in blobs), max(t[0] for t in blobs)
    j0, j1 = min(t[1] for t in blobs), max(t[1] for t in blobs)
    width, height = i1 - i0 + 1, j1 - j0 + 1
    index_off = 64
    tex_index_off = index_off + width * height * 24
    data_off = tex_index_off + len(used) * 12
    out = bytearray(data_off)
    index = bytearray(width * height * 24)
    big = []
    for (tx, ty) in sorted(blobs, key=lambda t: morton(t[0] - i0, t[1] - j0)):
        parts = [remap(b, tindex) if b else b'' for b in blobs[(tx, ty)][0]]
        out.extend(bytes((-len(out)) % ALIGN))
        offs = []
        for b in parts:
            if b:
                out.extend(bytes((-len(out)) % 16))
            offs.append(len(out) if b else 0)
            out.extend(b)
        far, near, third = parts
        k = (ty - j0) * width + (tx - i0)
        struct.pack_into('<6I', index, k * 24, offs[1], len(near), offs[0], len(far), offs[2], len(third))
        for b in parts:
            if len(b) > BLOCK_MAX:
                big.append(((tx, ty), len(b)))
    out.extend(bytes((-len(out)) % ALIGN))
    tindex_bytes = bytearray()
    tex_bytes = 0
    for key in used:
        blob = texture_blob(sources, tex_flags, key)
        out.extend(bytes((-len(out)) % 16))
        tindex_bytes.extend(struct.pack('<3I', key, len(out), len(blob)))
        out.extend(blob)
        tex_bytes += len(blob)
    out[index_off:index_off + len(index)] = index
    out[tex_index_off:tex_index_off + len(tindex_bytes)] = tindex_bytes
    struct.pack_into('<4sIf2i3I4f2I', out, 0, b'N2CY', 2, SHELL, i0, j0, width, height, len(used),
                     0.0, 0.0, 0.0, 0.0, index_off, tex_index_off)
    path.write_bytes(out)
    sizes = sorted(len(x) for b in blobs.values() for x in b[0] if x)
    return dict(tiles=len(blobs), grid=(i0, j0, width, height), textures=len(used), bytes=len(out), texture_bytes=tex_bytes,
                block_p50=sizes[len(sizes) // 2], block_max=sizes[-1], over_max=big[:10], over_max_count=len(big),
                sha256=hashlib.sha256(out).hexdigest())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--bbox', type=float, nargs=4, help='limit to game X0 Y0 X1 Y1 (testing)')
    ap.add_argument('--cluster', type=float, default=CLUSTER, help='vertex clustering grid (m); 0: the meshes whole')
    ap.add_argument('--cluster-up', type=float, default=CLUSTER_UP, help='the up-facing triangles\' grid (m); 0 drops them')
    args = ap.parse_args()
    t0 = time.time()
    cc._init(args.game)
    stream = cc._stream
    instances = cc.gather_instances(stream, cw.SKIP)
    groups = {k: v for k, v in nw.scenery_groups(stream.track).items() if 'BARRIERS_' in k or k.startswith('FREE_ROAM')}
    grouped = set().union(*groups.values()) if groups else set()
    lod = [i for i in instances if (i.section, i.index) not in grouped and lod_section(i.section)]
    print(len(lod), 'LOD instances of', len(instances), f'{time.time() - t0:.0f}s', flush=True)
    sources = cw.texture_index(stream, args.game)
    tex_flags = {}
    for key, tpk in sources.items():
        usage, blend = cw.draw_mode(tpk, key)
        tex_flags[key] = cw.F_ADD if blend == 2 else cw.F_BLEND if usage == 2 and blend == 1 else cw.F_ALPHA if usage == 1 else 0
    for key in (0x3394fe62, 0x2e95ce7c):
        tex_flags.pop(key, None)
    for key in cc.KEYED_BLACK:
        if key in tex_flags:
            tex_flags[key] = cw.F_ALPHA
    tiles = bake(lod, tex_flags, args.bbox)
    blobs = {}
    before = sum(len(t) for b in tiles.values() for t in b.values())
    for batches in tiles.values():
        for key in list(batches):
            batches[key] = cluster(batches[key], args.cluster, args.cluster_up)
            if not batches[key]:
                del batches[key]
    print('clustering', args.cluster, 'm:', before, '->', sum(len(t) for b in tiles.values() for t in b.values()), 'triangles', flush=True)
    for (tx, ty), batches in tiles.items():
        cx, cy = tx * SHELL + SHELL / 2 - cc.CELL / 2, ty * SHELL + SHELL / 2 - cc.CELL / 2
        blob = cc.pack_geometry(batches, cx, cy)
        if not blob:
            continue
        parts = [blob, b'', b'']
        for n in (2,):              # two blocks: the batches by size, dealt out in turn (the loader takes the third slot for collision)
            if max(len(b) for b in parts) <= BLOCK_MAX:
                break
            keys = sorted(batches, key=lambda k: -len(batches[k]))
            parts = [cc.pack_geometry({k: batches[k] for k in keys[i::n]}, cx, cy) if keys[i::n] else b'' for i in range(n)]
            parts += [b''] * (3 - n)
        blobs[(tx, ty)] = (parts, sorted({t for (t, fl) in batches}))
    info = write(blobs, sources, tex_flags, args.out / 'city-shell.n2cy')
    info['triangles'] = sum(len(t) for b in tiles.values() for t in b.values())
    print('city-shell.n2cy', info, f'{time.time() - t0:.0f}s')


if __name__ == '__main__':
    main()
