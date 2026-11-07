#!/usr/bin/env python3
"""The frontend's 3D scenes behind the menus -> showroom.n2sc and the
shops' shop-<PLATFORM>.n2sc for the PSP.

FRONTEND\\PLATFORMS\\SHOWROOM.BIN is the street the original's main and
quick race menus stand in: red shopfronts, a hazard-striped floor, the
car on a tread-plate turntable. The shops have their own rooms, in the
same files (FRONTEND\\PLATFORMS\\<the shop's +0x40>: CARLOT01, PERFSHOP01,
PARTSSHOP01 the body shops', PAINTSHOP01 the graphics shops', AUDIOSHOP01
the car specialties' and CRIB01 the garage's), the frontend loads one in
the showroom's place on the way in from the street
(GameFlowLoadingFrontEndPart1); their platform and turntables are the
showroom's. Its solids (chunk 0x80134000, as the
world's: prelit vertices, BGRA colour, UV) and textures (flat TPK) are
the scene; this keeps BACKDROP, BACKDROP_FLOOR and the turntable DISC01
(DISC02/03 are its copies), in GU space (x = game y, y = game z up, z =
game x; the turntable's centre at the origin, the floor at y 0). The
platform carries three turntables 120 degrees apart; the menus' car
stands on the one at game (0, 5.438), as the PS2's RAM places it (the
car's matrix, the cameras of its reflection): the rest of the street is
moved by that much; the other two wells get the turntable too, empty.
PLATFORMBASE, the three striped rings round the turntables a little lower
and wider, is left out: GarageMainScreen::HandleRender draws it only in
the mirrored view (the floor's reflection), never in the screen's own, and
drawn here it stood as a second striped rim under each turntable's (the
"stacked" platform, beta issue 29). When the car arrives the PS2 turns the
platform a third of a turn, bringing its turntable from the right. Triangles
longer than a few metres are split: the GE drops the ones that cross the
near plane; in the shops' rooms only near the car (their backdrops reach
340 m: cut whole into 6 m pieces, the car lot's took 1.9 MB), farther ones
to a length growing with their distance (the showroom's as it was).

The showroom's texture animation (chunk 0xb0300100, as the stream's,
convert_worldanim.texture_anims: a name, frames, frames a second, the
clock, then the frames' keys): SFX_BLINKORANGELIGHT, the hazard lamp on
the ROAD CLOSED barrier, bright, half, dark, half at 10 a second. Its
texture is opaque (AlphaBlendType +0x32 0, as convert_world.draw_mode
reads it), the lamp's orange its own and its vertices': added over the
street, as the other SFX_ glows, it read yellow and never blinked (beta
issue 141).

showroom.n2sc: 'N2SC', u32 version 1, u32 textures, u32 batches; a
texture: u16 w, u16 h, 12 pad, 1024-byte CLUT (RGBA), w*h swizzled T8 indices; a
batch: u16 texture, u16 flags (1 alpha-tested, 2 blended, 3 added; 4 the
turntable's glossy top, over the car's reflection; 8 the platform's
three turntables, which turn when the menus' car arrives; 0x10 an animated
texture: bits 8-11 its frames, the textures from the batch's on, bits 12-15
frames a second), u32 vertices,
then triangles as vertices of 24 bytes: f32 u, v, u32 colour (ABGR), f32
x, y, z.
"""
import argparse
import math
from pathlib import Path
import struct

from n2formats import archived, chunks, unpad, FlatTPK
import n2world
from convert_hud import texture_blob
import n2source

ROOT = Path(__file__).resolve().parents[1]
KEEP = {'BACKDROP': 6.0, 'BACKDROP_FLOOR': 2.5, 'DISC01': 1.5}   # object: split length (m)
DISC_AT = 5.438   # game y of the menus' turntable on the platform (PS2 RAM)
SPIN = {'DISC01'}   # the platform's turntables turn together (flag 8)
# The vertices' alpha and the floor's and turntable's texture alpha are not
# transparency on the Xbox (the turntable's is 0): the gloss of the
# original's reflections. Those draw opaque; the drop shadow (SHD_) goes,
# the PSP draws its own; glows and lights add (their dark texels add
# nothing: blended, the flares over the street were black cards); the rest
# with holes in their alpha (fences, the skyline) are cut out.
GLOSS = {'SHR_SUBDISK', 'SHRM_ROAD'}
PLATFORMS = ('CARLOT01', 'PERFSHOP01', 'PARTSSHOP01', 'PAINTSHOP01', 'AUDIOSHOP01', 'CRIB01')   # the shops' kinds' order (convert_career.SHOP_KIND)
NEAR, REACH = 12.0, 0.5   # m: the camera's farthest from the car; a far triangle's longest edge to its distance


def split_near(tri, limit):
    """n2world.tessellate's split, its limit growing past NEAR from the car
    (the origin): REACH of a piece's distance (its bounding sphere's)."""
    out, stack = [], [tri]
    while stack:
        t = stack.pop()
        c = [sum(v[i] for v in t) / 3 for i in range(3)]
        d = math.hypot(*c) - max(math.dist(v[:3], c) for v in t)
        e = [math.dist(t[0][:3], t[1][:3]), math.dist(t[1][:3], t[2][:3]), math.dist(t[2][:3], t[0][:3])]
        if max(e) <= max(limit, (d - NEAR) * REACH):
            out.append(t)
            continue
        k = e.index(max(e))
        a, b, n = t[k], t[(k + 1) % 3], tuple((p + q) / 2 for p, q in zip(t[k], t[(k + 1) % 3]))
        o = t[(k + 2) % 3]
        stack += [(a, n, o), (n, b, o)]
    return out


def texture_anims(d):
    """{first frame's key: (frames' keys, frames a second)} of the
    file's 0xb0300100 (convert_worldanim.texture_anims' records)."""
    out = {}
    for t, p, n in chunks(d):
        if t != 0xb0300100:
            continue
        leaves = {tt: unpad(d[o:o + s]) for tt, o, s in chunks(d, p, p + n) if not tt & 0x80000000}
        recs, keys = leaves.get(0x30300102, b''), leaves.get(0x30300103, b'')
        at = 0
        for r in range(0, len(recs) - 0x33, 0x34):
            count, fps, _clock = struct.unpack_from('<3I', recs, r + 0x1c)
            ks = [struct.unpack_from('<I', keys, at + 16 * i)[0] for i in range(count)]
            at += 16 * count
            if not 1 < count <= 15 or not 0 < fps <= 15:
                raise SystemExit(f'texture animation {recs[r:r + 24]!r}: {count} frames at {fps} do not fit the batch flags')
            out[ks[0]] = (ks, fps)
    return out


def scene(game, name='SHOWROOM'):
    d = archived(game, f'FRONTEND/PLATFORMS/{name}.BIN')
    split = n2world.tessellate if name == 'SHOWROOM' else split_near
    objs = n2world.section_objects(d)
    tpk = FlatTPK(d)
    anims = texture_anims(d)
    textures, tex_of, batches = [], {}, []
    for o in objs.values():
        if o.name not in KEEP:
            continue
        for ids, key in o.subs:
            if key not in tex_of:
                t = tpk.get(key)
                if t is None:
                    continue
                a = t.rgba()[3::4]
                holes = sum(1 for v in a if v < 16) / len(a)
                usage, blend = tpk.records[key][1][0x31], tpk.records[key][1][0x32]
                glow = (t.name.startswith('SFX_') or t.name == 'LIGHT') and (usage, blend) != (0, 0)   # an opaque SFX_ (the blinking lamp) is not a glow
                anim = 0
                if key in anims:   # its frames right after it
                    ks, fps = anims[key]
                    anim = 0x10 | len(ks) << 8 | fps << 12
                tex_of[key] = (len(textures), t.name, holes, glow, anim)
                textures.append(texture_blob(t, 128))
                for k in anims.get(key, ((),))[0][1:]:
                    f = tpk.get(k)
                    if f is None:
                        raise SystemExit(f'{name}: animation frame {k:#x} missing')
                    textures.append(texture_blob(f, 128))
            ti, tname, holes, glow, anim = tex_of[key]
            # the shops' turntables' tops (GRG_, PRF_, PRTSHP_, AUD_SUBDISK)
            # and floors are the showroom's gloss too. By the object, not
            # the texture alone: the car specialties' marble is also on its
            # BACKDROP, cut out there; taken from it, the floor was
            # alpha-tested away by its gloss and left the room black
            # under the turntable (beta issue 70).
            gloss = tname in GLOSS or tname.endswith('_SUBDISK') or o.name == 'BACKDROP_FLOOR'
            mode = (-1 if tname.startswith('SHD_') else 0 if gloss
                    else 3 if glow else 1 if holes > .05 else 0)
            if tname.endswith('_SUBDISK'):
                mode |= 4   # the turntable's top: glossy, over the car's reflection
            mode |= anim   # an animated texture (beta issue 141)
            if mode < 0:
                continue
            tris = []
            for i in range(0, len(ids), 3):
                tri = []
                for j in ids[i:i + 3]:
                    x, y, z, c, u, v = o.verts[j]
                    if o.name != 'DISC01':
                        y -= DISC_AT   # the street around the turntable the car stands on
                    tri.append((y, z, x, (c >> 16) & 255, (c >> 8) & 255, c & 255, 255, u, v))
                tris += split(tuple(tri), KEEP[o.name])
            spin = 8 if o.name in SPIN else 0
            batches.append((ti, mode | spin, tris))
            if o.name == 'DISC01':   # the other two wells' turntables, empty
                for turn in (120, 240):
                    batches.append((ti, mode | spin, [tuple(well(v, turn) for v in t) for t in tris]))
    return textures, batches


def well(v, degrees):
    """A vertex of the menus' turntable turned about the platform's centre
    (GU x -DISC_AT, z 0: the street is moved by DISC_AT) onto another well."""
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    gx, gy, gz = v[0] + DISC_AT, v[1], v[2]
    return (c * gx + s * gz - DISC_AT, gy, -s * gx + c * gz) + tuple(v[3:])


def write(textures, batches, path):
    out = bytearray(struct.pack('<4s3I', b'N2SC', 1, len(textures), len(batches)))
    for w, h, blob in textures:
        out += struct.pack('<2H12x', w, h) + blob   # 16 bytes: the CLUT and the indices 16-aligned for the GE
    for ti, flags, tris in batches:
        out += struct.pack('<2HI', ti, flags, 3 * len(tris))
        for tri in tris:
            for gx, gy, gz, r, g, b, a, u, v in tri:
                col = min(255, int(a)) << 24 | min(255, int(b)) << 16 | min(255, int(g)) << 8 | min(255, int(r))
                out += struct.pack('<2fI3f', u, v, col, gx, gy, gz)
    path.write_bytes(out)
    return len(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    for name, out in (('SHOWROOM', 'showroom.n2sc'), *((p, f'shop-{p}.n2sc') for p in PLATFORMS)):
        textures, batches = scene(args.game, name)
        size = write(textures, batches, args.out / out)
        print(out, dict(textures=len(textures), batches=len(batches),
                        triangles=sum(len(t) for _, _, t in batches), bytes=size))


if __name__ == '__main__':
    main()
