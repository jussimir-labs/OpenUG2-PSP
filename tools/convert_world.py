#!/usr/bin/env python3
"""Xbox NFSU2 city (L4RA) -> PSP world region (N2WD v1). Python stdlib only.

Bakes every scenery instance of a circular region into world space, splits
large triangles (the GE rejects triangles crossing the near plane), bins
the result into square cells batched by texture, and writes:
  world.n2wd  cells + CLUT8 texture bank + start pose
  world.n2wc  collision: two ground layers and a wall mask on a 1 m grid
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import struct

from convert_car import psp_swizzle, to_clut8, write_png
from n2formats import FlatTPK, SlotTPK, archived, chunks
import n2world as nw
import n2source

ROOT = Path(__file__).resolve().parents[1]
SKIP = ('ZPM_', 'PAN_', 'ANM_JUMBO', 'SFX_', 'SKYDOME', 'SKY_', 'XO_VISTA_SPOTLIGHT')  # race props, panoramas, flybys, sky (drawn apart), vista beams
F_ALPHA, F_BLEND, F_ADD = 1, 2, 4      # mirrored in psp-viewer/world.c
F_DECAL = 8   # opaque paint lying on another surface: drawn after it with a depth bias
F_CULL = 0x100   # batch flag: a sheet drawn with its back faces culled (convert_city.mark_sheets)
F_WET = 16    # texture flag: asphalt whose alpha is the original's wet mask (convert_city.wet)
# The texture's own UV scroll, mirrored in psp-viewer/world.c: bits 5-6 the
# type (1 smooth, 2 snap), bit 7 the axis (T, else S), bits 8-15 the snap
# step (1/256 s), bits 16-31 the speed (s16, 1/256 of the texture a second)
F_SCROLL_SMOOTH, F_SCROLL_SNAP, F_SCROLL_T = 0x20, 0x40, 0x80


def tex_info(tpk, key):
    """The TPK record from its key on (Xbox TextureInfo less 0x30)."""
    if isinstance(tpk, FlatTPK):
        return tpk.records[key][1]
    raw, p = tpk.raw(key)
    return raw[p:]


def draw_mode(tpk, key):
    """TPK record draw bytes relative to the key: usage +0x31, blend +0x32."""
    info = tex_info(tpk, key)
    return info[0x31], info[0x32]


def scroll_flags(tpk, key):
    """The record's scroll as F_SCROLL_* bits, 0 when still: ScrollType
    +0x2e (1 smooth, 2 snap), then from +0x34 s16 ScrollTimeStep (1/256 s),
    ScrollSpeedS, ScrollSpeedT (1/256 of the texture a second). Snap moves
    the UV by speed x time, time counted in whole steps: the race
    barriers' neon chevrons (OBJ_TRACKBARRIERB_NEON_, S -320 every 76)
    jump 0.37 of a panel forward every 0.3 s, as on the PS2. Every
    scrolling texture of the game moves along one axis."""
    info = tex_info(tpk, key)
    kind = info[0x2e]
    step, speed_s, speed_t = struct.unpack_from('<3h', info, 0x34)
    if kind not in (1, 2) or not (speed_s or speed_t):
        return 0
    flags = F_SCROLL_SMOOTH if kind == 1 else F_SCROLL_SNAP | min(max(step, 1), 255) << 8
    if not speed_s:
        flags |= F_SCROLL_T
    return flags | ((speed_s or speed_t) & 0xffff) << 16


def object_index(stream):
    """key -> section holding that model. Built once, cached locally."""
    import n2formats
    plat = '' if n2formats.PLATFORM == 'xbox' else f'-{n2formats.PLATFORM}'   # each disc its own index
    cache = ROOT / f'.tools/cache/{getattr(stream, "name", "L4RA")}{plat}-objects.json'
    if cache.exists():
        return {int(k, 16): v for k, v in json.loads(cache.read_text()).items()}
    index = {}
    for name in stream.sections:
        d = stream.section(name)
        for t, p, n in chunks(d):
            if t == 0x134011:
                key = struct.unpack_from('<I', nw.unpad(d[p:p+n]), 0x10)[0]
                index.setdefault(key, name)
        stream.cache.clear()
    cache.parent.mkdir(parents=True, exist_ok=True)
    tmp = cache.with_suffix('.tmp')       # whole or nothing: workers read it
    tmp.write_text(json.dumps({f'{k:08x}': v for k, v in index.items()}))
    tmp.replace(cache)
    return index


def texture_index(stream, game):
    """key -> texture source: section TPKs (full-res Y sets first), then
    LOC4DYNTEX (facades, sky), InGameCommon and GLOBALB, as OpenUG2 resolves them."""
    found = {}
    loc4 = SlotTPK(archived(game, 'TRACKS/LOC4DYNTEX.BIN'))
    for key in loc4.slots:
        found[key] = loc4
    order = sorted(stream.sections, key=lambda n: (not n.startswith('Y') or int(n[1:] or 0) >= 50, n))
    for name in order:
        d = stream.section(name)
        if not any(t == 0xb3300000 for t, p, n in chunks(d)):
            continue
        tpk = FlatTPK(d)
        for key in tpk.records:
            found.setdefault(key, tpk)
    # the race barriers' art (OBJ_TRACKBARRIERB_*, SFX_NEONBAR*), dropped
    # while unresolved: the barriers stood invisible
    common = FlatTPK(archived(game, 'GLOBAL/INGAMECOMMON.BUN'))
    for key in common.records:
        found.setdefault(key, common)
    # and GLOBALB, the game's always-loaded pack: HEADLIGHTGLOW, the glow
    # cards round the street lamps, the wall lamps and the parking lots'
    # lights (additive; ~4000 triangles of the city), the only key of
    # the city's it resolves
    globalb = FlatTPK(archived(game, 'GLOBAL/GLOBALB.BUN'))
    for key in globalb.records:
        found.setdefault(key, globalb)
    return found


def far_key(inst):
    """Instance detail keys are (_1A_, _1B_, _1Z_); 0 when absent."""
    return inst.keys[2] or inst.keys[1] or inst.keys[0]


def normal_z(tri):
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = tri
    ax, ay, az, bx, by, bz = x1 - x0, y1 - y0, z1 - z0, x2 - x0, y2 - y0, z2 - z0
    nx, ny, nz = ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    return nz / length if length > 1e-12 else 0.0


def gu(x, y, z):
    """Game (X forward, Y left, Z up) -> GU world (x, y, z) = (Y, Z, X)."""
    return y, z, x


def colour(bgra):
    b, g, r = bgra & 255, (bgra >> 8) & 255, (bgra >> 16) & 255
    return r, g, b


def build(game, centre, radius, cell, limit, ground_limit=4.0):
    stream = nw.Stream(game)
    index = object_index(stream)
    cx, cy = centre
    objects, instances = {}, []
    for name in stream.sections:
        for inst in nw.section_instances(stream.section(name)):
            if inst.name.upper().startswith(SKIP) or not any(inst.keys):
                continue
            mx, my = (inst.bmin[0] + inst.bmax[0]) / 2, (inst.bmin[1] + inst.bmax[1]) / 2
            half = math.hypot(inst.bmax[0] - inst.bmin[0], inst.bmax[1] - inst.bmin[1]) / 2
            if math.hypot(mx - cx, my - cy) - half <= radius:
                instances.append(inst)
        stream.cache.clear()
    # Deduplicate identical placements copied into several sections.
    unique = {}
    for inst in instances:
        k = (inst.keys[0], tuple(round(v, 2) for v in inst.pos), tuple(round(v, 3) for v in inst.rot))
        unique.setdefault(k, inst)
    instances = list(unique.values())
    needed = {k for inst in instances for k in (inst.keys[0], far_key(inst))}
    for key in needed:
        if key in index and key not in objects:
            sec = index[key]
            objects.update({k: o for k, o in nw.section_objects(stream.section(sec)).items() if k in needed})
    stream.cache.clear()
    tris = collections.defaultdict(list)       # (cell, lod, texkey) -> triangles
    box = (cx - radius, cy - radius, cx + radius, cy + radius)
    ground, walls, roads, missing = [], [], [], 0
    for inst in instances:
        if objects.get(inst.keys[0]) is None:
            missing += 1
            continue
        f = nw.place(inst)
        # LOD 0 (near): the _1A_ model, drivable ground split fine for the
        # chase camera. LOD 1 (far): the game's _1Z_/_1B_ model, coarse split.
        for lod, key in ((0, inst.keys[0]), (1, far_key(inst))):
            obj = objects.get(key) or objects[inst.keys[0]]
            drivable = bool(obj.flags & nw.DRIVABLE)
            road = drivable and 'ROAD' in obj.name.upper()
            pts = [f(*v[:3]) for v in obj.verts]
            for ids, tex in obj.subs:
                for j in range(0, len(ids), 3):
                    tri = []
                    for i in ids[j:j+3]:
                        x, y, z = pts[i]
                        _, _, _, c, u, v = obj.verts[i]
                        r, g, b = colour(c)
                        tri.append((x, y, z, u, v, r, g, b))
                    xs, ys = [p[0] for p in tri], [p[1] for p in tri]
                    if max(xs) < box[0] or min(xs) > box[2] or max(ys) < box[1] or min(ys) > box[3]:
                        continue
                    pieces = [tuple(tri)]
                    if min(xs) < box[0] or max(xs) > box[2] or min(ys) < box[1] or max(ys) > box[3]:
                        pieces = nw.clip_square(tuple(tri), *box)
                    if lod == 0:
                        terrain = obj.name.upper().startswith('TRN_')
                        for piece in pieces:
                            t3 = [p[:3] for p in piece]
                            nz = abs(normal_z(t3))
                            # Ground: walkable slopes of drivable meshes, plus flat
                            # terrain (parks, verges); everything else may block.
                            if (drivable and nz >= .45) or (terrain and nz >= .8):
                                ground.append(t3)
                                if road:
                                    roads.append(t3)
                            else:
                                walls.append(t3)
                    # The chase camera rides ~2 m above drivable ground: split it
                    # finer so pieces crossing the near plane (dropped by the GE)
                    # stay small. Far cells never reach the near plane.
                    split = (ground_limit if drivable else limit) if lod == 0 else 24.0
                    for a, bb, cc in (t for piece in pieces for t in nw.tessellate(piece, split)):
                        mx, my = (a[0] + bb[0] + cc[0]) / 3, (a[1] + bb[1] + cc[1]) / 3
                        tris[(int(math.floor(mx / cell)), int(math.floor(my / cell))), lod, tex].append((a, bb, cc))
    return stream, instances, tris, ground, walls, roads, missing


def collision(ground, walls, centre, radius, step=1.0):
    n = int(2 * radius / step) + 1
    return collision_grid(ground, walls, centre[0] - radius, centre[1] - radius, n, step)


NONE = -(1 << 30)    # no ground in a collision cell (heights are millimetres)
# Walls: scenery between 0.5 m (kerbs and low steps are climbed) and 1.45 m
# (the tallest racer's roof, 1.43 m) above a ground layer. Overhangs higher
# up (a guard rail's top lip, awnings, signs) pass over the car; at 1.8 m
# they stopped it up to half a metre before it touched anything.
WALL_TOP = 1.45
# A wall that also rises CAM_WALL_TOP or more above its layer (a building's
# side, the game's walls of no top, -1000 to 3000 m) is one the chase
# camera's eye (at most 3.45 m up) cannot look over: wall bit << 4, the
# camera's alone (psp-viewer world_wall_high). Rails and low fences stay
# below it, the camera over them as before.
CAM_WALL_TOP = 3.5
# m: a wall face whose part in a 1 m column spans less than this across is a
# point of contact with the column's border, not a wall in it
POINT_CONTACT = .01


def clip_column(tri, x0, y0, x1, y1):
    """The part of a 3D triangle inside the vertical column [x0,x1] x [y0,y1]
    (a polygon, possibly flat: vertical faces clip to a strip)."""
    poly = list(tri)
    for axis, bound, keep_above in ((0, x0, True), (0, x1, False), (1, y0, True), (1, y1, False)):
        out = []
        for a, b in zip(poly, poly[1:] + poly[:1]):
            ina, inb = (a[axis] >= bound) == keep_above, (b[axis] >= bound) == keep_above
            if a[axis] == bound:
                ina = True
            if b[axis] == bound:
                inb = True
            if ina:
                out.append(a)
            if ina != inb and a[axis] != b[axis]:
                t = (bound - a[axis]) / (b[axis] - a[axis])
                out.append(tuple(a[c] + (b[c] - a[c]) * t for c in range(3)))
        poly = out
        if not poly:
            return []
    return poly


def clip_z(poly, z0, z1):
    """The part of a polygon between heights z0 and z1."""
    for bound, keep_above in ((z0, True), (z1, False)):
        out = []
        for a, b in zip(poly, poly[1:] + poly[:1]):
            ina, inb = (a[2] >= bound) == keep_above, (b[2] >= bound) == keep_above
            if ina:
                out.append(a)
            if ina != inb and a[2] != b[2]:
                t = (bound - a[2]) / (b[2] - a[2])
                out.append(tuple(a[c] + (b[c] - a[c]) * t for c in range(3)))
        poly = out
        if not poly:
            return []
    return poly


# Wall shape of a 1 m collision cell: the strip lo <= n.(p - centre) <= hi
# that holds the wall geometry, n = (cos a, sin a) in game X/Y with
# a = angle * pi / 64. u16: angle (6 bits) << 10 | lo (5) << 5 | width (5),
# lo = code * SHAPE_UNIT - 0.75, hi = lo + width * SHAPE_UNIT (rounded
# outwards). The PSP collides with the cell clipped to that strip instead of
# the whole cell: a wall stops the car where it is, not up to 1 m before.
SHAPE_FULL = 0xFFFF
SHAPE_UNIT = 1.5 / 31


def hull(pts):
    pts = sorted(set((round(x, 4), round(y, 4)) for x, y in pts))
    if len(pts) < 3:
        return pts
    def half(seq):
        h = []
        for p in seq:
            while len(h) >= 2 and (h[-1][0] - h[-2][0]) * (p[1] - h[-2][1]) - (h[-1][1] - h[-2][1]) * (p[0] - h[-2][0]) <= 0:
                h.pop()
            h.append(p)
        return h
    lower, upper = half(pts), half(reversed(pts))
    return lower[:-1] + upper[:-1]


def shape_code(pts):
    h = hull(pts)
    if not h:
        return SHAPE_FULL
    # The narrowest strip lies along a hull edge; try those directions and
    # their neighbours among the 64 stored angles.
    cands = {0, 32}
    for (ax, ay), (bx, by) in zip(h, h[1:] + h[:1]):
        if (ax, ay) != (bx, by):
            a = (math.atan2(by - ay, bx - ax) + math.pi / 2) % math.pi
            k = int(round(a / math.pi * 64)) % 64
            cands.update(((k - 1) % 64, k, (k + 1) % 64))
    best = None
    for k in cands:
        a = k * math.pi / 64
        nx, ny = math.cos(a), math.sin(a)
        vs = [x * nx + y * ny for x, y in h]
        lo, hi = min(vs), max(vs)
        if best is None or hi - lo < best[0]:
            best = (hi - lo, k, lo, hi)
    _, k, lo, hi = best
    lc = max(0, min(31, int(math.floor((lo + .75) / SHAPE_UNIT + 1e-9))))
    wc = max(0, min(31 - lc, int(math.ceil((hi + .75) / SHAPE_UNIT - 1e-9)) - lc))
    if lc + wc >= 31 and lc == 0:
        return SHAPE_FULL
    return k << 10 | lc << 5 | wc


def shape_strip(code):
    """(nx, ny, lo, hi) of a shape code, None for SHAPE_FULL."""
    if code == SHAPE_FULL:
        return None
    a = (code >> 10) * math.pi / 64
    lo = ((code >> 5) & 31) * SHAPE_UNIT - .75
    return math.cos(a), math.sin(a), lo, lo + (code & 31) * SHAPE_UNIT


def grid_cells_inside(tri, ox, oy, n, step=1.0):
    """(cell k = j * n + i, height at its centre) for each cell of the n x n
    grid at (ox, oy) whose centre a ground triangle covers."""
    xs, ys = [p[0] for p in tri], [p[1] for p in tri]
    i0, i1 = max(0, int((min(xs) - ox) / step)), min(n - 1, int((max(xs) - ox) / step))
    j0, j1 = max(0, int((min(ys) - oy) / step)), min(n - 1, int((max(ys) - oy) / step))
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = tri
    det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
    if abs(det) < 1e-9:
        return
    for j in range(j0, j1 + 1):
        py = oy + (j + .5) * step
        for i in range(i0, i1 + 1):
            px = ox + (i + .5) * step
            w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / det
            w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / det
            w2 = 1 - w0 - w1
            if min(w0, w1, w2) >= -.02:
                yield j * n + i, w0 * z0 + w1 * z1 + w2 * z2


TERRAIN_REACH = 300   # mm: a layer takes the terrain of the ground triangle this near it at the cell's centre


def terrain_layers(ground, terrain, layers, ox, oy, n, step=1.0):
    """The game's TerrainType of each cell of each ground layer (lists of
    mm per cell from collision_grid, NONE = none): that of the ground
    triangle nearest the layer's height at the cell's centre (the
    triangle the layer came from), 0 where none is within TERRAIN_REACH
    (the pinholes the grid fills, empty cells)."""
    out = [[0] * (n * n) for _ in layers]
    best = [[TERRAIN_REACH + 1] * (n * n) for _ in layers]
    for tri, kind in zip(ground, terrain):
        for k, z in grid_cells_inside(tri, ox, oy, n, step):
            mm = int(round(z * 1000))
            for layer, o, b in zip(layers, out, best):
                if layer[k] in (NONE, -NONE):
                    continue
                d = abs(mm - layer[k])
                if d < b[k]:
                    b[k], o[k] = d, kind
    return out


def collision_grid(ground, walls, ox, oy, n, step=1.0, want_mid=False, road=None, want_shape=False, want_mid2=False, room=1500):
    """1 m grid in game axes. Two ground layers (lowest/highest walkable
    surface, millimetres, NONE = none) and wall bits per layer: bit 0 relative to
    the low layer, bit 1 to the high one. An obstacle is any non-ground
    surface between 0.5 m and WALL_TOP above that layer (one that also reaches
    CAM_WALL_TOP above it sets the bit << 4 too). Sloped faces are tested
    by their plane height at the cell centre; near-vertical faces (whose 2D
    projection is a line) are rasterised along their edges instead of their
    bounding box. Grid origin (ox, oy) in game X/Y, n x n cells.
    want_mid adds a third, middle layer (mm, NONE = none; wall bit 2) for
    three-level interchanges, returned after the wall bits. Where more than
    three levels stack, road surfaces (road[i] true for ground[i]) are kept
    before terrain and the lowest/highest/middle slots hold the kept ones;
    terrain with road 1-3.2 m above it is dropped.
    Each wall triangle is clipped to the cell's column and to each layer's
    band, so thin slanted faces (guard rail beams) mark the cells they cross.
    want_shape also returns, after mid, a u16 per cell: the strip of the cell
    the wall geometry occupies (shape_code; SHAPE_FULL = the whole cell).
    want_mid2 (with want_mid and want_shape) keeps a fourth level where four
    stack (interchanges over a street over a tunnel): a second middle layer
    above mid (wall bit 3), returned after the shapes. room (mm): ground
    with other ground less than this over it (and 0.25 m or more) is never
    driven on and is left out (want_mid)."""
    lo = [-NONE] * (n * n)
    hi = [NONE] * (n * n)
    def cells_inside(tri):
        return grid_cells_inside(tri, ox, oy, n, step)
    def cells_on_edges(tri):
        seen = set()
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            steps = max(1, int(length / (step * .25)))
            for s in range(steps + 1):
                t = s / steps
                i = int((a[0] + (b[0] - a[0]) * t - ox) / step)
                j = int((a[1] + (b[1] - a[1]) * t - oy) / step)
                if 0 <= i < n and 0 <= j < n and j * n + i not in seen:
                    seen.add(j * n + i)
                    yield j * n + i
    samples = collections.defaultdict(list) if want_mid else None
    for t, tri in enumerate(ground):
        is_road = bool(road[t]) if road else False
        for k, z in cells_inside(tri):
            mm = int(round(z * 1000))
            lo[k] = min(lo[k], mm)
            hi[k] = max(hi[k], mm)
            if want_mid:
                samples[k].append((mm, is_road))
    mid = [NONE] * (n * n)
    mid2 = [NONE] * (n * n)
    if want_mid:
        for k, zs in samples.items():
            if road and not all(r for _, r in zs):
                # Ground that is not road with road 1-3.2 m above it (too
                # low for a car under it): the game's fill under decks (flag 0x08).
                over = [z for z, r in zs if r]
                kept = [(z, r) for z, r in zs if r or not any(1000 < o - z < 3200 for o in over)]
                if len(kept) < len(zs):
                    zs[:] = kept
                    lo[k], hi[k] = min(z for z, _ in zs), max(z for z, _ in zs)
            # Ground with ground over it at the same point, 0.25 m up to
            # room (1.5 m; the closed tracks 2 m), is never driven on: no
            # room under the upper one for a car (its roof is 1.43 m up,
            # a Hummer's 1.9), and the game finds the ground from above,
            # the upper surface. The shortcut tunnel's ramp rises from a
            # floor the scenery does not draw (4042, 4044): taking the nearer
            # layer, cars kept to that floor up to its end, under the ramp.
            under = [(z, r) for z, r in zs if not any(250 < o - z < room for o, _ in zs)]
            if len(under) < len(zs):
                zs[:] = under
                lo[k], hi[k] = min(z for z, _ in zs), max(z for z, _ in zs)
            zs.sort()
            clusters, cur = [], [zs[0]]
            for z in zs[1:]:
                if z[0] - cur[-1][0] > 2500:
                    clusters.append(cur)
                    cur = [z]
                else:
                    cur.append(z)
            clusters.append(cur)
            keep = 4 if want_mid2 else 3
            if len(clusters) > keep:
                ranked = sorted(clusters, key=lambda c: (sum(r for _, r in c), len(c)), reverse=True)[:keep]
                clusters = [c for c in clusters if any(c is r for r in ranked)]
                lo[k], hi[k] = clusters[0][0][0], clusters[-1][-1][0]
            if len(clusters) >= 3:
                best = clusters[1]
                mid[k] = best[len(best) // 2][0]
            if len(clusters) == 4:
                best = clusters[2]
                mid2[k] = best[len(best) // 2][0]
    # Close pinholes between separate ground meshes (seams sampled between
    # cell centres): an empty cell with 3+ grounded 4-neighbours takes their mean.
    for _ in range(2):
        fill = {}
        for j in range(1, n - 1):
            for i in range(1, n - 1):
                k = j * n + i
                if hi[k] != NONE:
                    continue
                around = [m for m in (k - 1, k + 1, k - n, k + n) if hi[m] != NONE]
                if len(around) >= 3:
                    mids = [mid[m] for m in around if mid[m] != NONE]
                    fill[k] = (sum(lo[m] for m in around) // len(around), sum(hi[m] for m in around) // len(around),
                               sum(mids) // len(mids) if len(mids) >= 3 else NONE)
        for k, (a, b, c) in fill.items():
            lo[k], hi[k], mid[k] = a, b, c
    wall = bytearray(n * n)
    shape_pts = collections.defaultdict(list) if want_shape else None
    for tri in walls:
        xs, ys = [p[0] for p in tri], [p[1] for p in tri]
        i0, i1 = max(0, int(math.floor((min(xs) - ox) / step))), min(n - 1, int(math.floor((max(xs) - ox) / step)))
        j0, j1 = max(0, int(math.floor((min(ys) - oy) / step))), min(n - 1, int(math.floor((max(ys) - oy) / step)))
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                x0, y0 = ox + i * step, oy + j * step
                poly = clip_column(tri, x0, y0, x0 + step, y0 + step)
                if not poly:
                    continue
                # A face that only meets the column at a point (a wall's end
                # on the cell's border) stands in the next cell, not this one.
                # Kept, its strip had no direction: shape_code took angle 0
                # and drew a 1 m stub across the cell, square to the wall
                # (beta issue 32: the car hit one reversing along a wall).
                if all(abs(p[0] - poly[0][0]) < POINT_CONTACT and abs(p[1] - poly[0][1]) < POINT_CONTACT for p in poly):
                    continue
                zmin, zmax = min(p[2] for p in poly), max(p[2] for p in poly)
                k = j * n + i
                for bit, layer in ((1, lo[k]), (2, hi[k]), (4, mid[k]), (8, mid2[k])):
                    if layer in (-NONE, NONE):
                        continue
                    base = layer / 1000
                    if zmax > base + .5 and zmin < base + WALL_TOP:
                        wall[k] |= bit | (bit << 4 if zmax >= base + CAM_WALL_TOP else 0)
                        if shape_pts is not None:
                            band = clip_z(poly, base + .5, base + WALL_TOP)
                            cx, cy = x0 + step / 2, y0 + step / 2
                            shape_pts[k].extend((p[0] - cx, p[1] - cy) for p in (band or poly))
    lo = [v if v != -NONE else NONE for v in lo]
    if want_shape:
        shape = [SHAPE_FULL] * (n * n)
        for k, pts in shape_pts.items():
            shape[k] = shape_code(pts)
        if want_mid2:
            return n, ox, oy, step, lo, hi, wall, mid, shape, mid2
        return n, ox, oy, step, lo, hi, wall, mid, shape
    if want_mid:
        return n, ox, oy, step, lo, hi, wall, mid
    return n, ox, oy, step, lo, hi, wall


def write_world(path, tris, texdata, start, cell):
    """N2WD v2: header, CLUT8 textures, then cells (x0,y0,x1,y1 in game X/Y,
    near batch count, far batch count) followed by their batches."""
    cells = collections.defaultdict(lambda: ([], []))
    for (c, lod, tex), ts in tris.items():
        cells[c][lod].append((tex, ts))
    used = sorted({tex for (c, lod, tex) in tris})
    tindex = {k: i for i, k in enumerate(used)}
    out = bytearray(struct.pack('<4s5I4f', b'N2WD', 2, len(used), len(cells), 0, 0, *start))
    def align():
        out.extend(bytes((-len(out)) % 16))
    align()
    for key in used:
        w, h, pal, idx, vrepeat, flags = texdata[key]
        out.extend(struct.pack('<5If', key, w, h, flags, len(idx), vrepeat))
        align()
        out.extend(pal)
        out.extend(idx)
        align()
    stats = dict(cells=len(cells), batches=0, triangles=[0, 0], vertices=0)
    def emit(tex, ts):
        """Weld identical vertices; split to keep 16-bit indices."""
        count = 0
        for first in range(0, len(ts), 20000):
            verts, ids, remap = [], [], {}
            ymin, ymax = 1e9, -1e9
            for tri in ts[first:first + 20000]:
                for x, y, z, u, v, r, g, b in tri:
                    gx, gy, gz = gu(x, y, z)
                    key = struct.pack('<2fI3f', u, v, int(r) | int(g) << 8 | int(b) << 16 | 0xff000000, gx, gy, gz)
                    if key not in remap:
                        remap[key] = len(verts)
                        verts.append(key)
                    ids.append(remap[key])
                    ymin, ymax = min(ymin, gy), max(ymax, gy)
            body.append((struct.pack('<4I2f', tindex[tex], texdata[tex][5], len(verts), len(ids), ymin, ymax), verts, ids))
            stats['vertices'] += len(verts)
            count += 1
        return count
    for (ci, cj), lods in sorted(cells.items()):
        body = []
        counts = []
        for lod in (0, 1):
            n = 0
            for tex, ts in sorted(lods[lod], key=lambda b: b[0]):
                n += emit(tex, ts)
                stats['triangles'][lod] += len(ts)
            counts.append(n)
        out.extend(struct.pack('<4f2I', ci * cell, cj * cell, (ci + 1) * cell, (cj + 1) * cell, *counts))
        align()
        for head, verts, ids in body:
            out.extend(head)
            align()
            out.extend(b''.join(verts))
            align()
            out.extend(struct.pack('<' + str(len(ids)) + 'H', *ids))
            align()
            stats['batches'] += 1
    struct.pack_into('<I', out, 16, stats['batches'])
    path.write_bytes(out)
    stats.update(bytes=len(out), textures=len(used), sha256=hashlib.sha256(out).hexdigest())
    return stats


def pick_start(roads, centre, grid):
    """Road triangle nearest the requested point whose surroundings (3 m)
    are free of walls in the collision grid; heading along its longest
    horizontal edge."""
    n, ox, oy, step, lo, hi, wall = grid
    def clear(x, y):
        for dj in range(-3, 4):
            for di in range(-3, 4):
                i, j = int((x - ox) / step) + di, int((y - oy) / step) + dj
                if not (0 <= i < n and 0 <= j < n) or wall[j * n + i] or hi[j * n + i] == NONE:
                    return False
        return True
    ranked = sorted(roads, key=lambda t: math.hypot(sum(p[0] for p in t) / 3 - centre[0], sum(p[1] for p in t) / 3 - centre[1]))
    for tri in ranked:
        mx, my = sum(p[0] for p in tri) / 3, sum(p[1] for p in tri) / 3
        if not clear(mx, my):
            continue
        edges = [(tri[i], tri[(i + 1) % 3]) for i in range(3)]
        a, b = max(edges, key=lambda e: math.hypot(e[1][0] - e[0][0], e[1][1] - e[0][1]))
        return mx, my, sum(p[2] for p in tri) / 3, math.atan2(b[1] - a[1], b[0] - a[0])
    raise ValueError('no clear road triangle in the region')


def place_start(point, grid):
    """Explicit start (game X, Y): nearest cell with ground and no wall,
    heading along the longest wall-free ray of 16 directions."""
    n, ox, oy, step, lo, hi, wall = grid
    def free(i, j):
        return 0 <= i < n and 0 <= j < n and hi[j * n + i] != NONE and not wall[j * n + i]
    ci, cj = int((point[0] - ox) / step), int((point[1] - oy) / step)
    best = None
    for r in range(0, 40):
        for j in range(cj - r, cj + r + 1):
            for i in range(ci - r, ci + r + 1):
                if max(abs(i - ci), abs(j - cj)) == r and all(free(i + a, j + b) for a in (-2, 0, 2) for b in (-2, 0, 2)):
                    d = (i - ci) ** 2 + (j - cj) ** 2
                    if best is None or d < best[0]:
                        best = (d, i, j)
        if best:
            break
    if not best:
        raise ValueError('no free ground near the requested start')
    _, i, j = best
    z = hi[j * n + i] / 1000
    def ray(a):
        for t in range(1, 200):
            if not free(int(i + math.cos(a) * t), int(j + math.sin(a) * t)):
                return t
        return 200
    heading = max((k * math.pi / 8 for k in range(16)), key=ray)
    return ox + (i + .5) * step, oy + (j + .5) * step, z, heading


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--centre', type=float, nargs=2, default=(1700.0, -800.0), help='game X Y (default: airport)')
    ap.add_argument('--radius', type=float, default=320.0)
    ap.add_argument('--cell', type=float, default=64.0)
    ap.add_argument('--split', type=float, default=12.0, help='max triangle edge, m')
    ap.add_argument('--ground-split', type=float, default=4.0, help='max edge on drivable surfaces, m')
    ap.add_argument('--start', type=float, nargs=2, default=(1715.0, -830.0),
                    help='explicit start, game X Y (default: airport parking lot)')
    ap.add_argument('--map-png', type=Path)
    args = ap.parse_args()
    stream, instances, tris, ground, walls, roads, missing = build(args.game, args.centre, args.radius, args.cell, args.split, args.ground_split)
    print(len(instances), 'instances,', missing, 'missing models,', sum(len(t) for t in tris.values()), 'triangles after split')
    tex_sources = texture_index(stream, args.game)
    texdata = {}
    for key in sorted({k[2] for k in tris}):
        tpk = tex_sources.get(key)
        t = tpk.get(key) if tpk else None
        if t is None:
            continue
        w, h, pal, idx, vrepeat = to_clut8(t, 64)
        usage, blend = draw_mode(tpk, key)
        mode = F_ADD if blend == 2 else F_BLEND if usage == 2 and blend == 1 else F_ALPHA if usage == 1 else 0
        if not mode:
            # Opaque: on roads (usage 2) alpha is a wet-reflection mask, not
            # coverage. Force it opaque so the GE alpha test keeps every texel.
            pal = bytes(b if i % 4 != 3 else 255 for i, b in enumerate(pal))
        texdata[key] = (w, h, pal, idx, vrepeat, mode)
    # Unresolved keys are light beams/glows without art (HEADLIGHTGLOW,
    # SFX_LIGHT_BEAMA, ...): drop them rather than draw untextured slabs.
    dropped = sorted({f'{k[2]:08x}' for k in tris if k[2] not in texdata})
    tris = {k: v for k, v in tris.items() if k[2] in texdata}
    grid = collision(ground, walls, args.centre, args.radius)
    n, ox, oy, step, lo, hi, wall = grid
    sx, sy, sz, heading = place_start(args.start, grid) if args.start else pick_start(roads, args.centre, grid)
    gx, gy, gz = gu(sx, sy, sz)
    args.out.mkdir(parents=True, exist_ok=True)
    stats = write_world(args.out / 'world.n2wd', tris, texdata, (gx, gy, gz, heading), args.cell)
    # Collision grid in GU axes: i along game X (= GU z), j along game Y (= GU x).
    cm = lambda vs: [-32768 if v == NONE else max(-32767, min(32767, round(v / 10))) for v in vs]   # region tool: s16 cm
    col = struct.pack('<4sI3f', b'N2WC', n, ox, oy, step) + struct.pack(f'<{n*n}h', *cm(lo)) + struct.pack(f'<{n*n}h', *cm(hi)) + bytes(wall)
    (args.out / 'world.n2wc').write_bytes(col)
    report = dict(centre=args.centre, radius=args.radius, start_game=(sx, sy, sz, heading), instances=len(instances),
                  missing_models=missing, dropped_texture_keys=dropped,
                  collision=dict(grid=n, step=step, bytes=len(col)), world=stats)
    (args.out / 'world.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=1))
    if args.map_png:
        img = bytearray(n * n * 4)
        for k in range(n * n):
            v = 0 if hi[k] == NONE else 90 + max(0, min(150, hi[k] // 200))
            img[k*4:k*4+4] = bytes((200, 40, 40, 255)) if wall[k] else bytes((v, v, v, 255))
        flipped = b''.join(bytes(img[(n-1-j)*n*4:(n-j)*n*4]) for j in range(n))
        write_png(args.map_png, n, n, flipped)


if __name__ == '__main__':
    main()
