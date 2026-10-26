#!/usr/bin/env python3
"""The original's precomputed visibility (PVS) -> city.n2vz, beside city.n2cy.
Python stdlib only.

NFSU2 splits the city into scenery sections numbered letter * 100 + n
(A1 = 101). Some of them are drivable sections, each with a boundary
polygon on the ground and the list of every section visible from anywhere
inside it, made offline by EA's tool (the "Seeulator"). The game draws the
list of the section the camera is in, and streams the lists of the
section the car is in and of the one it will be in shortly. They live in
L4RA.BUN, chunk 0x80034150 (all little endian on the Xbox):

  0x34151  u32 LOD offset (20: section n + 20 holds n's terrain, roads and
           big shells), u32 count, count x u16 section.
  0x34152  boundaries, 36 + 8 * points bytes each: 8 bytes of list links,
           s16 section, s16 points, f32 x2 box min, f32 x2 box max,
           f32 x2 centre, points x (f32 x, f32 y) (game X Y).
  0x34153  drivable sections, 164 bytes each: 8 bytes of list links, s16
           section, 2 pad, u32 (the boundary, filled at load), u32 count,
           count x u16 visible section (at most 72).
  0x34154  per track (u32 id) its own sections (32 bytes each).
  0x34155  loading sections (76 bytes each: name, drivable sections, extra
           sections).

n2world.drivable_sections reads them (one reader for every tool).

The port bakes every section's instances into 64 m cells, so a section
maps to the cells its instances' boxes touch (convert_city.py's cells hold
whatever falls in them), and a drivable section's list to the union of
those: a cell is visible when any section with something in it is. The
sections 2600-2699 (the horizon's) the game draws from everywhere: their
cells go into every list.

N2VZ v2 (little endian):
  header 32 B: magic, version, i0, j0, width, height (the city archive's
    grid, checked at load), zones, points.
  zones x 32 B: s16 section, u16 points, u32 first point, u32 first cell,
    u32 cells, f32 GU box x0 z0 x1 z1.
  points x (f32 GU x, f32 GU z): the boundaries, game Y X.
  cells: u16 cell number (j * width + i, as the archive's index), each
    zone's sorted, only those within REACH of its boundary (the farthest
    the PSP ever streams: world.c's FAR_WIDE plus a margin).
  (v2) aligned to 4: u32 sections, then zones x (u32 first, u32 count) and
    sections x u16: each zone's PVS list, sorted. The batches of the gated
    sections (convert_city.gated: the LOD ones, which span far) carry their
    section and are drawn only while the camera's zone lists it, near cells
    too, as the original draws.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import struct

import convert_world as cw
import n2world as nw
import n2source

ROOT = Path(__file__).resolve().parents[1]
CELL = 64.0
REACH = 600.0          # m from a zone's boundary box
HORIZON = range(2600, 2700)


def section_cells(stream):
    """{section: {(i, j)}}: the 64 m cells (i along game X, j along game Y)
    the boxes of each section's instances touch, those convert_city bakes
    into the city (no skipped art, no event or free roam barriers)."""
    groups = {k: v for k, v in nw.scenery_groups(stream.track).items() if 'BARRIERS_' in k or k.startswith('FREE_ROAM')}
    grouped = set().union(*groups.values()) if groups else set()
    cells = collections.defaultdict(set)
    for name in stream.sections:
        for inst in nw.section_instances(stream.section(name)):
            if inst.name.upper().startswith(cw.SKIP) or not inst.keys[0] or (inst.section, inst.index) in grouped:
                continue
            for i in range(math.floor(inst.bmin[0] / CELL), math.floor(inst.bmax[0] / CELL) + 1):
                for j in range(math.floor(inst.bmin[1] / CELL), math.floor(inst.bmax[1] / CELL) + 1):
                    cells[inst.section].add((i, j))
        stream.cache.clear()
    return cells


def archive_grid(path):
    with path.open('rb') as f:
        h = f.read(64)
    if h[:4] != b'N2CY':
        raise ValueError(f'{path}: not an N2CY archive')
    i0, j0, w, hh = struct.unpack_from('<iiII', h, 12)
    return i0, j0, w, hh


def build(game, archive):
    stream = nw.Stream(game, 'L4RA')
    lod, bounds, visible = nw.drivable_sections(stream.track)
    cells = section_cells(stream)
    i0, j0, w, h = archive_grid(archive)
    horizon = set().union(*(cells.get(s, set()) for s in HORIZON))
    zones, points, cell_list, lists = [], [], [], []
    for sec in sorted(visible):
        if sec not in bounds:
            continue
        (x0, y0, x1, y1), pts = bounds[sec]
        seen = set(horizon)
        for v in visible[sec]:
            seen |= cells.get(v, set())
        keep = sorted((j - j0) * w + (i - i0) for i, j in seen
                      if 0 <= i - i0 < w and 0 <= j - j0 < h
                      and math.hypot(max(0, x0 - (i + 1) * CELL, i * CELL - x1), max(0, y0 - (j + 1) * CELL, j * CELL - y1)) < REACH)
        # GU x = game Y, GU z = game X
        zones.append(struct.pack('<hHIII4f', sec, len(pts), len(points), len(cell_list), len(keep), y0, x0, y1, x1))
        points += [(y, x) for x, y in pts]
        cell_list += keep
        lists.append(sorted(set(visible[sec])))
    head = struct.pack('<4s7I', b'N2VZ', 2, i0 & 0xffffffff, j0 & 0xffffffff, w, h, len(zones), len(points))
    blob = head + b''.join(zones) + b''.join(struct.pack('<2f', *p) for p in points) + struct.pack(f'<{len(cell_list)}H', *cell_list)
    blob += bytes((-len(blob)) % 4)
    secs, at = [], 0
    blob += struct.pack('<I', sum(len(x) for x in lists))
    for x in lists:
        blob += struct.pack('<2I', at, len(x))
        at += len(x)
        secs += x
    blob += struct.pack(f'<{len(secs)}H', *secs)
    sizes = [struct.unpack_from('<I', z, 12)[0] for z in zones]
    report = {'zones': len(zones), 'lod_offset': lod, 'points': len(points), 'cells': len(cell_list),
              'cells_per_zone': [min(sizes), round(sum(sizes) / len(sizes)), max(sizes)], 'bytes': len(blob),
              'sections_with_cells': len(cells), 'horizon_cells': len(horizon), 'listed': len(secs)}
    return blob, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    blob, report = build(args.game, args.out / 'city.n2cy')
    (args.out / 'city.n2vz').unlink(missing_ok=True)   # a link into another dist: never written through
    (args.out / 'city.n2vz').write_bytes(blob)
    print(json.dumps(report))


if __name__ == '__main__':
    main()
