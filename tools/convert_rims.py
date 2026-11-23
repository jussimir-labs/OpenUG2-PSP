#!/usr/bin/env python3
"""The rim shop's whole library, shared by every car: rims.n2rm.

The game's rims (CARS/WHEELS/GEOMETRY_<BRAND>.BIN: <BRAND>_STYLE<nn>_<rim
inches>_<tyre>, the rim and its tyre, three levels of detail) for the
brands its shop lists (default.xbe 0x142a39): the cars' (5Zigen, Avus, BBS,
Enkei, Gianelle, Konig, Lexani, Lowenhart, Momo, O.Z., NFSU 2 (OASIS),
Racing Hart, Rota, Volk, Work; 15 to 20 inches) and the SUVs' (Donz, Foxx,
Gianelle, Giovanna, Kaizer, O.Z., NFSU 2, StreetSpin (SPINNER), Weld
(WELDWHEEL); 20 to 26 inches); the cars' spinners are StreetSpin's 15 to
20 inches, sold apart (Car Specialties Shop > Spinners). Each
rim is stored once, in a wheel's own space and at its own size (the
second number of its name the tyre's diameter in inches: 0.3048 m of
radius for a 24): its face, the model's y = 0, at 0 and outward (+x in
GU, a left wheel), the rest of it inward. The PSP fits it on each car as
the original's CarRenderInfo::Render: that size on all four wheels, the
face at the record's (cc.wheel_track), and across the axle the record's
tyre width over WheelStandardWidth (0.184 m, the rims' own), mirrored for
the right side. The shop lists a car the rims of its tyre size alone
(n2formats.tyre_size: the COROLLA's 24, 16 to 18 inches). Fitted to the
stock wheel's radius times each axle's record radius, the COROLLA's rear
rims (0.334 m against 0.265 at the front, its physics') came out a
quarter bigger than the front ones (beta #56).

rims.n2rm: 'N2RM', u32 version 3, u32 cars, u32 rims; cars x (char
model[16], u8 1 for an SUV, u8 its tyre size, u16 0, 4 x (f32 x, y, z
(GU, the wheel's outer face on its axle), width scale, side (1 left, -1
right))); rims x (char name[24] (ENKEI_STYLE01_17_25), u8 brand (the
shop's order), u8 style, u8 inches, u8 tyre, u32 offset and bytes of its
detailed pack, of its street pack); then the packs (N2PV v3, one wheel,
flagged F_WHEEL in the rims' slot, wheel 0; a spinner's spinning part,
<family>_SPIN_ at the rim's level, F_SPINNER: it turns on its own).
Version 2 (until 0.2.2): centred, radius 1, each wheel's mount its centre
and the stock wheel's radius.

rimstyles.n2rs, the styles' names as the shop shows them ("GRAVIS I", the
5Zigen's style 02): the parts table's rims (tools/partsdb.py) carry their
family's name hash (attribute 0x10c98090: bin hash of 5ZIGEN_STYLE02) and
their title. 'N2RS', u32 version 1, u32 n, n x (u8 brand (BRANDS' order),
u8 style, u8 pad[2], char title[20]). `--styles` writes it alone.
"""
import argparse
import concurrent.futures
import functools
import os
from pathlib import Path
import re
import struct

import convert_car as cc
import convert_physics
import mac_slots
from n2formats import archived, car_record, lod_family, parts, tyre_size, wheel_faces
import n2source

ROOT = Path(__file__).resolve().parents[1]
# the brands' folders: the cars' shop's in its order, then the SUVs' own
# (psp-viewer/main.c RIM_BRAND_* and its SUV list index these)
BRANDS = (('5ZIGEN', '5Zigen'), ('AVUS', 'Avus'), ('BBS', 'BBS'), ('ENKEI', 'Enkei'), ('GIANELLE', 'Gianelle'),
          ('KONIG', 'Konig'), ('LEXANI', 'Lexani'), ('LOWENHART', 'Lowenhart'), ('MOMO', 'Momo'), ('OZ', 'O.Z.'),
          ('OASIS', 'NFSU 2'), ('RACINGHART', 'Racing Hart'), ('ROTA', 'Rota'), ('VOLK', 'Volk'), ('WORK', 'Work'),
          ('DONZ', 'Donz'), ('FOXX', 'Foxx'), ('GIOVANNA', 'Giovanna'), ('KAIZER', 'Kaizer'), ('SPINNER', 'StreetSpin'),
          ('WELDWHEEL', 'Weld'))
SUVS = ('HUMMER', 'NAVIGATOR', 'ESCALADE')   # the game's big rims (20-26 inches) and their brands
FAMILY = re.compile(r'(.+)_STYLE(\d+)_(\d+)_(\d+)$')
WHEEL_STANDARD_WIDTH = 0.184   # the original's WheelStandardWidth: every rim model's, its face at y 0 to its back at 0.184
F_SPINNER = 0x100000   # psp-viewer/main.c: a spinner's part that turns on its own


def rim_pack(game, textures, lib, family, lod, spin=None):
    """One wheel of a rim family, in its own space and size, as an N2PV v3
    blob; spin: a spinner's spinning part (its levels), in the same space."""
    part = cc.pick_lod(lib[family], lod)
    b = cc.Builder(textures, (128, 128, 128))
    b.slot = cc.SLOT_RIMS
    # wheel_xform's front flip about the face: the model's y = 0 at 0, outward
    xf = lambda x, y, z, nx, ny, nz: (-x, -y, z, -nx, -ny, nz)
    b.add(part, xf, library=True, wheel=0)
    if spin:
        first = len(b.meshes)
        b.add(cc.pick_lod(spin, lod), xf, library=True, wheel=0)
        b.meshes[first:] = [m[:4] + (m[4] | F_SPINNER,) + m[5:] for m in b.meshes[first:]]
    meshes = b.batched()
    texdata = {}
    for ref in {m[2] for m in meshes if m[2] is not None}:
        tex = textures.get(ref)
        if tex is None:
            continue
        texdata[ref] = cc.to_clut8(tex, 128)
    meshes = [m if m[2] in texdata or m[2] is None else (m[0], m[1], None) + m[3:] for m in meshes]
    tmp = Path(os.environ.get('TMPDIR', '/tmp')) / f'rim-{os.getpid()}.n2pv'
    cc.save_pack(tmp, meshes, texdata, 0)
    blob = tmp.read_bytes()
    tmp.unlink()
    return blob


def brand_rims(game, brand):
    """[(name, style, inches, tyre, detailed pack, street pack)] of a brand."""
    if hasattr(os, 'nice'):   # POSIX only; Windows keeps the default priority
        os.nice(15)
    textures = cc.Textures(game, '350Z')   # the rims' art is the shared wheels' (any car's sources hold it)
    lib, spins = {}, {}
    for p in parts(archived(game, f'CARS/WHEELS/GEOMETRY_{brand}.BIN')):
        f = lod_family(p.name)
        if f.endswith('_SPIN_'):   # its three levels under one name (cut at 27 characters)
            spins.setdefault(f[:-len('_SPIN_')], []).append(p)
            continue
        m = FAMILY.match(f)
        if m and m.group(1) == brand:
            lib.setdefault(f, []).append(p)
    out = []
    for f in sorted(lib, key=lambda f: tuple(int(g) for g in FAMILY.match(f).groups()[1:])):
        _, style, inches, tyre = FAMILY.match(f).groups()
        sp = spins.get(f)
        out.append((f, int(style), int(inches), int(tyre), rim_pack(game, textures, lib, f, 0, sp), rim_pack(game, textures, lib, f, 1, sp)))
    return brand, out


def car_mounts(globalb, car):
    """The car's four wheels: GU outer face on the axle (the game's,
    cc.wheel_track), the width scale (the record's tyre width over the
    rims' own), the side."""
    wheels = cc.wheel_track(wheel_faces(globalb, car), car_record(globalb, car))
    return [(y, z, x, w / WHEEL_STANDARD_WIDTH, 1.0 if y > 0 else -1.0)   # Xbox x forward, y lateral, z up -> GU (y, z, x)
            for x, y, z, r, w in wheels[:4]]


STYLE_FAMILY = 0x10c98090   # a rim part's attribute: bin hash of its family (5ZIGEN_STYLE02)


def write_styles(game, out_dir):
    """rimstyles.n2rs: {(brand index, style): title} of the parts table's rims."""
    import n2feng as fe
    from partsdb import Parts
    parts = Parts(game)
    families = {fe.bin_hash(f'{b}_STYLE{s:02d}'): (k, s) for k, (b, _) in enumerate(BRANDS) for s in range(100)}
    titles = {}
    for q in parts.parts:
        if not q['title']:
            continue
        fam = families.get(parts.attributes(q).get(STYLE_FAMILY))
        if fam:
            titles.setdefault(fam, q['title'])
    blob = struct.pack('<4s2I', b'N2RS', 1, len(titles))
    blob += b''.join(struct.pack('<2B2x20s', b, s, t.encode('latin-1')[:19]) for (b, s), t in sorted(titles.items()))
    (out_dir / 'rimstyles.n2rs').write_bytes(blob)
    print(f'rimstyles.n2rs: {len(titles)} styles named:', ', '.join(f'{BRANDS[b][1]} {s:02d} {t}' for (b, s), t in sorted(titles.items())[:6]), '...')
    return titles


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--styles', action='store_true', help='the styles\' names alone (rimstyles.n2rs)')
    ap.add_argument('--jobs', type=int, help='maximum rim conversion workers')
    args = ap.parse_args()
    write_styles(args.game, args.out)
    if args.styles:
        return
    globalb = archived(args.game, 'GLOBAL/GLOBALB.BUN')
    cars = convert_physics.playable(convert_physics.records(globalb))
    mounts = {car: car_mounts(globalb, car) for car in cars}
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        # one core per brand from the Mac's queue (tools/mac_slots.py)
        rims = dict(pool.map(functools.partial(mac_slots.call, 'convert_rims', brand_rims), [args.game] * len(BRANDS), [b for b, _ in BRANDS]))
    table, blobs, at = [], [], 0
    for k, (brand, _) in enumerate(BRANDS):
        for name, style, inches, tyre, detail, street in rims[brand]:
            table.append((name, k, style, inches, tyre, at, len(detail), at + len(detail), len(street)))
            blobs += [detail, street]
            at += len(detail) + len(street)
    head = struct.pack('<4s3I', b'N2RM', 3, len(cars), len(table))
    head += b''.join(struct.pack('<16s2BH', c.encode(), c in SUVS, tyre_size(globalb, c), 0) + b''.join(struct.pack('<5f', *m) for m in mounts[c])
                     for c in cars)
    base = len(head) + len(table) * 44
    base += -base % 16
    head += b''.join(struct.pack('<24s4B4I', n.encode()[:23], b, s, i, t, base + o1, l1, base + o2, l2)
                     for n, b, s, i, t, o1, l1, o2, l2 in table)
    head += bytes(base - len(head))
    (args.out / 'rims.n2rm').write_bytes(head + b''.join(blobs))
    print(f'{len(table)} wheels from {len(BRANDS)} brands, {len(cars)} cars, {(base + at) / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
