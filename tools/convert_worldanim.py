#!/usr/bin/env python3
"""The level's animated world (TRACKS/<level>.BUN) -> worldanim[-<level>].n2wa.

What the original moves in the city, as its loaders read it:
- the trees (0x37110 marks the end of one, after its 0x37080 entity
  records: name, tree, model key, animation name, which of the three
  channels it has, parent) and their instances (0x37090: the tree, the
  world matrix, the play settings at +0x0c: flags, speed, the delay before
  the first run and between runs, a loop range; two event hashes at +0x20
  for the shop doors, which stand closed until a shop's scene moves them:
  kind 6, all of them on one set of entities);
- the animations, EAGL banks (0xe34010): per entity one compound with a
  DeltaQ rotation (_q) and F3 scale (_s) and translation (_t) channels on
  the ROOT skeleton's first bone, whose rest is the identity: the pose is
  the entity's place in its instance's frame;
- the texture animations of the stream (0xb0300100 in its sections): a name,
  the frames' count, frames a second, the clock (1 real time, else the
  game's), then the frames' texture keys. Every texture of the set cycles:
  the n-th shows frame (n + now) mod count.

Timing (CWorldAnimCtrl::AdvanceAnimTime, CWorldAnimEntity::Init): the
animation runs at 30 frames a second times its speed; before the first run
it waits delay0 / speed seconds and before each next one delay1 / speed
(the frame stays at the start meanwhile). The pose is interpolated between
frames (a plane at speed 0.02 holds a frame for 1.7 s).

The models (the entities' _1A_ keys) are cut like the city's near geometry
(convert_city.pack_geometry, origin at the model's own), textured with the
city's encoding; their static twins in the stream (the same models at the
rest pose, flag 0x10) are what the city leaves out (convert_world.SKIP).

N2WA v1 (little endian, blobs 16-aligned): header 64 B: 'N2WA', version,
ntex, nmodel, ntree, nent, nanim, then the offsets of the texture index
(ntex x key, off, size: the city's texture blob), the model index (nmodel
x off, size, radius f32, pad), the trees (128 B: game matrix 16 f32 row
vectors, speed, delay0, delay1 f32, flags u32, loop start/end u16, first
entity, entity count u16, length in frames f32 (the run ends there), kind u32, the path's box
min, max f32 x 3 in game space, the instance's id u32, pad), the entities (16 B: model i32, frames,
offset of frames x 32 B: t xyz f32, q xyzw s16 (x 32767), scale xyz f32, in game space; pad) and the
texture animations (16 B: frames, fps f32, clock, offset of frames x 6 u32:
key, blob off, size, mip off, size, levels; the mip levels after the
city's convert_mips.py).
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

import convert_city as cc
import convert_mips as cm
import convert_world as cw
from convert_car import to_clut8
from convert_nis import _blocks, deltaq, elf_data, h33, translations
from n2formats import archived, chunks, unpad
import n2world as nw
import n2source

ROOT = Path(__file__).resolve().parents[1]
KINDS = (('JumboPlane', 1), ('Graffiti', 2), ('DragTrams', 2), ('Spotlight', 3), ('Crane', 4), ('Sail', 5))
WA_DOOR = 6   # a shop's roll-up door (worldanim.h): moved by the shop's scenes only
# m: the longest edge kept (the GE drops a triangle leaving its +-2048 px
# range, one wide and near the camera) for what passes by the street (the
# trains on the viaducts, 10 m and more over it; the pub's sails); what
# stays up in the air (planes, cranes, the beams) is left whole
SPLIT_NEAR = 12.0
NEAR_MODELS = ('XB_TRAIN', 'XB_PUBSAIL')


def bank_channels(d, scalar_translation_names=()):
    """name -> (kind, frames, values): the banks' channels decoded."""
    out = {}
    for t, p, n in chunks(d):
        if t != 0xe34010:
            continue
        D, syms = elf_data(d[p:p + n])
        w = struct.unpack_from('<8I', D, syms['__AnimationBank:::bank'])
        for ptr, no in zip(struct.unpack_from(f'<{w[1]}I', D, w[4]), struct.unpack_from(f'<{w[1]}I', D, w[5])):
            name = D[no:D.index(b'\0', no)].decode()
            top = struct.unpack_from('<8I', D, ptr)
            nch, frames = top[2] & 0xffff, top[2] >> 16
            for c in top[3:3 + nch]:
                cw0 = struct.unpack_from('<4I', D, c)
                kind = cw0[0] & 0xffff
                if kind == 0x0b:
                    per, = struct.unpack_from('<H', D, cw0[1])
                    out[name] = ('q', frames, deltaq(D, cw0[1], cw0[2] & 0xffff, per // 4)[:, 0])
                elif kind == 0x15 and name in scalar_translation_names:
                    if cw0[2]:
                        raise ValueError(f'{name}: keyed frame times')
                    # World entity translation can use scalar F1 channels:
                    # animated components plus constants in one SQT bone.
                    bones = translations(D, [c], frames)
                    if len(bones) != 1:
                        raise ValueError(f'{name}: expected one translation bone')
                    v = next(iter(bones.values()))
                    if not np.isfinite(v).all():
                        raise ValueError(f'{name}: incomplete scalar translation')
                    out[name] = ('v', frames, v)
                elif kind == 0x14:
                    if cw0[2]:
                        raise ValueError(f'{name}: keyed frame times')
                    vals, offs, consts = _blocks(D, c, 3)
                    v = np.full((frames, 3), np.nan)
                    if offs:
                        v[:] = vals[:frames, 0]
                    for o, c3 in consts.items():
                        v[:] = c3
                    out[name] = ('v', frames, v)
    return out


def level_trees(d):
    """The trees, their entities and their instances, as the loaders take them."""
    entities, trees, insts, pending = {}, {}, [], []
    for t, p, n in chunks(d):
        u = unpad(d[p:p + n])
        if t == 0x37080:
            w = struct.unpack_from('<12I', u)
            pending.append(dict(name=w[1], tree=w[2], model=w[3], self_=w[8], anim=w[9], chan=u[0x28],
                                parent=struct.unpack_from('b', u, 0x29)[0],
                                rest=struct.unpack_from('<16f', u, 0x30)))
        elif t == 0x37110:
            key, = struct.unpack_from('<I', u)
            trees[key] = pending
            pending = []
        elif t == 0x37090:
            w = struct.unpack_from('<4I', u)
            speed, d0, d1 = struct.unpack_from('<3f', u, 0x10)
            insts.append(dict(id=w[0], tree=w[1], section=w[2], flags=w[3], speed=speed, d0=d0, d1=d1,
                              loop=struct.unpack_from('<2H', u, 0x1c), events=struct.unpack_from('<2I', u, 0x20),
                              matrix=struct.unpack_from('<16f', u, 0x40)))
    return trees, insts


def anim_names(d):
    """h33 hash -> base name (the channel's name less _q/_s/_t)."""
    names = {}
    for t, p, n in chunks(d):
        if t != 0xe34010:
            continue
        D, syms = elf_data(d[p:p + n])
        w = struct.unpack_from('<8I', D, syms['__AnimationBank:::bank'])
        for no in struct.unpack_from(f'<{w[1]}I', D, w[5]):
            name = D[no:D.index(b'\0', no)].decode()[:-2]
            names[h33(name)] = name
    return names


def quat_rows(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
                     [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
                     [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]])


def tex_flags_of(tpk, key):
    usage, blend = cw.draw_mode(tpk, key)
    return cw.F_ADD if blend == 2 else cw.F_BLEND if usage == 2 and blend == 1 else cw.F_ALPHA if usage == 1 else 0


def tex_blob(tpk, key, flags, limit=64):
    t = tpk.get(key)
    w, h, pal, idx, vrepeat = to_clut8(t, limit)
    if not flags & (cw.F_ALPHA | cw.F_BLEND | cw.F_ADD):
        pal = bytes(b if i % 4 != 3 else 255 for i, b in enumerate(pal))
    return struct.pack('<3If', w, h, flags, vrepeat) + pal + idx, t.name


def mip_levels(blob):
    """The levels convert_mips.write_mips makes for a city texture blob."""
    w, h = struct.unpack_from('<II', blob)
    n = cm.levels_for(w, h)
    pal = np.frombuffer(blob, np.uint8, 1024, 16).reshape(256, 4)
    img = cm.unswizzle(blob[1040:1040 + w * h], w, h)
    out = b''
    for _ in range(n):
        img = cm.halve(img, pal)
        lvl = cm.swizzle(img)
        out += lvl + b'\0' * (-len(lvl) % 16)
    return out, n


def texture_anims(stream):
    """[(name, frames, fps, clock, keys)] of the stream's sections, once each."""
    found = {}
    for name in stream.sections:
        d = stream.section(name)
        for t, p, n in chunks(d):
            if t != 0xb0300100:
                continue
            leaves = {tt: unpad(d[o:o + s]) for tt, o, s in chunks(d, p, p + n) if not tt & 0x80000000}
            recs, keys = leaves.get(0x30300102, b''), leaves.get(0x30300103, b'')
            at = 0
            for r in range(0, len(recs) - 0x33, 0x34):
                nm = recs[r:r + 24].split(b'\0')[0].decode('ascii', 'replace')
                count, fps, clock = struct.unpack_from('<3I', recs, r + 0x1c)
                ks = [struct.unpack_from('<I', keys, at + 16 * i)[0] for i in range(count)]
                at += 16 * count
                found.setdefault(nm, (nm, count, fps, clock, ks))
        stream.cache.clear()
    return list(found.values())


def _encoded_radius(blob):
    """Sphere about the local origin, including emitted GU s16 quantization."""
    count, = struct.unpack_from('<I', blob)
    at, radius = 16, 0.0
    for _ in range(count):
        _, _, nv, ni, ox, oy, oz, scale, _, _, _, _ = struct.unpack_from('<4I8f', blob, at)
        at += 48
        for i in range(nv):
            x, y, z = struct.unpack_from('<3h', blob, at + 12 * i + 6)
            x, y, z = ox + x * scale / 32768, oy + y * scale / 32768, oz + z * scale / 32768
            radius = max(radius, math.sqrt(x * x + y * y + z * z))
        at = (at + nv * 12 + 15) & ~15
        at = (at + ni * 2 + 15) & ~15
    if not math.isfinite(radius):
        raise ValueError('nonfinite encoded model radius')
    return radius


def _door_bounds(paths, matrix, models):
    """Conservative for linear packed T/S and any normalized interpolated Q.

    Local vertices lie in a sphere; max abs scale cannot grow between keys.
    Each instance matrix column maps that sphere onto a world-axis extent.
    Translation centers stay in the convex hull of packed f32 key centers.
    The rounding guard and outward f32 packing avoid shrinking the enclosure.
    """
    lo, hi, radii = np.full(3, np.inf), np.full(3, -np.inf), {}
    axes = np.linalg.norm(matrix[:3, :3], axis=0)
    for T, mi, S in paths:
        T, S = np.asarray(T, dtype=np.float32).astype(float), np.asarray(S, dtype=np.float32).astype(float)
        if T.shape != S.shape or T.ndim != 2 or T.shape[1] != 3 or not np.isfinite(T).all() or not np.isfinite(S).all():
            raise ValueError('invalid door translation/scale bounds input')
        if mi not in radii:
            radii[mi] = max(models[mi][2], _encoded_radius(models[mi][1]))
        extent = radii[mi] * np.max(np.abs(S)) * axes
        centers = np.column_stack((T, np.ones(len(T)))) @ matrix
        lo = np.minimum(lo, np.min(centers[:, :3], axis=0) - extent)
        hi = np.maximum(hi, np.max(centers[:, :3], axis=0) + extent)
    if not np.isfinite(lo).all() or not np.isfinite(hi).all():
        raise ValueError('nonfinite door bounds')
    pad = 32 * np.finfo(np.float32).eps * np.maximum(1, np.maximum(np.abs(lo), np.abs(hi)))
    lo = np.nextafter(np.asarray(lo - pad, dtype=np.float32), np.float32(-np.inf))
    hi = np.nextafter(np.asarray(hi + pad, dtype=np.float32), np.float32(np.inf))
    return lo, hi


def convert(game, track, out_dir, verbose=True):
    d = archived(game, f'TRACKS/{track}.BUN')
    stream = nw.Stream(game, track)
    index = cw.object_index(stream)
    sources = cw.texture_index(stream, game)
    names = anim_names(d)
    trees, insts = level_trees(d)
    # Decode scalar translation only for shop-door trees in this level.
    door_t = {names[e['anim']] + '_t'
              for inst in insts if any(inst['events'])
              for e in trees.get(inst['tree'], ()) if e['anim'] in names}
    chans = bank_channels(d, scalar_translation_names=door_t)
    textures, tex_of = [], {}
    def tex_index(key):
        if key not in tex_of:
            tpk = sources.get(key)
            if tpk is None:
                return None
            blob, _ = tex_blob(tpk, key, tex_flags_of(tpk, key))
            tex_of[key] = len(textures)
            textures.append((key, blob))
        return tex_of[key]
    models, model_of = [], {}
    def model_index(key):
        if key in model_of:
            return model_of[key]
        sec = index.get(key)
        obj = nw.section_objects(stream.section(sec)).get(key) if sec else None
        stream.cache.clear()
        if obj is None:
            model_of[key] = -1
            return -1
        batches, radius = {}, 0.0
        split = SPLIT_NEAR if obj.name.upper().startswith(NEAR_MODELS) else None
        for ids, tk in obj.subs:
            ti = tex_index(tk)
            if ti is None:
                continue
            tpk = sources[tk]
            flags = tex_flags_of(tpk, tk)
            for j in range(0, len(ids), 3):
                tri = []
                for i in ids[j:j + 3]:
                    x, y, z, c, u, v = obj.verts[i]
                    r, g, b = cw.colour(c)
                    tri.append((x, y, z, u, v, r, g, b))
                    radius = max(radius, math.sqrt(x * x + y * y + z * z))
                cut = [tuple(tri)] if split is None or flags & cw.F_ADD else nw.tessellate(tuple(tri), split)
                batches.setdefault((tk, flags), []).extend(cut)
        batches = {k: v for k, v in batches.items() if v}
        blob = bytearray(cc.pack_geometry(batches, -cc.CELL / 2, -cc.CELL / 2))
        n, at = struct.unpack_from('<I', blob, 0)[0], 16
        for _ in range(n):
            tk, flags, nv, ni = struct.unpack_from('<4I', blob, at)
            struct.pack_into('<I', blob, at, tex_of[tk])
            at += 48 + nv * 12
            at += (-at) % 16
            at += ni * 2
            at += (-at) % 16
        model_of[key] = len(models)
        models.append((obj.name, bytes(blob), radius))
        return model_of[key]
    tree_recs, ents, report, skipped = [], [], [], []
    shared = {}   # the shop doors' tree: its entities once, every door's record on them
    for inst in insts:
        if inst['tree'] not in trees:
            continue
        # a shop door (its two event hashes, the shop's triggers): it stands
        # closed until a shop's scene opens or closes it (WA_DOOR)
        door = any(inst['events'])
        M = np.array(inst['matrix']).reshape(4, 4)
        lo, hi, length, kind, label = np.full(3, np.inf), np.full(3, -np.inf), 0, 0, ''
        if door and inst['tree'] in shared:
            first, length, label, paths = shared[inst['tree']]
        else:
            first, paths = len(ents), []
        for e in (trees[inst['tree']] if first == len(ents) else ()):
            if e['self_'] == e['name'] or not e['model'] or e['anim'] not in names:
                continue
            base = names[e['anim']]
            q = chans.get(base + '_q')
            s = chans.get(base + '_s')
            t = chans.get(base + '_t')
            if not (q or s or t):
                # channels of a kind not decoded here (L4RB's drag trams:
                # a type 0 channel under the compound): left out
                skipped.append(base)
                continue
            mi = model_index(e['model'])
            if mi < 0:
                continue
            frames = max(c[1] for c in (q, s, t) if c)
            rest = np.array(e['rest']).reshape(4, 4)
            T = t[2] if t else np.tile(rest[3, :3], (frames, 1))
            S = s[2] if s else np.ones((frames, 3))
            if q:
                Q = q[2]
            else:
                Q = np.tile([0, 0, 0, 1.0], (frames, 1))
            Qn = Q[:frames] / np.linalg.norm(Q[:frames], axis=1, keepdims=True)
            track_ = b''.join(struct.pack('<3f4h3f', *T[f], *np.round(Qn[f] * 32767).astype(int), *S[f])
                              for f in range(frames))
            ents.append((mi, frames, track_))
            paths.append((T[:frames], mi, S[:frames]))
            length = max(length, frames)
            label = base
            kind = next((k for word, k in KINDS if word in base), 0)
        if door:
            shared[inst['tree']] = (first, length, label, paths)
            kind = WA_DOOR
        if not paths:
            continue
        if door:
            lo, hi = _door_bounds(paths, M, models)
        else:
            for T, mi, _ in paths:
                for row in T:
                    p = np.append(row, 1) @ M
                    lo, hi = np.minimum(lo, p[:3] - models[mi][2]), np.maximum(hi, p[:3] + models[mi][2])
        tree_recs.append(struct.pack('<16f3fI4Hf I6fI', *M.flatten(), inst['speed'], inst['d0'], inst['d1'], inst['flags'],
                                     *inst['loop'], first, len(paths), float(length), kind, *lo, *hi, inst['id']).ljust(128, b'\0'))
        period = length / (30 * inst['speed']) + inst['d1'] / inst['speed']
        report.append(dict(anim=label, kind=kind, entities=len(paths), frames=length, speed=inst['speed'],
                           start_s=round(inst['d0'] / inst['speed'], 1), period_s=round(period, 1),
                           at=[round(v) for v in M[3, :3]], box=[[round(v) for v in lo], [round(v) for v in hi]]))
    anims = []
    if track == 'L4RA':
        for nm, count, fps, clock, keys in texture_anims(stream):
            frames = []
            for k in keys:
                tpk = sources.get(k)
                if tpk is None:
                    break
                flags = tex_flags_of(tpk, keys[0]) | cw.scroll_flags(tpk, keys[0])
                blob, tname = tex_blob(tpk, k, flags)
                if cc.wet(sources, keys[0], tname, tex_flags_of(tpk, keys[0])):
                    struct.pack_into('<I', blob, 8, flags | cw.F_WET)
                mips, n = mip_levels(blob)
                frames.append((k, blob, mips, n))
            if len(frames) == count and count > 1:
                anims.append((nm, fps, clock, frames))
                report.append(dict(texture_anim=nm, frames=count, fps=fps, clock=clock))
    # write
    out = bytearray(64)
    def put(b):
        out.extend(bytes(-len(out) % 16))
        o = len(out)
        out.extend(b)
        return o, len(b)
    tex_index_ = b''.join(struct.pack('<3I', k, *put(b)) for k, b in textures)
    model_index_ = b''.join(struct.pack('<2IfI', *put(b), r, 0) for _, b, r in models)
    ent_recs = b''.join(struct.pack('<iII4x', mi, fr, put(tr)[0]) for mi, fr, tr in ents)
    anim_recs = b''
    for nm, fps, clock, frames in anims:
        tab = b''
        for k, blob, mips, n in frames:
            bo, bs = put(blob)
            mo, ms = put(mips) if mips else (0, 0)
            tab += struct.pack('<6I', k, bo, bs, mo, ms, n)
        anim_recs += struct.pack('<IfII', len(frames), float(fps), clock, put(tab)[0])
    offs = [put(x)[0] for x in (tex_index_, model_index_, b''.join(tree_recs), ent_recs, anim_recs)]
    struct.pack_into('<4s6I5I', out, 0, b'N2WA', 1, len(textures), len(models), len(tree_recs), len(ents), len(anims), *offs)
    name = 'worldanim.n2wa' if track == 'L4RA' else f'worldanim-{track}.n2wa'
    path = out_dir / name
    path.write_bytes(out)
    summary = dict(file=str(path), bytes=len(out), textures=len(textures), models=[m[0] for m in models],
                   trees=len(tree_recs), entities=len(ents), texture_anims=len(anims), skipped=len(skipped), items=report)
    if verbose:
        print(json.dumps(summary, indent=1))
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--track', nargs='*', default=['L4RA', 'L4RB', 'L4RD', 'L4RG'])
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for t in args.track:
        convert(args.game, t, args.out)


if __name__ == '__main__':
    sys.exit(main())
