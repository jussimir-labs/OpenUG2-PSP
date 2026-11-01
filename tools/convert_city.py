#!/usr/bin/env python3
"""Xbox NFSU2 city (whole L4RA) -> streamed PSP archive city.n2cy; a
closed track (--track L4RC...) -> track-<track>.n2cy and its events'
barriers, bar-<id>.n2cy.

Every scenery instance of the city is baked into world space and clipped to
64 m cells. Each cell carries three independently loadable blobs: near
geometry (_1A_ models, ground split for the chase camera), far geometry
(_1Z_/_1B_ models) and collision (two ground layers + per-layer walls on a
1 m grid) from the game's own collision polygons (n2world.track_collision),
not the drawn geometry. Textures are stored once each, CLUT8. Tiles of 4x4
cells are processed in parallel.

Archive N2CY v2 (little endian, blobs 16-byte aligned):
  header 64 B: magic, version, cell size f32, i0, j0 (i32, cell index of
    the grid origin; i along game X, j along game Y), width, height, ntex,
    start GU x y z heading (f32), cell index offset, texture index offset.
  cell index: width*height x (near off,size, far off,size, col off,size).
  texture index: ntex x (key, off, size).
  geometry blob: u32 batches, u32 pad[3]; per batch 48 B header
    (tex index, flags, nv, ni, origin GU xyz f32, position scale f32,
    u offset, v offset, uv scale f32, pad) + nv x 12 B vertices
    (u16 u, u16 v, u16 colour 5650, s16 x y z; position = origin +
    s16/32768*scale, uv = u16/32768*uvscale + offset) + ni x u16.
    ni = 0: the nv vertices are one triangle strip (tools/tristrip.py): on a
    real PSP an indexed vertex costs the GE ~4 times a strip's. Strips
    reorder the triangles (~2 vertices a triangle) but in blended batches,
    whose overlaps depend on their order (~3). --indexed writes lists only.
  collision blob (v2): f32 base height, f32 unit (m, 1/1024 unless the
    cells heights span more than +-32 m), u32 the terrain section's offset
    (0: none, all road), 4 bytes pad, then 64x64 s16 low
    ground, 64x64 s16 high ground (height = base + value * unit), 64x64 u8
    wall bits (bit 0 low layer, bit 1 high, bit 2 middle, bit 3 the second
    middle; the same << 4: the wall rises over the chase camera,
    convert_world.CAM_WALL_TOP), 64x64 s16 middle
    ground (three-level interchanges); -32768 = none; then 64x64 u16 wall
    shapes (convert_world.shape_code: the strip of each wall cell its
    geometry occupies, 0xffff = whole cell). v1 stored absolute
    2 cm steps: a 2 cm step under one side of the car rolls it 0.75 deg, a
    visible shudder as the wheels cross the 1 m cells. Last, in cells with
    more than asphalt, the terrain section (pack_terrain): the game's
    TerrainType of the low and high layers' ground (n2world.track_collision),
    a nibble each through a 16-entry palette.
  texture blob: w, h, flags (draw mode, F_WET: alpha is the wet mask; the
    texture's UV scroll, convert_world.scroll_flags), vrepeat f32, CLUT 1024 B,
    swizzled indices.

Each event puts up its own barriers: their art (the level's scenery groups
BARRIERS_<id> and PLAYER_BARRIERS_<id>, chunk 0x34108 over the override
table 0x34107: n2world.scenery_groups) and their walls (the collision
polygons of the barrier groups of the same names, n2world.barrier_groups;
PLAYER_BARRIERS_ are invisible). The track or the city is baked without
any; bar-<id>.n2cy, the same format, holds the cells an event's barriers
reach: their art (drawn over the track's) and the track's collision with
the barriers' walls (in place of the track's), loaded with the event
(psp-viewer/world.c). The city's free roam has its own per career stage,
bar-free<n>.n2cy (FREE_ROAM art, BARRIERS_CAREER<n> walls).
"""
import argparse
import collections
import hashlib
import json
import math
import re
from multiprocessing import Pool
from pathlib import Path
import struct
import time

import functools

from convert_car import to_clut8
import mac_slots
from city_owners import (OwnedTriangle, door_context, instance_key,
                         owned_triangles, inherit_fan, remove_owned)
import convert_world as cw
import n2world as nw
import n2cy_patch   # the barriers' collision as patches on the track's
from n2cy_bounds import with_bounds   # the loader's per-batch boxes and triangles, at each blob's end
import tristrip
import n2source

ROOT = Path(__file__).resolve().parents[1]
CELL = 64.0
TILE = 4                       # cells per tile side
GRID = 64                      # collision cells per cell side (1 m)

_stream = _index = None
_strips = True
_sheets = True       # mark_sheets on (--no-sheets: the A/B's before)
_instance_owners = {}
_static_door_owners = frozenset()
# Ground under other ground less than this (mm) is never driven on
# (convert_world.collision_grid's room). The closed tracks: 2 m. The rail
# yard drag's level crossing (4211-4213) is a hump 1.54 m over the yard's
# ground at its crest, the ground still under it: over 1.5 m, the grid
# kept both, and a car coming down off the hump onto the lower one met the
# upper one as a solid block (a 50 m/s wall hit, the drag lost).
ROOM, TRACK_ROOM = 1500, 2000
_room = ROOM
_objects = {}
_loaded_sections = collections.OrderedDict()


def _init(game, track='L4RA', strips=True, instance_owners=None, static_door_owners=(), sheets=True):
    global _stream, _index, _strips, _room, _instance_owners, _static_door_owners, _sheets
    _sheets = sheets
    _instance_owners = instance_owners or {}
    _static_door_owners = frozenset(static_door_owners)
    _strips = strips
    _room = ROOM if track == 'L4RA' else TRACK_ROOM
    _stream = nw.Stream(game, track)
    _index = cw.object_index(_stream)


def _object(key):
    """Models parsed per section, keeping the 24 most recent sections.
    A model key can live in several sections (2258 do): evicting a section
    drops only the entries it supplied. It used to drop every key it held,
    including one a newer section had just been loaded for, and that whole
    instance (drawing and collision) vanished from the city depending on
    what the worker had converted before."""
    if key in _objects:
        return _objects[key]
    sec = _index.get(key)
    if sec is None:
        return None
    if sec not in _loaded_sections:
        _loaded_sections[sec] = list(nw.section_objects(_stream.section(sec)).items())
        _stream.cache.clear()
        for k, o in _loaded_sections[sec]:
            _objects.setdefault(k, o)
        while len(_loaded_sections) > 24:
            _, old = _loaded_sections.popitem(last=False)
            for k, o in old:
                if _objects.get(k) is o:
                    del _objects[k]
    else:
        _loaded_sections.move_to_end(sec)
    if key not in _objects:     # held by an evicted section's copy: take this section's
        _objects[key] = dict(_loaded_sections[sec]).get(key)
    return _objects[key]


STACK_MAX = .03   # m: a copy of a triangle at most this far above another is the same floor (drop_stacked_copies)


def drop_stacked_copies(batches):
    """The shops' buildings (XB_PAINTSHOPA, XB_BODYSHOPA... the 7 of them)
    hold their floor twice, the same triangles 1 cm (the PS2's 1.2 cm) one
    over the other, laid on the street's ground that runs on under the
    shop: the upper copy beats the ground in the original's depth buffer.
    Here the lower copy came within 3 mm of the ground and mark_overlays
    made the ground's triangles the decal over it, drawn at the same bias
    as the upper copy, 8 mm over them: the two fought in the 16-bit depth,
    the courtyard's pale wet ground in sawteeth over the shop's dark floor
    as a shop's scene drove the car in (beta issue 60, CC_GRAPHICS). Only
    the highest of such copies is kept (same texture, footprint, mapping
    and winding, every corner the same height over the other), before
    mark_overlays. Returns the number of triangles dropped."""
    def footprint(t):
        k = min(range(3), key=lambda a: (round(t[a][0], 3), round(t[a][1], 3)))
        t = t[k:] + t[:k]   # rotated, not reordered: the winding kept
        return tuple((round(p[0], 3), round(p[1], 3), round(p[3], 4), round(p[4], 4)) for p in t), [p[2] for p in t]
    dropped = 0
    for key, tris in list(batches.items()):
        if key[1] & (cw.F_ALPHA | cw.F_BLEND | cw.F_ADD | cw.F_DECAL):
            continue
        same = collections.defaultdict(list)
        for k, t in enumerate(tris):
            fp, zs = footprint(list(t))
            same[fp].append((zs, k))
        drop = set()
        for copies in same.values():
            if len(copies) < 2:
                continue
            copies.sort(key=lambda c: sum(c[0]))
            for (lo, k), (hi, _) in zip(copies, copies[1:]):
                d = [b - a for a, b in zip(lo, hi)]
                if 0 < min(d) and max(d) <= STACK_MAX and max(d) - min(d) < 1e-3:
                    drop.add(k)
        if drop:
            batches[key] = [t for k, t in enumerate(tris) if k not in drop]
            dropped += len(drop)
    return dropped


def mark_overlays(batches):
    """Road paint, kerb stripes and patches are opaque triangles lying on
    (or a few centimetres above) another surface with a different texture.
    The 16-bit PSP depth buffer cannot separate such pairs, so those on top
    move to a (tex, flags | F_DECAL) batch that the PSP draws after the
    base with a depth bias. Exactly coplanar pairs: the smaller triangle is
    the paint. Returns the number of triangles moved."""
    flat = []
    for key, tris in batches.items():
        tex, flags = key[:2]
        if flags & (cw.F_ALPHA | cw.F_BLEND | cw.F_ADD | cw.F_DECAL):
            continue
        for k, t in enumerate(tris):
            (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = (p[:3] for p in t)
            nx, ny, nz = (y1 - y0) * (z2 - z0) - (z1 - z0) * (y2 - y0), (z1 - z0) * (x2 - x0) - (x1 - x0) * (z2 - z0), (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
            area = math.sqrt(nx * nx + ny * ny + nz * nz)
            if area < 1e-6 or abs(nz) < .9 * area:
                continue
            flat.append((tex, key, k, t, area, (nx, ny, nz)))
    grid = collections.defaultdict(list)
    for e in flat:
        xs, ys = [p[0] for p in e[3]], [p[1] for p in e[3]]
        for gx in range(math.floor(min(xs) / 2), math.floor(max(xs) / 2) + 1):
            for gy in range(math.floor(min(ys) / 2), math.floor(max(ys) / 2) + 1):
                grid[(gx, gy)].append(e)
    def inside(px, py, t):
        (ax, ay), (bx, by), (cx, cy) = ((p[0], p[1]) for p in t)
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-9:
            return False
        w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / d
        w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / d
        return w0 >= -1e-4 and w1 >= -1e-4 and 1 - w0 - w1 >= -1e-4
    def height(px, py, t, n):
        x0, y0, z0 = t[0][:3]
        return z0 - (n[0] * (px - x0) + n[1] * (py - y0)) / n[2]
    moved = set()
    for tex, key, k, t, area, n in flat:
        px, py = sum(p[0] for p in t) / 3, sum(p[1] for p in t) / 3
        pz = height(px, py, t, n)
        for utex, uflags, uk, u, uarea, un in grid[(math.floor(px / 2), math.floor(py / 2))]:
            if utex == tex or not inside(px, py, u):
                continue
            d = pz - height(px, py, u, un)
            if .003 < d < .08 or (abs(d) <= .003 and area < uarea):
                moved.add((key, k))
                break
    for key, tris in list(batches.items()):
        keep = [t for k, t in enumerate(tris) if (key, k) not in moved]
        paint = [t for k, t in enumerate(tris) if (key, k) in moved]
        if paint:
            batches[key] = keep
            batches.setdefault((key[0], key[1] | cw.F_DECAL, *key[2:]), []).extend(paint)
            if not keep:
                del batches[key]
    return len(moved)


SHEET_GAP = 1.0      # m: a surface this near behind a sheet hides its back
SHEET_MIN = 0.01     # m: nearer, coplanar (no side to choose)
SHEET_PARALLEL = .98
# where a sheet must be covered: its corners, the middles of its edges, its
# centre and the points between (a window seen through a wall's opening has
# its corners on the opening's edges, covered, and its middle in the hole)
SHEET_SAMPLES = ((1, 0, 0), (0, 1, 0), (0, 0, 1), (.5, .5, 0), (0, .5, .5), (.5, 0, .5),
                 (1 / 3, 1 / 3, 1 / 3), (2 / 3, 1 / 6, 1 / 6), (1 / 6, 2 / 3, 1 / 6), (1 / 6, 1 / 6, 2 / 3))
SHEET_LAYER = .1     # m: a sheet this near a surface facing its way is a layer over it (a sign on a wall, a
                     # plate on a sign: 2-4 cm), drawn as a decal (F_DECAL, after it, with the depth bias)
SHEET_AREA = 4.0     # m²: a batch's sheets less than this stay drawn both ways
SHEET_EDGE = .05     # m: a vertex this far past the surface's edge still covered (a billboard's back stands 2 cm past its picture)


def mark_sheets(batches):
    """Billboards (an ADS_ picture 0.2-0.5 m in front of its board's back,
    SGN_BACK_001, which faces the other way), barriers and thin walls are
    sheets back to back. The original draws them without culling and its
    24-bit depth keeps the far one behind; the PSP's 16 bits cannot from
    ~50 m on (two surfaces 0.27 m apart differ by one depth step at ~100 m),
    and the one behind shows through in patches that change every frame.
    A sheet whose back faces an opaque surface parallel to it, at most
    SHEET_GAP behind, covering it (its corners, the middles of its edges and
    of itself: SHEET_SAMPLES), is never seen from that
    side in the original (that surface stands in front of it there): its
    triangles are wound to show the other side only and move to a (tex, flags | F_CULL)
    batch, drawn with the back faces culled (a batch's, SHEET_AREA at least).
    A sheet whose cover faces its own way within SHEET_LAYER (a traffic
    sign 2 cm off a wall or another plate: they fight from ~20 m) is in
    front of a surface that culling leaves drawn: it becomes a decal too
    (F_DECAL: drawn after, pulled toward the camera), its back culled so
    that the bias never shows it through the surface. A plate on an
    alpha-tested or blended mesh (a sign on the chain-link fence, which
    blends: 2 cm) becomes a decal without the culling: the fence's holes
    show its back. Upright sheets only (roads and
    decks keep their headlights' and wet passes as they are). Returns the
    number of triangles moved."""
    import numpy as np
    samples = np.array(SHEET_SAMPLES)
    cand, cut = [], []   # cut: an alpha-tested or blended surface (a fence's mesh): a cover for layers only
    for key, tris in batches.items():
        if key[1] & (cw.F_ADD | cw.F_DECAL | cw.F_CULL):
            continue
        cand.extend((key, k) for k in range(len(tris)))
        cut.extend([bool(key[1] & (cw.F_ALPHA | cw.F_BLEND))] * len(tris))
    cut = np.array(cut, bool)
    if len(cand) < 2:
        return 0
    P = np.array([[p[:3] for p in batches[key][k]] for key, k in cand], float)
    N = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    area = np.linalg.norm(N, axis=1)
    ok = area > 1e-6
    N[ok] /= area[ok, None]
    upright = ok & (np.abs(N[:, 2]) < .7)
    idx = np.nonzero(upright)[0]
    if len(idx) < 2:
        return 0
    # Coarse partners: the same upright direction (either way) within
    # ~0.1 rad, the planes within SHEET_GAP + slack, sorted by plane offset.
    # (each group's offsets along its own direction: a big surface a little
    # askew may stand a few metres off at its centre, hence the slack)
    nb = 32
    theta = np.arctan2(N[idx, 1], N[idx, 0]) % np.pi
    tb = np.minimum((theta / (np.pi / nb)).astype(int), nb - 1)
    xy = P[idx, :, :2].mean(1)
    groups = {b: np.nonzero(tb == b)[0] for b in np.unique(tb)}
    moved = {}
    for b, mine in groups.items():
        c = (b + .5) * np.pi / nb
        near_b = np.concatenate([groups.get((b + d) % nb, np.zeros(0, int)) for d in (-1, 0, 1)])
        off = xy[near_b] @ np.array([np.cos(c), np.sin(c)])
        order = np.argsort(off)
        so = off[order]
        order = near_b[order]
        slack = SHEET_GAP + 6.0
        at = xy[mine] @ np.array([np.cos(c), np.sin(c)])
        lo = np.searchsorted(so, at - slack)
        hi = np.searchsorted(so, at + slack)
        for m, a, z in zip(mine, lo, hi):
            if z - a < 2 or cut[idx[m]]:
                continue
            us = idx[order[a:z]]
            t = idx[m]
            us = us[us != t]
            n = N[t]
            dots = N[us] @ n
            us = us[np.abs(dots) > SHEET_PARALLEL]
            if not len(us):
                continue
            U, M = P[us], N[us]
            e0, e1 = U[:, 1] - U[:, 0], U[:, 2] - U[:, 0]
            d00, d01, d11 = (e0 * e0).sum(1), (e0 * e1).sum(1), (e1 * e1).sum(1)
            den = d00 * d11 - d01 * d01
            good = den > 1e-12
            # SHEET_EDGE past each edge, in its barycentric (the height over it)
            area2 = np.sqrt(np.maximum(den, 1e-12))
            t1, t2 = SHEET_EDGE * np.sqrt(d11) / area2, SHEET_EDGE * np.sqrt(d00) / area2
            t0 = SHEET_EDGE * np.linalg.norm(e1 - e0, axis=1) / area2
            side, layer, fence = 0, False, True
            facing = dots[np.abs(dots) > SHEET_PARALLEL] > 0    # each surface the same way as the sheet
            holed = cut[us]
            for v in samples @ P[t]:
                dist = np.einsum('ij,ij->i', U[:, 0] - v, M) / (M @ n)   # along n to the surface's plane
                q = v + dist[:, None] * n - U[:, 0]
                d20, d21 = (q * e0).sum(1), (q * e1).sum(1)
                with np.errstate(divide='ignore', invalid='ignore'):   # (a degenerate surface: good is false)
                    b1 = (d11 * d20 - d01 * d21) / den
                    b2 = (d00 * d21 - d01 * d20) / den
                    cover = good & (np.abs(dist) > SHEET_MIN) & (np.abs(dist) <= SHEET_GAP) & \
                        (b1 >= -t1) & (b2 >= -t2) & (b1 + b2 <= 1 + t0)
                near = cover & facing & (np.abs(dist) <= SHEET_LAYER)
                fence &= bool((near & holed).any())
                cover &= ~holed
                s = (2 if (cover & (dist > 0)).any() else 0) | (1 if (cover & (dist < 0)).any() else 0)
                if side != -1:
                    side = s if s in (1, 2) and side in (0, s) else -1
                layer |= bool((near & ~holed).any())
                if side == -1 and not fence:
                    break
            if side not in (1, 2) and fence:
                # a plate on a fence's mesh: drawn over it (a decal), both
                # ways (from behind, the fence's holes show its back)
                moved[cand[t]] = (False, area[t] / 2, 'decal')
            elif side in (1, 2):
                # wound so that the GE keeps the side away from the surface
                # (main.c's sceGuFrontFace(GU_CW)): its normal, by the right
                # hand in the game's axes, toward the surface
                moved[cand[t]] = (side == 1, area[t] / 2, 'cull+decal' if layer else 'cull')      # turned, m², how
    # a batch's few small ones stay as they are (a draw more for a trim's
    # sliver costs more than it shows), unless they are layers, whose fight
    # starts at a few tens of metres
    sheet_area = collections.Counter()
    for (key, _), (_, a, how) in moved.items():
        sheet_area[key] += SHEET_AREA if 'decal' in how else a
    moved = {k: (turn, how) for k, (turn, _, how) in moved.items() if sheet_area[k[0]] >= SHEET_AREA}
    if not moved:
        return 0
    for key, tris in list(batches.items()):
        keep, sheet = [], {'cull': [], 'cull+decal': [], 'decal': []}
        for k, tri in enumerate(tris):
            m = moved.get((key, k))
            if m is None:
                keep.append(tri)
                continue
            flip, how = m
            if flip:
                pts = (tri[0], tri[2], tri[1])
                tri = OwnedTriangle(pts, tri.owner) if isinstance(tri, OwnedTriangle) else pts
            sheet[how].append(tri)
        if any(sheet.values()):
            batches[key] = keep
            for how, part in sheet.items():
                if part:
                    extra = (cw.F_CULL if 'cull' in how else 0) | (cw.F_DECAL if 'decal' in how else 0)
                    batches.setdefault((key[0], key[1] | extra, *key[2:]), []).extend(part)
            if not keep:
                del batches[key]
    return len(moved)


def pack_geometry(batches, cell_x, cell_y):
    """batches: {(tex, flags[, section]): [tri]} with tri = 3 x (x,y,z,u,v,r,g,b)
    in game space. Returns the geometry blob (texture field = key, remapped
    later); a section in the key goes in the batch header's last word (its
    gate: gated(), the PSP draws the batch only from the drivable sections
    whose PVS list holds it), 0 without."""
    out = bytearray(struct.pack('<4I', 0, 0, 0, 0))
    count = 0
    def align():
        out.extend(bytes((-len(out)) % 16))
    # One origin and one power-of-two step for every batch of the cell (and
    # a global 1/128 m lattice): a vertex shared by two batches or two cells
    # lands on exactly the same position, so no pixel cracks open between them.
    heights = [p[2] for tris in batches.values() for t in tris for p in t]
    if not heights:
        return b''
    oy = round((min(heights) + max(heights)) / 2)
    span = max(32.0, max(heights) - oy, oy - min(heights))
    step = 1 / 128
    while span > 32767 * step:
        step *= 2
    origin = (cell_y + CELL / 2, float(oy), cell_x + CELL / 2)     # GU x = game Y, z = game X
    scale = step * 32768
    for (tex, flags, *gate), tris in sorted(batches.items()):
        for first in range(0, len(tris), 20000):
            part = tris[first:first + 20000]
            pts = [p for t in part for p in t]
            uoff = math.floor(min(p[3] for p in pts)); voff = math.floor(min(p[4] for p in pts))
            uvscale = 1.0
            span = max(max(p[3] for p in pts) - uoff, max(p[4] for p in pts) - voff)
            while uvscale < span:
                uvscale *= 2
            if uvscale > 64:          # wildly tiled faces: clamp precision loss
                uvscale = 64.0
            verts, ids, remap = [], [], {}
            for x, y, z, u, v, r, g, b in pts:
                gx, gy, gz = y, z, x
                key = struct.pack('<HHH3h',
                                  max(0, min(65535, int(round((u - uoff) / uvscale * 32768)))),
                                  max(0, min(65535, int(round((v - voff) / uvscale * 32768)))),
                                  (int(r) >> 3) | ((int(g) >> 2) << 5) | ((int(b) >> 3) << 11),
                                  max(-32768, min(32767, int(round((gx - origin[0]) / step)))),
                                  max(-32768, min(32767, int(round((gy - origin[1]) / step)))),
                                  max(-32768, min(32767, int(round((gz - origin[2]) / step)))))
                if key not in remap:
                    remap[key] = len(verts)
                    verts.append(key)
                ids.append(remap[key])
            if len(verts) > 65535:
                raise ValueError('batch exceeds 16-bit indices')
            head = (tex, flags)
            tail = (*origin, scale, uoff, voff, uvscale, gate[0] if gate else 0)
            if not _strips:
                out.extend(struct.pack('<4I7fI', *head, len(verts), len(ids), *tail))
                out.extend(b''.join(verts))
                align()
                out.extend(struct.pack('<' + str(len(ids)) + 'H', *ids))
                align()
                count += 1
                continue
            # Strips: one batch per run of pieces whose joined strip fits the
            # GE's 16-bit vertex count.
            tris = [tuple(ids[k:k + 3]) for k in range(0, len(ids), 3)]
            runs, run, size = [], [], 0
            # Blends keep the triangles' order (overlapping blends depend on
            # it). Elsewhere a strip changes a few pixels a frame either way
            # (~50 of 130560 on a PSP-3000, 2026-10: a triangle's first vertex
            # moves the GE's rounding), reordered or not.
            ordered = flags & (cw.F_BLEND | cw.F_ADD)
            culled = flags & cw.F_CULL      # (its windings kept: mark_sheets)
            for piece in (tristrip.in_order if ordered else tristrip.oriented if culled else tristrip.stripify)(tris):
                grown = size + len(piece) + ((3 if culled else 2) if run else 0)   # (a join_oriented's pad)
                if run and grown > 65535:
                    runs.append(run)
                    run, grown = [], len(piece)
                run.append(piece)
                size = grown
            if run:
                runs.append(run)
            strips = [(tristrip.join_oriented if culled else tristrip.join)(r) for r in runs]
            tristrip.check(tris, strips)
            if culled:
                tristrip.check_oriented(tris, strips)
            for st in strips:
                out.extend(struct.pack('<4I7fI', *head, len(st), 0, *tail))
                out.extend(b''.join(verts[v] for v in st))
                align()
                count += 1
    struct.pack_into('<I', out, 0, count)
    return bytes(out) if count else b''


# Adaptive near-geometry grid. The PSP's GE drops a triangle whose
# projection leaves its +-2048 px range: one wide and near the camera
# (tested on a PSP-3000: a floor quad 60 m wide through the camera vanished,
# the same one 4 m wide drew, clipped). Hence near geometry cut on a 4 m
# grid, which multiplied the city's triangles ~6x. Only what the camera can
# come near needs that: the camera's space is the game's own ground (its
# collision) from REACH_BELOW under it to REACH_ABOVE over it (the race
# intro's camera rises 12 m); a triangle at least 2 x step from all of it
# is cut every step metres (8, 16, 32, 64), a fraction of what the GE
# allows (~9 x the distance). The steps are powers of two times the base,
# so every cut lies on the base grid's planes; reach_level, per 8 m voxel,
# is the largest step's exponent its distance allows.
REACH_VOXEL, REACH_BELOW, REACH_ABOVE, REACH_LEVELS = 8.0, 3.0, 15.0, 4


def reach_levels(ground, base):
    """ground: [(triangle, road, terrain)] (game x, y, z). Returns (levels uint8
    array [x, y, z], x0, y0, z0): per voxel, k such that a cut every
    base * 2**k metres is safe there (0: the base grid)."""
    import numpy as np
    from scipy.ndimage import distance_transform_edt
    V = REACH_VOXEL
    xs = [p[0] for t, *_ in ground for p in t]; ys = [p[1] for t, *_ in ground for p in t]; zs = [p[2] for t, *_ in ground for p in t]
    x0, y0, z0 = math.floor(min(xs) / V) * V - 2 * V, math.floor(min(ys) / V) * V - 2 * V, math.floor(min(zs) / V) * V - 4 * V
    nx, ny = int((max(xs) - x0) / V) + 3, int((max(ys) - y0) / V) + 3
    nz = int((max(zs) + 256 - z0) / V) + 1             # buildings stand far over the highest road
    reach = np.zeros((nx, ny, nz), bool)
    for tri, *_ in ground:
        tx, ty, tz = [p[0] for p in tri], [p[1] for p in tri], [p[2] for p in tri]
        reach[int((min(tx) - x0) // V):int((max(tx) - x0) // V) + 1, int((min(ty) - y0) // V):int((max(ty) - y0) // V) + 1,
              max(0, int((min(tz) - REACH_BELOW - z0) // V)):int((max(tz) + REACH_ABOVE - z0) // V) + 1] = True
    dist = distance_transform_edt(~reach, sampling=V) - V * math.sqrt(3)   # from any point of the voxel
    levels = np.zeros(reach.shape, np.uint8)
    for k in range(1, REACH_LEVELS + 1):
        levels[dist >= 2 * base * 2 ** k] = k
    return levels, x0, y0, z0


def reach_slice(reach, ti, tj):
    """The tile's part of reach_levels' array (its triangles stay in it)."""
    levels, x0, y0, z0 = reach
    V, T = REACH_VOXEL, CELL * TILE
    i0, j0 = max(0, int((ti * T - x0) // V)), max(0, int((tj * T - y0) // V))
    i1, j1 = min(levels.shape[0], int(((ti + 1) * T - x0) // V) + 1), min(levels.shape[1], int(((tj + 1) * T - y0) // V) + 1)
    return levels[i0:max(i0, i1), j0:max(j0, j1)].copy(), x0 + i0 * V, y0 + j0 * V, z0


def tri_level(sl, xs, ys, zs):
    """The step exponent for a triangle of box xs, ys, zs (0 off the array)."""
    levels, x0, y0, z0 = sl
    V = REACH_VOXEL
    if levels.size == 0:
        return 0
    i0, i1 = int((min(xs) - x0) // V), int((max(xs) - x0) // V) + 1
    j0, j1 = int((min(ys) - y0) // V), int((max(ys) - y0) // V) + 1
    k0, k1 = int((min(zs) - z0) // V), int((max(zs) - z0) // V) + 1
    if i0 < 0 or j0 < 0 or i1 > levels.shape[0] or j1 > levels.shape[1]:
        return 0
    if k0 >= levels.shape[2]:
        return REACH_LEVELS     # over the array's top: 256 m over the highest ground
    k0, k1 = max(0, k0), min(levels.shape[2], k1)
    if k0 >= k1:
        return 0
    return int(levels[i0:i1, j0:j1, k0:k1].min())


def stitch(cells_near, coarse, base):
    """T-junctions out of the tile's near geometry: a coarse piece's edge
    crossing a finer plane where a neighbour has a vertex gets that vertex
    (the neighbour's exact position), the piece then fanned from its centre.
    cells_near: {cell: {key: [triangles]}}; coarse: {(cell, key): set of
    indices} (the pieces cut coarser than base)."""
    Q = 0.004
    seen = {}
    for batches in cells_near.values():
        for tris in batches.values():
            for t in tris:
                for v in t:
                    seen.setdefault((round(v[0] / Q), round(v[1] / Q), round(v[2] / Q)), v)
    def find(p):
        kx, ky, kz = round(p[0] / Q), round(p[1] / Q), round(p[2] / Q)
        for dx in (0, -1, 1):
            for dy in (0, -1, 1):
                for dz in (0, -1, 1):
                    v = seen.get((kx + dx, ky + dy, kz + dz))
                    if v is not None and abs(v[0] - p[0]) < Q and abs(v[1] - p[1]) < Q and abs(v[2] - p[2]) < Q:
                        return v
        return None
    added = 0
    for (c, key), ids in coarse.items():
        tris = cells_near[c][key]
        for n in sorted(ids):
            a, b, cc = tris[n]
            ring, inserted = [], False
            for p, q in ((a, b), (b, cc), (cc, a)):
                ring.append(p)
                pts = []
                for axis, size in ((0, base), (1, base), (2, 2 * base)):
                    lo, hi = sorted((p[axis], q[axis]))
                    k = math.floor(lo / size) + 1
                    while k * size < hi:
                        t = (k * size - p[axis]) / (q[axis] - p[axis])
                        cut = tuple(p[i] + (q[i] - p[i]) * t for i in range(len(p)))
                        v = find(cut)
                        if v is not None and v[:3] != p[:3] and v[:3] != q[:3]:
                            pts.append((t, v[:3] + cut[3:]))   # the neighbour's position, this piece's attributes
                        k += 1
                for t, v in sorted(pts):
                    ring.append(v)
                    inserted = True
            if not inserted:
                continue
            centre = tuple((a[i] + b[i] + cc[i]) / 3 for i in range(len(a)))
            fan = [(centre, ring[i], ring[(i + 1) % len(ring)]) for i in range(len(ring))]
            fan = inherit_fan(fan, tris[n])
            tris[n] = fan[0]
            tris.extend(fan[1:])
            added += len(fan) - 1
    return added


def gated(section):
    """The section an instance's batches are drawn by (its gate), or 0.
    The original draws only the sections the camera's drivable section lists
    (GrandSceneryCullInfo::WhatSectionsShouldWeDraw); the port bakes all of
    them in the 64 m cells, and its PVS by cell (convert_pvs.py) skips only
    far cells none of whose sections is seen. A LOD section (n + 20 and up:
    terrain, roads, big shells) can span a kilometre: C97's plateau, 1.3 m
    over the coast road of the 4024 where the camera's section (D3) does
    not list it, covered the sky and buried the car. Those batches keep
    their section, drawn only where it is listed; the detailed sections
    (n < 20, tight around their streets) and the horizon's (2600-2699, drawn
    from everywhere) stay merged."""
    return section if section % 100 >= 20 and not 2600 <= section < 2700 else 0


def with_rows(blob, rows):
    """A geometry blob with its cell's preculler rows (precull_batches) at
    its end, after the bounds table: rows x 128 B, then 16 bytes "PCRW",
    u32 rows, 0, 0 (psp-viewer/world.c parse_geometry)."""
    if not rows or not blob:
        return blob
    return blob + bytes((-len(blob)) % 16) + b''.join(rows) + struct.pack('<4s3I', b'PCRW', len(rows), 0, 0)


def precull_booboo(game, track):
    """The camera cells where the game leaves its preculler off (the rows
    hid what should show there: TRACKS\\PrecullerBooBooScript.hoo, its
    "BOOBOO: <track> <section> SET|CLR Pos = x, y, z" lines in order;
    LoadPrecullerBooBooScript): 2048 bytes, a bit per 32 m on a 128 x 128
    grid repeating every 4096 m, bit (Y & 0xfe0) << 2 | X >> 5 & 0x7f."""
    out = bytearray(2048)
    try:
        text = nw.archived(game, 'TRACKS\\PrecullerBooBooScript.hoo').decode('latin1')
    except ValueError:
        return bytes(out)
    for name, op, x, y in re.findall(r'BOOBOO:\s+(\S+)\s+\S+\s+(\S+)\s+Pos\s*=\s*([-\d.]+)\s*,\s*([-\d.]+)', text):
        if name.upper() != track.upper():
            continue
        n = (int(float(y)) & 0xfe0) << 2 | (int(float(x)) & 0xffffffff) >> 5 & 0x7f
        if op.upper() == 'SET':
            out[n >> 3] |= 1 << (n & 7)
        elif op.upper() in ('CLR', 'CLEAR'):
            out[n >> 3] &= ~(1 << (n & 7)) & 0xff
    return bytes(out)


def write_precull(booboo, path):
    """N2PC v3 (little endian), beside the archive: magic, version, 2048,
    0; the camera cells where the preculler is off (precull_booboo). The
    rows go with the cells' blobs (with_rows); without this file the PSP
    culls nothing."""
    path.unlink(missing_ok=True)   # a link into another dist: never written through
    path.write_bytes(struct.pack('<4s3I', b'N2PC', 3, len(booboo), 0) + booboo)
    return {'booboo_cells': sum(bin(b).count('1') for b in booboo), 'bytes': 16 + len(booboo)}


def process_tile(task):
    """Bake one tile of TILE x TILE cells. Returns per-cell blobs."""
    (ti, tj), instances, tex_flags, ground_split, split, overlay, (own_ground, own_walls, extra_walls), reach = task
    coarse = collections.defaultdict(set)   # (cell, key): the pieces cut coarser than split (stitch)
    near = collections.defaultdict(lambda: collections.defaultdict(list))
    far = collections.defaultdict(lambda: collections.defaultdict(list))
    ground = collections.defaultdict(list)
    walls = collections.defaultdict(list)
    # Collision is the game's own (n2world.track_collision), not the drawn
    # geometry: its ground, the walls that always stand and, in an
    # overlay, the walls of the event's barrier groups.
    def cells_of(poly):
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        for ci in range(max(ti * TILE, math.floor(min(xs) / CELL)), min(ti * TILE + TILE - 1, math.floor(max(xs) / CELL)) + 1):
            for cj in range(max(tj * TILE, math.floor(min(ys) / CELL)), min(tj * TILE + TILE - 1, math.floor(max(ys) / CELL)) + 1):
                yield ci, cj
    road = collections.defaultdict(list)     # per ground triangle: a road surface (n2world.track_collision)
    terrain = collections.defaultdict(list)  # and its TerrainType
    for tri, is_road, surface in own_ground:
        for c in cells_of(tri):
            ground[c].append(tri)
            road[c].append(is_road)
            terrain[c].append(surface)
    for poly in own_walls:
        for c in cells_of(poly):
            walls[c].append(poly)
    barred = set()           # cells an overlay's barrier walls reach
    for poly in extra_walls:
        for c in cells_of(poly):
            walls[c].append(poly)
            barred.add(c)
    groups = {}   # (cell, model): its instances' preculler rows ANDed (precull_batches)
    for keys, pos, rot, barrier, gate, *row in instances:
        # Keep the PVS section gate and the preculler row. Ownership is
        # parallel metadata; it never becomes a geometry/material key.
        row = row[0] if row and not overlay else None
        owned = bool(_static_door_owners) and not overlay
        owner = _instance_owners[instance_key(keys, pos, rot)] if owned else None
        inst = nw.Instance('', keys, 0, (0, 0, 0), (0, 0, 0), 0, pos, rot)
        base = _object(keys[0])
        if base is None:
            continue
        f = nw.place(inst)
        for lod, key in ((0, keys[0]), (1, keys[2] or keys[1] or keys[0])):
            obj = _object(key) or base
            pts = [f(*v[:3]) for v in obj.verts]
            limit = 24.0     # far geometry; near geometry is cut on the global grid
            target = near if lod == 0 else far
            for ids, tex in obj.subs:
                if tex not in tex_flags:
                    continue      # light beams/glows without art
                for j in range(0, len(ids), 3):
                    tri = []
                    for i in ids[j:j+3]:
                        x, y, z = pts[i]
                        _, _, _, c, u, v = obj.verts[i]
                        tri.append((x, y, z, u, v, (c >> 16) & 255, (c >> 8) & 255, c & 255))
                    xs, ys = [p[0] for p in tri], [p[1] for p in tri]
                    ci0 = max(ti * TILE, math.floor(min(xs) / CELL)); ci1 = min(ti * TILE + TILE - 1, math.floor(max(xs) / CELL))
                    cj0 = max(tj * TILE, math.floor(min(ys) / CELL)); cj1 = min(tj * TILE + TILE - 1, math.floor(max(ys) / CELL))
                    for ci in range(ci0, ci1 + 1):
                        for cj in range(cj0, cj1 + 1):
                            bx, by = ci * CELL, cj * CELL
                            inside = min(xs) >= bx and max(xs) <= bx + CELL and min(ys) >= by and max(ys) <= by + CELL
                            pieces = [tuple(tri)] if inside else nw.clip_square(tuple(tri), bx, by, bx + CELL, by + CELL)
                            for piece in pieces:
                                key = (tex, tex_flags[tex], gate) if gate else (tex, tex_flags[tex])
                                if row is not None:   # by model until its rows are ANDed (precull_batches)
                                    key = (tex, tex_flags[tex], gate, keys[0])
                                    g = groups.get(((ci, cj), keys[0]))
                                    groups[((ci, cj), keys[0])] = row if g is None else bytes(a & b for a, b in zip(g, row))
                                lst = target[(ci, cj)][key]
                                if lod:
                                    produced = nw.tessellate(piece, limit)
                                    lst.extend(owned_triangles(produced, owner) if owned else produced)
                                    continue
                                step = split
                                if reach is not None:
                                    px, py = [p[0] for p in piece], [p[1] for p in piece]
                                    edge = min(min(px) - ti * TILE * CELL, (ti + 1) * TILE * CELL - max(px),
                                               min(py) - tj * TILE * CELL, (tj + 1) * TILE * CELL - max(py))
                                    if edge > .5:   # at a tile's edge: the base grid, as the next tile's (no stitching across)
                                        step = split * 2 ** tri_level(reach, px, py, [p[2] for p in piece])
                                n0 = len(lst)
                                produced = nw.grid_clip(piece, step, 2 * step)
                                lst.extend(owned_triangles(produced, owner) if owned else produced)
                                if step > split:
                                    coarse[((ci, cj), key)].update(range(n0, len(lst)))
    if coarse:
        stitch(near, coarse, split)
    rows = precull_batches(near, far, groups)
    out = []
    for ci in range(ti * TILE, ti * TILE + TILE):
        for cj in range(tj * TILE, tj * TILE + TILE):
            c = (ci, cj)
            if c not in near and c not in ground:
                continue
            if overlay and c not in near and c not in far and c not in barred:
                continue        # an overlay keeps only the cells its barriers reach
            col = b''
            if ground.get(c):
                n, ox, oy, step, lo, hi, wall, mid, shape, mid2, layer_shapes = cw.collision_grid(
                    ground[c], walls.get(c, []), ci * CELL, cj * CELL, GRID, 1.0, True, road[c], True, True, room=_room,
                    want_layer_shapes=True)
                kinds = cw.terrain_layers(ground[c], terrain[c], (lo, hi), ci * CELL, cj * CELL, GRID)
                col = pack_collision(lo, hi, wall, mid, shape, mid2, kinds, layer_shapes)
            if c in near:
                drop_stacked_copies(near[c])
                mark_overlays(near[c])
            if _static_door_owners and not overlay:
                # Classification sees the complete original context. Filtering
                # earlier changes neighbours' stitching and decal flags.
                remove_owned(near.get(c, {}), _static_door_owners, OwnedTriangle)
                remove_owned(far.get(c, {}), _static_door_owners, OwnedTriangle)
            # after the doors left: a shop's door (worldanim) opens on what
            # stands behind it, which keeps its back
            for part in (near, far):
                if c in part and _sheets:
                    mark_sheets(part[c])
            out.append((c, with_rows(with_bounds(pack_geometry(near.get(c, {}), ci * CELL, cj * CELL)), rows.get(c)),
                        with_rows(with_bounds(pack_geometry(far.get(c, {}), ci * CELL, cj * CELL)), rows.get(c)), col,
                        sorted({k[0] for k in near.get(c, {})} | {k[0] for k in far.get(c, {})})))
    return out


PRECULL_REACH = 250.0   # m: the camera cells a cell's rows keep, those it can be drawn from (the far ring)
PRECULL_GROUPS = 8      # rows a cell at most: its models' grouped (the batches split by them)


def precull_window(ci, cj):
    """128 bytes: the camera cells (32 m, the grid repeating every 1024 m)
    within PRECULL_REACH of cell (ci, cj), each at its nearest repeat."""
    out = bytearray(128)
    def gap(c, lo):
        p = c * 32 + 16
        p += round((lo + CELL / 2 - p) / 1024) * 1024
        return max(0.0, abs(p - (lo + CELL / 2)) - (CELL / 2 + 16))
    for cy in range(32):
        for cx in range(32):
            if math.hypot(gap(cx, ci * CELL), gap(cy, cj * CELL)) <= PRECULL_REACH:
                n = cy * 32 + cx
                out[n >> 3] |= 1 << (n & 7)
    return bytes(out)


def precull_batches(near, far, groups):
    """The original's preculler (L4RA's 0x34106 rows: a bit per camera cell
    of 32 m, on a 32 x 32 grid repeating every 1024 m, set where EA's tool
    found the instance hidden; GrandSceneryCullInfo::DoCulling) for the
    port's batches. A cell's instances of one model share their rows ANDed
    (process_tile), kept to the camera cells it can be drawn from; its
    models go into at most PRECULL_GROUPS groups, the biggest (triangles by
    bits) first, each joining the group whose AND keeps the most, a group's
    row the AND of its models'. Along the 4101, 4104 and Outer Ring, of the
    triangles within 180 m: by instance ~17% hide from the spot, by model
    13%, 8 groups a cell 9%, 4 6%, a whole cell 2-3%; split by model, the
    batches were 54% more, each a call of its own, and the frames no
    faster. The batches keyed by model merge by texture again, a group's
    together; its row's index + 1 in the gate's upper half, the rows at the
    end of the cell's blobs (with_rows: loaded and freed with them; the
    whole city's in one table was 1.5 MB). Returns {cell: [rows]}."""
    bits = lambda r: sum(bin(x).count('1') for x in r)
    weight = collections.Counter()
    for batches in (near, far):
        for c, keyed in batches.items():
            for key, tris in keyed.items():
                if len(key) == 4:
                    weight[(c, key[3])] += len(tris)
    rows_of, out = {}, {}
    by_cell = collections.defaultdict(list)
    for (c, model), r in groups.items():
        by_cell[c].append(model)
    for c, models in by_cell.items():
        window = precull_window(*c)
        kept = {m: bytes(a & b for a, b in zip(groups[(c, m)], window)) for m in models}
        kept = {m: r for m, r in kept.items() if any(r)}
        reps, members = [], []
        for m in sorted(kept, key=lambda m: (-weight[(c, m)] * bits(kept[m]), m)):
            if len(reps) < PRECULL_GROUPS:
                reps.append(kept[m]); members.append([m])
                continue
            def gain(j):
                return bits(bytes(a & b for a, b in zip(reps[j], kept[m]))) * (weight[(c, m)] + sum(weight[(c, x)] for x in members[j]))
            j = max(range(len(reps)), key=gain)
            reps[j] = bytes(a & b for a, b in zip(reps[j], kept[m])); members[j].append(m)
        lst = []
        for rep, ms in zip(reps, members):
            if not any(rep):
                continue
            lst.append(rep)
            for m in ms:
                rows_of[(c, m)] = len(lst)
        if lst:
            out[c] = lst
    for batches in (near, far):
        for c, keyed in batches.items():
            merged = collections.defaultdict(list)
            for key, tris in keyed.items():
                if len(key) == 4:
                    tex, flags, gate, model = key
                    gate |= rows_of.get((c, model), 0) << 16
                    key = (tex, flags, gate) if gate else (tex, flags)
                merged[key].extend(tris)
            batches[c] = merged
    return out


# An event's start and finish lines: the chequered strips painted across
# the road, ZPM_<event>_FW_StartFinGraphic (the course's way) and _BW_ (the
# other way), in the event's BARRIERS_<event> group. The original turns the
# group on with its race and draws each strip only one way: instance flag
# 0x04 the forward one, 0x08 the backward one, the other way's hidden by
# the view's mask (SetupSceneryCullInfo: forward hides 0x08, backward 0x04;
# ScenerySectionHeader::DrawAScenery). The port gates their batches with
# WAY_GATE, drawn only the race's way (psp-viewer/world.c gate_open).
START_FINISH = 'STARTFINGRAPHIC'
WAY_GATE = {0x04: 0xfff4, 0x08: 0xfff8}    # mirrored in psp-viewer/world.c (GATE_FORWARD, GATE_BACKWARD)


def start_finish(inst):
    return inst.name.upper().startswith('ZPM_') and START_FINISH in inst.name.upper()


def instance_gate(inst):
    """gated(), or the way an event's start/finish strip is drawn."""
    if start_finish(inst) and (inst.flags & 0x0c) in WAY_GATE:
        return WAY_GATE[inst.flags & 0x0c]
    return gated(inst.section)


def gather_instances(stream, skip):
    found = {}
    for name in stream.sections:
        for inst in nw.section_instances(stream.section(name)):
            if (inst.name.upper().startswith(skip) and not start_finish(inst)) or not inst.keys[0]:
                continue
            k = (inst.keys[0], tuple(round(v, 2) for v in inst.pos), tuple(round(v, 3) for v in inst.rot))
            kept = found.setdefault(k, inst)
            if kept is not inst:   # the same instance in another section: the game draws each copy by its own preculler row
                kept.precull = None if kept.precull is None or inst.precull is None else bytes(a & b for a, b in zip(kept.precull, inst.precull))
        stream.cache.clear()
    return list(found.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--track', default='L4RA', help='L4RA the city (city.n2cy); L4RB-L4RG the drag, drift, Street X and URL '
                                                   'tracks (track-<name>.n2cy)')
    ap.add_argument('--start', type=float, nargs=2, help='game X Y (default: the airport parking; a track: its first event)')
    ap.add_argument('--bbox', type=float, nargs=4, help='limit to game X0 Y0 X1 Y1 (testing)')
    ap.add_argument('--ground-split', type=float, default=4.0, help='unused, kept for old command lines')
    ap.add_argument('--split', type=float, default=4.0, help='global grid (m) cutting near geometry; z uses twice that')
    ap.add_argument('--adaptive', type=int, default=1, help='1: near geometry the camera cannot come near cut coarser (reach_levels); 0: all on --split')
    ap.add_argument('--jobs', type=int, default=14)
    ap.add_argument('--indexed', action='store_true', help='indexed triangle lists, the format before strips (A/B tests)')
    ap.add_argument('--no-precull', action='store_true', help="without the original's preculler rows: a cell's batches not split by model (on the PSP-3000 the split lists cost more CPU than the preculler hides)")
    ap.add_argument('--no-sheets', action='store_true', help='back-to-back sheets drawn both ways, as before mark_sheets (A/B tests)')
    args = ap.parse_args()
    t0 = time.time()
    stream = nw.Stream(args.game, args.track)
    if args.start is None:
        args.start = (1715.0, -830.0) if args.track == 'L4RA' else first_event_start(args.game, args.track)
    instances = gather_instances(stream, cw.SKIP)
    if args.track == 'L4RA':
        # The knock-over props (the original's smokeables) stand for their
        # scenery instances: smokeables.n2sm draws them, the cells leave
        # them out (tools/convert_smokeables.py).
        props = {s['place'] for s in nw.smokeable_spawners(stream) if s['place']}
        before = len(instances)
        instances = [i for i in instances if (i.keys[0], tuple(round(v, 2) for v in i.pos), tuple(round(v, 3) for v in i.rot)) not in props]
        print(before - len(instances), 'knock-over props left to smokeables.n2sm', flush=True)
    print(len(instances), 'instances', f'{time.time()-t0:.0f}s', flush=True)
    # A closed track puts up each event's own barriers (scenery groups
    # BARRIERS_<id> and PLAYER_BARRIERS_<id>; FREE_ROAM's are for the
    # city): the track is baked without any, and each event's come in an
    # overlay archive, bar-<id>.n2cy, loaded with the event.
    # The city too: its events' barriers (some 6000 instances, flagged 0x200,
    # off until their event) and the free roam's (FREE_ROAM, and per career
    # stage BARRIERS_CAREER<n>) come in overlays, bar-free<stage>.n2cy for
    # the free roam.
    groups = {k: v for k, v in nw.scenery_groups(stream.track).items() if 'BARRIERS_' in k or k.startswith('FREE_ROAM')}
    grouped = set().union(*groups.values()) if groups else set()
    base = [i for i in instances if (i.section, i.index) not in grouped]
    # The animated world's twins (flag 0x10, in no group): the trains, the
    # planes, the cranes' arms, the pub's sails and the spotlights' beams at
    # their rest, which the original's view never draws as scenery (the
    # section's cull skips 0x10 unless a group turns it on); its
    # CWorldAnimEntity draws them moving (worldanim.n2wa), including shop doors.
    base, instance_owners, static_door_owners = door_context(base, args.track)
    # Textures: draw modes decided once, before baking (unresolved keys drop).
    sources = cw.texture_index(stream, args.game)
    # A closed track is the city's map with its own roads closed off and its
    # courses: only the ground around the courses is kept (the whole level
    # would be ~900 MB a track).
    corridor = course_corridor(args.game, args.track) if args.track != 'L4RA' and not args.bbox else None
    # A closed track holds courses far apart (L4RB's three drag strips),
    # each region with its own panorama: the airport strip's, a sheet 9 km
    # wide, passes the other strips' lanes 30 m up (dark slabs over the sky,
    # skyline facades smeared beside the road). The game draws in a region
    # only the sections its drivable sections list (event_sections). Near a
    # course (within COURSE_MARGIN), the art of a section an event there
    # does not draw, drawn instead along another event's course or nowhere,
    # leaves the track: it goes in the overlays (bar-<id>.n2cy) of the
    # events there that draw it. (A section only a region without events
    # draws stays: L4RD's islands of the city, a few walls and lamps.)
    here, moved = {}, collections.defaultdict(lambda: collections.defaultdict(list))
    if corridor is not None:
        polys = event_polylines(args.game, args.track)
        seen, drawn = event_sections(stream.track, polys)
        regional = set().union(*(s for s in seen.values() if s is not None))
        courses = {eid: course_corridor(args.game, args.track, lines) for eid, lines in polys.items() if seen[eid] is not None}
        def events_here(t):
            if t not in here:
                T = CELL * TILE
                here[t] = [(eid, seen[eid]) for eid, near in courses.items() if near(t[0] * T, t[1] * T, t[0] * T + T, t[1] * T + T)]
            return here[t]
    def kept(inst, t, entry):
        """Whether the track's own cells hold an instance's art in tile t;
        if not, the overlays of the events there that draw it do."""
        if corridor is None:
            return True
        foreign = [eid for eid, s in events_here(t) if inst.section not in s]
        if not foreign or (inst.section in drawn and inst.section not in regional):
            return True
        for eid, s in events_here(t):
            if inst.section in s:
                moved[eid][t].append(entry)
        return False
    def tiles_of(insts, barrier, tiles, only=None, route=False):
        for inst in insts:
            x0, y0, x1, y1 = inst.bmin[0], inst.bmin[1], inst.bmax[0], inst.bmax[1]
            if args.bbox and (x1 < args.bbox[0] or x0 > args.bbox[2] or y1 < args.bbox[1] or y0 > args.bbox[3]):
                continue
            if corridor is not None and not corridor(x0, y0, x1, y1):
                continue
            for ti in range(math.floor(x0 / (CELL * TILE)), math.floor(x1 / (CELL * TILE)) + 1):
                for tj in range(math.floor(y0 / (CELL * TILE)), math.floor(y1 / (CELL * TILE)) + 1):
                    # a large instance (ground sheets, the sea) near a course only
                    # brings the tiles that are near it too
                    T = CELL * TILE
                    if corridor is not None and not corridor(ti * T, tj * T, ti * T + T, tj * T + T):
                        continue
                    if only is not None and (ti, tj) not in only:
                        continue
                    entry = (inst.keys, inst.pos, inst.rot, barrier, instance_gate(inst), None if barrier or args.no_precull else inst.precull)
                    if route and not kept(inst, (ti, tj), entry):
                        continue
                    tiles[(ti, tj)].append(entry)
        return tiles
    # Draw-mode flags per texture key (needed by workers for batching).
    tex_flags = {}
    for key, tpk in sources.items():
        usage, blend = cw.draw_mode(tpk, key)
        tex_flags[key] = cw.F_ADD if blend == 2 else cw.F_BLEND if usage == 2 and blend == 1 else cw.F_ALPHA if usage == 1 else 0
    for key in KEYED_BLACK:
        if key in tex_flags:
            tex_flags[key] = cw.F_ALPHA
    # The game's collision polygons by tile: ground, the walls that always
    # stand, and each barrier group's walls
    def by_tile(items, shape=lambda item: item):
        out = collections.defaultdict(list)
        T = CELL * TILE
        for item in items:
            poly = shape(item)
            xs, ys = [p[0] for p in poly], [p[1] for p in poly]
            for ti in range(math.floor(min(xs) / T), math.floor(max(xs) / T) + 1):
                for tj in range(math.floor(min(ys) / T), math.floor(max(ys) / T) + 1):
                    out[(ti, tj)].append(item)
        return out
    col_ground, col_walls = nw.track_collision(stream)
    if args.track != 'L4RA':
        added = bare_course_ground(args.game, args.track, col_ground, base)
        if added:
            print(added[0], 'courses:', added[1], 'drawn road triangles as ground where the collision has none', flush=True)
    own_ground, own_walls = by_tile(col_ground, lambda g: g[0]), by_tile(col_walls.pop('', []))
    reach = reach_levels(col_ground, args.split) if args.adaptive and col_ground else None
    group_walls = {name: by_tile(polys) for name, polys in col_walls.items()}
    cw.object_index(stream)       # built once here, before the workers read its cache
    with Pool(args.jobs, initializer=_init,
              initargs=(args.game, args.track, not args.indexed,
                        instance_owners, static_door_owners, not args.no_sheets)) as pool:
        def bake(tiles, overlay, extra=None):
            extra = extra or {}
            tasks = [(t, inst, tex_flags, args.ground_split, args.split, overlay, (own_ground.get(t, []), own_walls.get(t, []), extra.get(t, [])),
                      reach_slice(reach, *t) if reach else None)
                     for t, inst in sorted(tiles.items())]
            print(len(tasks), 'tiles', flush=True)
            cells = {}
            # one core per tile from the Mac's queue (tools/mac_slots.py)
            for k, result in enumerate(pool.imap_unordered(functools.partial(mac_slots.call, 'convert_city', process_tile), tasks)):
                for c, nb, fb, col, texs in result:
                    cells[c] = (nb, fb, col, texs)
                if k % 20 == 0:
                    print(f'  {k+1}/{len(tasks)} tiles, {len(cells)} cells, {time.time()-t0:.0f}s', flush=True)
            return cells
        cells = bake(tiles_of(base, False, collections.defaultdict(list), route=True), False)
        if moved:
            print('art moved to the events\' overlays:', {eid: sum(map(len, t.values())) for eid, t in sorted(moved.items())}, flush=True)
        name = 'city' if args.track == 'L4RA' else f'track-{args.track}'
        report = write_archive(cells, sources, tex_flags, args.out / f'{name}.n2cy', args.start)
        if args.no_precull:   # no rows in the blobs: no .n2pc either (a stale one would be read for nothing)
            (args.out / f'{name}.n2pc').unlink(missing_ok=True)
        else:
            report['precull'] = write_precull(precull_booboo(args.game, args.track), args.out / f'{name}.n2pc')
        try:   # its textures' mipmaps beside it (.n2mp; numpy: the venv's Python, as tools/build.sh runs it)
            from convert_mips import write_mips
            print(write_mips(args.out / f'{name}.n2cy'))
        except ImportError:
            print('sem numpy: os mipmaps ficam para tools/convert_mips.py (tools/build.sh o roda com o venv)')
        report['seconds'] = round(time.time() - t0)
        (args.out / f'{name}.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=1))
        # Each event's barriers over the track: the cells their art or their
        # collision walls (barrier groups BARRIERS_<id>, PLAYER_BARRIERS_<id>
        # and per career stage BARRIERS_CAREER<n>) reach, drawing the art,
        # colliding with the track's walls and the group's.
        overlays, walled = {}, collections.defaultdict(set)
        for gname in col_walls:
            eid = gname.rsplit('_', 1)[-1]
            if eid.isdigit() and int(eid) >= 4000:
                walled[int(eid)].add(gname)
            elif gname.startswith('BARRIERS_CAREER') and args.track == 'L4RA':
                walled[f'free{gname[15:]}'].add(gname)
        for gname in sorted(groups):
            eid = gname.rsplit('_', 1)[-1]
            if eid.isdigit() and int(eid) >= 4000:
                overlays.setdefault(int(eid), set()).update(groups[gname])
        if args.track == 'L4RA':   # the free roam of each career stage
            roam = set().union(*(v for k, v in groups.items() if k == 'FREE_ROAM'))
            for st in range(6):
                extra = groups.get(f'BARRIERS_CAREER{st}', set()) | (groups.get('FREE_ROAM_STAGE0', set()) | groups.get('FREE_ROAM_STAGE0A', set()) if st == 0 else set())
                overlays[f'free{st}'] = roam | extra
        for eid in list(walled) + list(moved):
            overlays.setdefault(eid, set())
        for eid, members in sorted(overlays.items(), key=lambda kv: str(kv[0])):
            bars = [i for i in instances if (i.section, i.index) in members]
            tiles = tiles_of(bars, True, collections.defaultdict(list))
            for t, entries in moved.get(eid, {}).items():
                tiles[t].extend(entries)
            extra = collections.defaultdict(list)
            for gname in walled.get(eid, ()):
                for t, polys in group_walls[gname].items():
                    if corridor is None or t in tiles or corridor(t[0] * CELL * TILE, t[1] * CELL * TILE, (t[0] + 1) * CELL * TILE, (t[1] + 1) * CELL * TILE):
                        extra[t].extend(polys)
                        tiles.setdefault(t, [])
            ov = bake(tiles, True, extra) if tiles else {}
            if not ov:
                (args.out / f'bar-{eid}.n2cy').unlink(missing_ok=True)
                continue
            r = write_archive(ov, sources, tex_flags, args.out / f'bar-{eid}.n2cy', None)
            # its collision as patches on the track's (tools/n2cy_patch.py):
            # most of each cell's grid is the track's own
            _, size, patched, whole = n2cy_patch.patch_archive(args.out / f'bar-{eid}.n2cy', args.out / f'{name}.n2cy')
            print(f'bar-{eid}.n2cy', len(bars), 'barriers', sum(len(v) for v in extra.values()), 'walls', r['cells'], 'cells', r['bytes'], 'bytes,',
                  size, 'with', patched, 'collision patches', whole, 'whole', flush=True)


# LOD_PALMTREE_RB, the far palms' sprite, is a DXT1 without its 1-bit
# alpha: a palm on opaque black, which drew as dark squares in front of
# the airport's terminal. Its black is cut out (alpha-tested) here.
KEYED_BLACK = {0x543079ca}


def keyed_black(t):
    px = bytearray(t.rgba())
    for i in range(0, len(px), 4):
        if px[i] + px[i+1] + px[i+2] < 24:
            px[i+3] = 0

    class Keyed:
        name, w, h, fmt, palette = t.name, t.w, t.h, t.fmt, None

        def rgba(self):
            return px
    return Keyed()


# Ground the original shows wet: its opaque textures of usage 2 keep in
# their alpha how much of the street's reflection each texel takes (puddles,
# the worn wheel tracks; ~17% on average, ~80% at most). Walls and stairs
# share the usage and the mask, so only ground is picked, by name.
WET_GROUND = ('RDP_', 'RDT_', 'ARC_SIDEWALK', 'ARC_BOARDWALK', 'ARC_CONCRETE', 'ARC_COURTGD', 'ARC_UC_BASKETBALL')


def wet(sources, key, name, flags):
    return not flags and name.startswith(WET_GROUND) and cw.draw_mode(sources[key], key)[0] == 2


def write_archive(cells, sources, tex_flags, path, start):
    """N2CY v2 archive of baked cells (see the module doc); start: game X Y
    of the spawn (its pose from the collision), None for an overlay."""
    used = sorted({t for v in cells.values() for t in v[3]})
    tindex = {k: i for i, k in enumerate(used)}
    texblobs = []
    for key in used:
        t = sources[key].get(key)
        if key in KEYED_BLACK:
            t = keyed_black(t)
        # 64 px; the road paint (RDP_: the lanes' asphalt with their lines,
        # parking bays) whole, up to 512: its lines are 2-3 texels wide in
        # 64 x 512 strips, and any downscale wiped the yellow centre lines
        # off the roads and blurred the bays into pale streaks
        w, h, pal, idx, vrepeat = to_clut8(t, 512 if t.name.startswith('RDP_') else 64)
        flags = tex_flags[key] | (cw.F_WET if wet(sources, key, t.name, tex_flags[key]) else 0)
        if not flags:   # opaque: alpha would only fail the GE's alpha test
            pal = bytes(b if i % 4 != 3 else 255 for i, b in enumerate(pal))
        flags |= cw.scroll_flags(sources[key], key)
        texblobs.append(struct.pack('<3If', w, h, flags, vrepeat) + pal + idx)
    # Remap geometry texture keys -> archive texture indices.
    def remap(blob):
        if not blob:
            return blob
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
    i0 = min(c[0] for c in cells); i1 = max(c[0] for c in cells)
    j0 = min(c[1] for c in cells); j1 = max(c[1] for c in cells)
    width, height = i1 - i0 + 1, j1 - j0 + 1
    sx = sy = sz = heading = 0.0
    if start is not None:
        # Start pose from the start cell's collision.
        sc = (math.floor(start[0] / CELL), math.floor(start[1] / CELL))
        region = {}
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                c = (sc[0] + di, sc[1] + dj)
                if c in cells and cells[c][2]:
                    region[c] = cells[c][2]
        sx, sy, sz, heading = start_from(region, start)
    gx, gy, gz = sy, sz, sx
    header_size = 64
    index_off = header_size
    tex_index_off = index_off + width * height * 24
    data_off = tex_index_off + len(used) * 12
    data_off += (-data_off) % 16
    out = bytearray(data_off)
    index = bytearray(width * height * 24)
    def put(blob):
        if not blob:
            return 0, 0
        out.extend(bytes((-len(out)) % 16))
        off = len(out)
        out.extend(blob)
        return off, len(blob)
    stats = collections.Counter()
    for (ci, cj), (nb, fb, col, texs) in sorted(cells.items()):
        k = (cj - j0) * width + (ci - i0)
        no, ns = put(remap(nb)); fo, fs = put(remap(fb)); co, cs = put(col)
        struct.pack_into('<6I', index, k * 24, no, ns, fo, fs, co, cs)
        stats['near'] += ns; stats['far'] += fs; stats['col'] += cs
    tindex_bytes = bytearray()
    for key, blob in zip(used, texblobs):
        off, size = put(blob)
        tindex_bytes.extend(struct.pack('<3I', key, off, size))
        stats['tex'] += size
    out[index_off:index_off + len(index)] = index
    out[tex_index_off:tex_index_off + len(tindex_bytes)] = tindex_bytes
    struct.pack_into('<4sIf2i3I4f2I', out, 0, b'N2CY', 2, CELL, i0, j0, width, height, len(used),
                     gx, gy, gz, heading, index_off, tex_index_off)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)
    return dict(cells=len(cells), grid=(i0, j0, width, height), textures=len(used), bytes=len(out),
                bytes_by_kind=dict(stats), start_game=(sx, sy, sz, heading), sha256=hashlib.sha256(out).hexdigest())


COURSE_MARGIN = 250.0


def course_corridor(game, track, polys=None):
    """A test (x0, y0, x1, y1) -> whether a game X Y box comes within
    COURSE_MARGIN of a track's courses (their outlines and race road nodes,
    sampled every 50 m); polys: only these (one event's)."""
    pts = []
    for poly in course_polylines(game, track) if polys is None else polys:
        for a, b in zip(poly, poly[1:]):
            n = max(1, int(math.dist(a, b) // 50))
            pts += [(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n) for k in range(n + 1)]
    grid = collections.defaultdict(list)
    G = COURSE_MARGIN
    for x, y in pts:
        grid[(int(x // G), int(y // G))].append((x, y))
    def near(x0, y0, x1, y1):
        for i in range(int(x0 // G) - 1, int(x1 // G) + 2):
            for j in range(int(y0 // G) - 1, int(y1 // G) + 2):
                for x, y in grid.get((i, j), ()):
                    if math.hypot(max(x0 - x, 0, x - x1), max(y0 - y, 0, y - y1)) <= G:
                        return True
        return False
    return near


def course_polylines(game, track):
    """Per course: its race road nodes, then its outline (game X Y)."""
    return [poly for polys in event_polylines(game, track).values() for poly in polys]


def event_polylines(game, track):
    """{event: its course's polylines}: its race road nodes, then its
    outline (game X Y)."""
    from n2formats import archived as arch, chunks as ch, game_hash
    z = (game / 'ZDIR.BIN').read_bytes()
    have = {e[0] for e in struct.iter_unpack('<6I', z)}
    out = {}
    for eid in range(4000, 4800):
        name = f'TRACKS/ROUTES{track}/PATHS{eid}.BIN'
        if game_hash(name) not in have:
            continue
        d = arch(game, name)
        polys = out.setdefault(eid, [])
        for t, p, n in ch(d):
            if t == 0x34148:
                polys.append([struct.unpack_from('<2f', d, p + 24 * k) for k in range(n // 24)])
            if t == 0x3414c:
                for at in range(p, p + n - 271, 272):
                    if struct.unpack_from('<H', d, at)[0] == eid:
                        polys.append([struct.unpack_from('<2f', d, at + 8 + 8 * k) for k in range(d[at + 2])])
    return out


def event_sections(track_bun, polys):
    """({event: the scenery sections the game draws along its course}, the
    sections some drivable section draws): an event's, the union of what the
    drivable sections of the regions its course crosses draw
    (n2world.course_regions); None for a course outside them all (the game
    then draws every section; so does the port)."""
    zone_of, region, drawn = nw.course_regions(track_bun)
    out = {}
    for eid, lines in polys.items():
        regions = set()
        for poly in lines:
            for a, b in zip(poly, poly[1:]):
                if math.dist(a, b) >= 1000:
                    continue      # a jump between runs of race road nodes is no road
                n = max(1, int(math.dist(a, b) // 10))
                for k in range(n + 1):
                    z = zone_of(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n)
                    if z is not None:
                        regions.add(region[z])
        out[eid] = set().union(*(drawn[r] for r in regions)) if regions else None
    return out, set().union(*drawn.values())


BARE_REACH = 20.0      # metres either side of a course whose road has no ground


def bare_course_ground(game, track, col_ground, instances):
    """L4RB's drag strips: its 2 km straights run where the game's
    collision has no ground (scenery and barriers, but no ground polygon
    under the lanes). Along the track's courses (race road nodes and
    outlines), the drawn road within BARE_REACH where the collision has no
    ground becomes road ground, added to col_ground. Returns (courses
    sampled, triangles added) or None."""
    if track != 'L4RB':
        return None
    covered = collections.defaultdict(list)
    for tri, *_ in col_ground:
        xs, ys = [q[0] for q in tri], [q[1] for q in tri]
        for i in range(math.floor(min(xs) / 8), math.floor(max(xs) / 8) + 1):
            for j in range(math.floor(min(ys) / 8), math.floor(max(ys) / 8) + 1):
                covered[(i, j)].append(tri)
    def ground_near(x, y):
        """Heights of the game's ground within some 16-24 m."""
        i, j = math.floor(x / 8), math.floor(y / 8)
        return [q[2] for a in range(i - 2, i + 3) for b in range(j - 2, j + 3) for tri in covered.get((a, b), ()) for q in tri]
    def on_ground(x, y):
        for tri in covered.get((math.floor(x / 8), math.floor(y / 8)), ()):
            (x0, y0, _), (x1, y1, _), (x2, y2, _) = tri
            det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(det) < 1e-9:
                continue
            w0 = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / det
            w1 = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / det
            if min(w0, w1, 1 - w0 - w1) >= -.01:
                return True
        return False
    polys = course_polylines(game, track)
    # race road nodes are listed in runs, a drag's 200-800 m apart; a
    # jump between runs is no road
    segs = [(a, b) for poly in polys for a, b in zip(poly, poly[1:]) if math.dist(a, b) < 1000]
    if not segs:
        return None
    samples = [(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n)
               for a, b in segs for n in [max(1, int(math.dist(a, b) // 5))] for k in range(n + 1)]
    by_bin = collections.defaultdict(list)
    for x, y in samples:
        by_bin[(math.floor(x / 32), math.floor(y / 32))].append((x, y))
    def near(x, y):
        i, j = math.floor(x / 32), math.floor(y / 32)
        return any(math.hypot(x - px, y - py) <= BARE_REACH
                   for a in (i - 1, i, i + 1) for b in (j - 1, j, j + 1) for px, py in by_bin.get((a, b), ()))
    _init(game, track)
    added, found = 0, []
    sx0, sx1 = min(x for x, _ in samples) - BARE_REACH, max(x for x, _ in samples) + BARE_REACH
    sy0, sy1 = min(y for _, y in samples) - BARE_REACH, max(y for _, y in samples) + BARE_REACH
    for inst in instances:
        if inst.bmax[0] < sx0 or inst.bmin[0] > sx1 or inst.bmax[1] < sy0 or inst.bmin[1] > sy1:
            continue
        if not inst.name.upper().startswith('TRN_'):
            continue      # terrain only (runways, roads, grass): not the roofs (XB_SteelMill...)
        if not any(math.hypot(max(inst.bmin[0] - x, 0, x - inst.bmax[0]), max(inst.bmin[1] - y, 0, y - inst.bmax[1])) <= BARE_REACH
                   for x, y in samples):
            continue
        obj = _object(inst.keys[0])
        if obj is None:
            continue
        f = nw.place(inst)
        pts = [f(*v[:3]) for v in obj.verts]
        for ids, tex in obj.subs:
            for j in range(0, len(ids) - 2, 3):
                tri = tuple(pts[i] for i in ids[j:j + 3])
                (ax, ay, az), (bx, by, bz), (cx, cy, cz) = tri
                ux, uy, uz, wx, wy, wz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
                nx, ny, nz = uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx
                length = math.sqrt(nx * nx + ny * ny + nz * nz)
                if length < 1e-6 or abs(nz) / length < .97:
                    continue
                mx, my = (ax + bx + cx) / 3, (ay + by + cy) / 3
                if near(mx, my) and not on_ground(mx, my):
                    # not the hills drawn far above (TRN_HillRidge, 200 m up)
                    zs = ground_near(mx, my)
                    if zs and min(abs((az + bz + cz) / 3 - z) for z in zs) > 2:
                        continue
                    found.append((tri, mx, my, (az + bz + cz) / 3))
    # the lowest drawn surface: not the roofs and signs over the road
    low = {}
    for _, mx, my, z in found:
        k = (math.floor(mx / 4), math.floor(my / 4))
        low[k] = min(low.get(k, z), z)
    for tri, mx, my, z in found:
        if z <= low[(math.floor(mx / 4), math.floor(my / 4))] + 2:
            col_ground.append((tri, True, 1))   # ROAD
            added += 1
    return len(polys), added


def course_boxes(game, track):
    """Game X Y boxes (x0, y0, x1, y1) around each course of a track: its
    outline and race road nodes, COURSE_MARGIN wider."""
    from n2formats import archived as arch, chunks as ch, game_hash
    z = (game / 'ZDIR.BIN').read_bytes()
    have = {e[0] for e in struct.iter_unpack('<6I', z)}
    boxes = []
    for eid in range(4000, 4800):
        name = f'TRACKS/ROUTES{track}/PATHS{eid}.BIN'
        if game_hash(name) not in have:
            continue
        d = arch(game, name)
        pts = []
        for t, p, n in ch(d):
            if t == 0x34148:
                pts += [struct.unpack_from('<2f', d, p + 24 * k) for k in range(n // 24)]
            if t == 0x3414c:
                for at in range(p, p + n - 271, 272):
                    if struct.unpack_from('<H', d, at)[0] == eid:
                        npts = d[at + 2]
                        pts += [struct.unpack_from('<2f', d, at + 8 + 8 * k) for k in range(npts)]
        if pts:
            xs, ys = [q[0] for q in pts], [q[1] for q in pts]
            m = COURSE_MARGIN
            boxes.append((min(xs) - m, min(ys) - m, max(xs) + m, max(ys) + m))
    return boxes


def first_event_start(game, track):
    """Game X Y of the first event's start in a track's catalog (any of its
    ROUTES<track>/PATHS<id>.BIN carries the whole catalog, leaf 0x3414c)."""
    from n2formats import archived as arch, chunks as ch, game_hash
    z = (game / 'ZDIR.BIN').read_bytes()
    have = {e[0] for e in struct.iter_unpack('<6I', z)}
    for eid in range(4000, 4800):
        name = f'TRACKS/ROUTES{track}/PATHS{eid}.BIN'
        if game_hash(name) in have:
            d = arch(game, name)
            cat = [d[p:p+n] for t, p, n in ch(d) if t == 0x3414c][0]
            return struct.unpack_from('<2f', cat, 8)
    raise ValueError(f'{track}: no event paths')


ROAD = 1      # the game's TerrainType of asphalt (the default)


def pack_terrain(kinds):
    """The terrain section of a collision blob from the low and high
    layers' TerrainType per cell (convert_world.terrain_layers): a 16-byte
    palette (code -> TerrainType; code 0 is ROAD) and 64x64 bytes, the low
    layer's code in the low nibble, the high layer's in the high one. b''
    when the cell is all road (the PSP reads a blob without one as road)."""
    lo, hi = kinds
    seen = collections.Counter(v for v in lo + hi if v not in (0, ROAD))
    if not seen:
        return b''
    palette = [ROAD] + [v for v, _ in seen.most_common(15)]   # a 64 m cell never holds more than a handful
    code = {v: i for i, v in enumerate(palette)}
    pack = bytes(code.get(a, 0) | code.get(b, 0) << 4 for a, b in zip(lo, hi))
    return bytes(palette + [ROAD] * (16 - len(palette))) + pack



def pack_layer_shapes(layer_shapes):
    """The layers' own wall shapes (convert_world.collision_grid
    want_layer_shapes): u32 count, then per walled layer whose strip is not
    the cell's, by cell and bit, u16 cell (j*64 + i), u8 layer bit, u8 pad,
    u16 shape. b'' when there is none."""
    if not layer_shapes:
        return b''
    items = sorted(layer_shapes.items())
    return struct.pack('<I', len(items)) + b''.join(struct.pack('<HBxH', k, bit, code) for (k, bit), code in items)


def pack_collision(lo, hi, wall, mid, shape=None, mid2=None, kinds=None, layer_shapes=None):
    """Collision blob v2 from millimetre layers (convert_world.collision_grid),
    with the wall shapes (u16 per cell) after the middle layer and, only in
    cells where four levels stack, the second middle layer after them; the
    terrain section (pack_terrain) after all, its offset in the header's
    u32 at +8 (0: none, all road); then, 4-aligned, the layers' own wall
    shapes (pack_layer_shapes), their offset in the u32 at +12 (0: none).
    A blob with them always has a terrain section, all road if need be:
    its offset is where the PSP's layers end, and a game without the
    layers' shapes (they came with beta issue 75) found that end by the
    blob's size, reading the shapes as a fourth level."""
    if mid2 and all(v == cw.NONE for v in mid2):
        mid2 = None
    if mid2 and not shape:
        shape = [cw.SHAPE_FULL] * (GRID * GRID)
    present = [v for layer in (lo, hi, mid, mid2 or []) for v in layer if v != cw.NONE]
    if not present:
        present = [0]
    base = (min(present) + max(present)) / 2000          # metres
    unit = 1 / 1024
    while max(abs(v / 1000 - base) for v in (min(present), max(present))) > 32000 * unit:
        unit *= 2
    q = lambda vs: [-32768 if v == cw.NONE else max(-32767, min(32767, round((v / 1000 - base) / unit))) for v in vs]
    body = (struct.pack(f'<{GRID*GRID}h', *q(lo)) + struct.pack(f'<{GRID*GRID}h', *q(hi))
            + bytes(wall) + struct.pack(f'<{GRID*GRID}h', *q(mid))
            + (struct.pack(f'<{GRID*GRID}H', *shape) if shape else b'')
            + (struct.pack(f'<{GRID*GRID}h', *q(mid2)) if mid2 else b''))
    terrain = pack_terrain(kinds) if kinds else b''
    layers = pack_layer_shapes(layer_shapes)
    if layers and not terrain:
        terrain = bytes([ROAD] * 16) + bytes(GRID * GRID)
    at = 16 + len(body) + len(terrain)
    pad = (-at) % 4 if layers else 0
    return (struct.pack('<2f2I', base, unit, 16 + len(body) if terrain else 0, at + pad if layers else 0)
            + body + terrain + bytes(pad) + layers)


def unpack_collision(col):
    """-> (lo, hi, wall, mid) in millimetres (convert_world.NONE = none);
    shapes: wall_shapes(col); the fourth level: second_mid(col)."""
    base, unit = struct.unpack_from('<2f', col, 0)
    mm = lambda vs: [cw.NONE if v == -32768 else round((base + v * unit) * 1000) for v in vs]
    o = 16
    lo = mm(struct.unpack_from(f'<{GRID*GRID}h', col, o))
    hi = mm(struct.unpack_from(f'<{GRID*GRID}h', col, o + GRID * GRID * 2))
    wall = col[o + GRID * GRID * 4:o + GRID * GRID * 5]
    mid = mm(struct.unpack_from(f'<{GRID*GRID}h', col, o + GRID * GRID * 5))
    return lo, hi, wall, mid


def second_mid(col):
    """The second middle layer (mm per cell, convert_world.NONE = none), all
    none for blobs without it."""
    o = 16 + GRID * GRID * 9
    if len(col) < o + GRID * GRID * 2:
        return [cw.NONE] * (GRID * GRID)
    base, unit = struct.unpack_from('<2f', col, 0)
    return [cw.NONE if v == -32768 else round((base + v * unit) * 1000) for v in struct.unpack_from(f'<{GRID*GRID}h', col, o)]


def wall_shapes(col):
    """u16 wall shape per cell (convert_world.shape_code), all SHAPE_FULL
    for blobs without them."""
    o = 16 + GRID * GRID * 7
    if len(col) < o + GRID * GRID * 2:
        return [cw.SHAPE_FULL] * (GRID * GRID)
    return list(struct.unpack_from(f'<{GRID*GRID}H', col, o))


def start_from(region, point):
    """Merge the 3x3 start cells into one grid and reuse convert_world's
    explicit-start search (nearest free ground, longest free heading)."""
    i0 = min(c[0] for c in region); j0 = min(c[1] for c in region)
    n = 3 * GRID
    lo, hi, wall = [cw.NONE] * (n * n), [cw.NONE] * (n * n), bytearray(n * n)
    for (ci, cj), col in region.items():
        clo, chi, cw_, _ = unpack_collision(col)
        for y in range(GRID):
            for x in range(GRID):
                k = ((cj - j0) * GRID + y) * n + (ci - i0) * GRID + x
                lo[k], hi[k], wall[k] = clo[y * GRID + x], chi[y * GRID + x], cw_[y * GRID + x] & 3
    return cw.place_start(point, (n, i0 * CELL, j0 * CELL, 1.0, lo, hi, wall))


if __name__ == '__main__':
    main()
