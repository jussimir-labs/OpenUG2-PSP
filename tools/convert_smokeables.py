#!/usr/bin/env python3
"""The city's knock-over props (the original's smokeables) -> smokeables.n2sm,
beside city.n2cy. Python stdlib only.

The original (Speed.elf of the GameCube, classes Smokeable, VirginSmokeable,
SmokeableSpawnerPack, LoaderSmokeables) reads two chunks:

  L4RA.BUN 0x34026  the types (SmokeableInfo), 256 bytes each, 16-aligned
           after the chunk header: n2world.smokeable_types.
  STREAML4RA 0x34027  per scenery section a spawner pack: 16-byte header
           (s16 section @+8, s16 first spawner id @+10, s16 count @+12),
           then 112 bytes a spawner: u32 type key, u16 scenery override
           (0x34107: the scenery instance it stands for), u32 model key @+8,
           box min @+0x10, max @+0x20, a 4x4 matrix @+0x30 (rows: the
           local axes, then the place): n2world.smokeable_spawners.

The city has 514 of them (ids 0-513) in 112 sections, of 12 of the 79 types:
crash barrels (227), street signs (157), garbage cans (62), orange
bumpers (26), no-parking signs (17) and a few doors, chairs, tables,
fences, water barrels and saw horses. Each is a scenery instance of the
group SMOKEABLE drawn like any other until a car touches it; then the
game hides the instance and puts a rigid body in its place
(VirginSmokeable::ConvertToSmokeable). convert_city.py leaves these
instances out of the cells (psp-viewer/smokeable.c draws them).

Each type's collision is its GJK polytope (L4RA.BUN 0x80034020: 0x39200
header with the key and the name, 0x39201 its vertices, 48 bytes each,
the position at +0x10): its box here.

N2SM v1 (little endian, sections 16-aligned):
  header 64 B: magic, version, types, models, textures, spawners, then the
    offsets of the four tables, the city grid's cell size (64 m).
  types x 80 B: f32 mass (t), f32 inertia xx yy zz (t m^2, the body's own
    axes), f32 centre of gravity x y z, f32 anchor (impulse it takes to
    move, t m/s), f32 life (s awake before it goes; 0 forever), f32
    bounce, f32 friction, f32 box centre x y z, f32 box half x y z; all GU
    axes (game y, z, x) and model space; then u8 category, u8 emitters
    (bit 0 BARREL_WATERA, 1 GLASSWALLA, 2 any other awake emitter), 10
    bytes pad.
  models x 16 B: u32 offset, u32 size of a geometry blob (convert_city's
    batch format, the texture field an index into this file's textures,
    model space GU), f32 radius, u32 pad.
  textures x 16 B: u32 offset, u32 size of a texture blob (convert_city's:
    w, h, flags, vrepeat, CLUT, swizzled indices), 8 pad.
  spawners x 64 B: u16 type, u16 near model, u16 far model, u16 id, f32
    GU position x y z, f32 3x3 rotation (rows: the model's GU axes in GU
    world), f32 the box's radius about the position, u32 pad. Sorted
    by the city's 64 m cells (j along game Y, then i along game X).
"""
import argparse
import json
import math
from pathlib import Path
import struct

from convert_car import to_clut8
import convert_city as cc
import convert_world as cw
import n2world as nw
from n2formats import chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
CATEGORIES = ['NONE', 'GARBAGE_BAG', 'SHOPPING_CART', 'METAL_FENCE', 'TRAFFIC_CONE', 'WATER_DRUM', 'OIL_DRUM',
              'PARKING_METER', 'ROAD_SIGN_METAL', 'ROAD_SIGN_WOOD', 'MARKET_STALL', 'SANDWICH_BOARD', 'SAW_HORSE',
              'METAL_SMALL', 'METAL_BIG', 'WOODBOX', 'TRASHCAN', 'GLASS', 'CHAIR']   # SmokeableCategoryNameTable
EMIT_WATER, EMIT_GLASS = 0x95c9f5fa, 0xe84348ea   # BARREL_WATERA, GLASSWALLA (GLOBALB's emitter groups)


def gu(v):
    """game (x, y, z) -> GU (y, z, x)"""
    return (v[1], v[2], v[0])


def polytopes(track_bun):
    """key -> the GJK polytope's vertices (model space, game axes)."""
    out, key = {}, None
    for t, p, n in chunks(track_bun):
        if t == 0x39200:
            key = struct.unpack_from('<I', nw.unpad(track_bun[p:p+n]), 0x18)[0]
        elif t == 0x39201 and key is not None:
            b = track_bun[p:p+n]
            out[key] = [struct.unpack_from('<3f', b, at + 0x10) for at in range(0, len(b) - 47, 48)]
            key = None
    return out


def model_blob(obj, tex_index, tex_flags):
    """A model's triangles in its own space (game axes) as a geometry blob
    whose origin is the model's (convert_city.pack_geometry about the
    origin), the texture field this file's texture index."""
    batches = {}
    for ids, tex in obj.subs:
        if tex not in tex_flags:
            continue
        for j in range(0, len(ids), 3):
            tri = []
            for i in ids[j:j+3]:
                x, y, z, c, u, v = obj.verts[i]
                tri.append((x, y, z, u, v, (c >> 16) & 255, (c >> 8) & 255, c & 255))
            batches.setdefault((tex, tex_flags[tex]), []).append(tuple(tri))
    blob = cc.pack_geometry(batches, -cc.CELL / 2, -cc.CELL / 2)
    if not blob:
        return b'', 0.0
    b = bytearray(blob)
    n, at = struct.unpack_from('<I', b, 0)[0], 16
    for _ in range(n):
        key, flags, nv, ni = struct.unpack_from('<4I', b, at)
        struct.pack_into('<I', b, at, tex_index.setdefault(key, len(tex_index)))
        at += 48 + nv * 12
        at += (-at) % 16
        at += ni * 2
        at += (-at) % 16
    radius = max(math.sqrt(x * x + y * y + z * z) for x, y, z, *_ in obj.verts)
    return bytes(b), radius


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    stream = nw.Stream(args.game)
    types = nw.smokeable_types(stream.track)
    spawners = nw.smokeable_spawners(stream)
    polys = polytopes(stream.track)
    index = cw.object_index(stream)
    sources = cw.texture_index(stream, args.game)
    cc._init(args.game)   # convert_city's model cache and strips

    used = sorted({s['type'] for s in spawners}, key=lambda k: types[k]['name'])
    type_no = {k: i for i, k in enumerate(used)}
    models, model_no, tex_index, tex_flags = [], {}, {}, {}
    for key, tpk in sources.items():
        usage, blend = cw.draw_mode(tpk, key)
        tex_flags[key] = cw.F_ADD if blend == 2 else cw.F_BLEND if usage == 2 and blend == 1 else cw.F_ALPHA if usage == 1 else 0

    def model(key):
        if key not in model_no:
            obj = cc._object(key)
            if obj is None:
                raise ValueError(f'model {key:#x} not in the stream')
            models.append(model_blob(obj, tex_index, tex_flags) + (obj.name,))
            model_no[key] = len(models) - 1
        return model_no[key]

    out_spawners = []
    for s in spawners:
        near = model(s['model'])
        far_key = s['keys'][2] or s['keys'][1]   # convert_city's far detail (_1Z_, else _1B_)
        far = model(far_key) if far_key and index.get(far_key) else near
        m = s['matrix']
        rows = [gu(m[1]), gu(m[2]), gu(m[0])]   # the model's GU axes (its game y, z, x) in GU world
        lo, hi = s['box']
        radius = 0.5 * math.dist(lo, hi)
        out_spawners.append((s, near, far, gu(m[3]), rows, radius))
    # by the city's cells: j (game Y), then i (game X)
    out_spawners.sort(key=lambda e: (math.floor(e[0]['matrix'][3][1] / cc.CELL), math.floor(e[0]['matrix'][3][0] / cc.CELL), e[0]['id']))

    type_rec, report_types = [], {}
    for k in used:
        t = types[k]
        pts = polys.get(k)
        if pts:
            lo = [min(p[a] for p in pts) for a in range(3)]
            hi = [max(p[a] for p in pts) for a in range(3)]
        else:   # Smokeable::Init: the model's box when the type has no polytope
            obj = cc._object(next(s['model'] for s in spawners if s['type'] == k))
            lo = [min(v[a] for v in obj.verts) for a in range(3)]
            hi = [max(v[a] for v in obj.verts) for a in range(3)]
        centre = [(a + b) / 2 for a, b in zip(lo, hi)]
        half = [(b - a) / 2 for a, b in zip(lo, hi)]
        emit = (1 if EMIT_WATER in t['emitters'] else 0) | (2 if EMIT_GLASS in t['emitters'] else 0) \
            | (4 if any(e and e not in (EMIT_WATER, EMIT_GLASS) for e in t['emitters']) else 0)
        cat = CATEGORIES.index(t['category']) if t['category'] in CATEGORIES else 0
        type_rec.append(struct.pack('<17f4B8x', t['mass'], *gu(t['inertia']), *gu(t['cg']), t['anchor'], t['life'],
                                    t['bounce'], t['friction'], *gu(centre), *gu(half), cat, emit, 0, 0))
        report_types[t['name']] = dict(spawners=sum(1 for s in spawners if s['type'] == k), mass_t=round(t['mass'], 4),
                                       anchor=t['anchor'], life_s=t['life'], bounce=t['bounce'], friction=t['friction'],
                                       category=t['category'], box_m=[round(2 * h, 2) for h in half], polytope=bool(pts))

    tex_keys = sorted(tex_index, key=tex_index.get)
    tex_blobs = []
    for key in tex_keys:
        t = sources[key].get(key)
        w, h, pal, idx, vrepeat = to_clut8(t, 64)
        flags = tex_flags[key]
        if not flags:
            pal = bytes(b if i % 4 != 3 else 255 for i, b in enumerate(pal))
        tex_blobs.append(struct.pack('<3If', w, h, flags, vrepeat) + pal + idx)

    def al(b):
        return b + bytes((-len(b)) % 16)
    head = 64
    types_off = head
    models_off = types_off + 80 * len(type_rec)
    tex_off = models_off + 16 * len(models)
    spawn_off = tex_off + 16 * len(tex_blobs)
    data_off = spawn_off + 64 * len(out_spawners)
    data = bytearray()
    model_tab, tex_tab = bytearray(), bytearray()
    for blob, radius, name in models:
        model_tab += struct.pack('<2If4x', data_off + len(data), len(blob), radius)
        data += al(blob)
    for blob in tex_blobs:
        tex_tab += struct.pack('<2I8x', data_off + len(data), len(blob))
        data += al(blob)
    spawn_tab = bytearray()
    for s, near, far, pos, rows, radius in out_spawners:
        spawn_tab += struct.pack('<4H3f9ff4x', type_no[s['type']], near, far, s['id'], *pos, *rows[0], *rows[1], *rows[2], radius)
    out = struct.pack('<4s5I4If20x', b'N2SM', 1, len(type_rec), len(models), len(tex_blobs), len(out_spawners),
                      types_off, models_off, tex_off, spawn_off, cc.CELL)
    out += b''.join(type_rec) + model_tab + tex_tab + spawn_tab
    assert len(out) == data_off
    out += data
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'smokeables.n2sm').write_bytes(out)
    report = dict(spawners=len(out_spawners), types=report_types, models={name: len(blob) for blob, _, name in models},
                  textures=len(tex_blobs), bytes=len(out),
                  bytes_by_kind=dict(geometry=sum(len(b) for b, _, _ in models), textures=sum(len(b) for b in tex_blobs),
                                     tables=data_off))
    (args.out / 'smokeables.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report, indent=1))


if __name__ == '__main__':
    main()
