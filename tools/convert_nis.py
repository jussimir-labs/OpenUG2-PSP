#!/usr/bin/env python3
"""The race intros' people (NIS\\Scene_IntroNis<NN>_BundleB.bun) -> nis-<NN>.n2ni.

Each intro scene is a bundle of: the crowd's and the flag girl's skinned
models (solids, 56-byte vertices: position, three weights, three u16 bone slots, colour,
UV, normal; each submesh lists the bones its slots stand for, from its
46th byte), their
textures (flat TPK), EAGL skeletons and an animation bank (ELF objects inside
chunks 0xe34009 and 0xe34010), and the scene (0x80037020: the 0x37030
header, then a 0x37040 record per actor: model key, skeleton, animation hash,
place and heading on the grid).

Decoded here:
- skeletons: per bone scale, parent, rest quaternion and translation, the
  inverse of its rest matrix (row vectors);
- animations, type 0x0f compound of a DeltaQ (0x0b: per bone and quaternion
  component a float (min, step, start), then a byte per component per frame,
  q += min + byte * step) and the translations: xyz (0x14) and scalar (0x15)
  channels in blocks of keys and byte deltas, as the Xbox executable's
  decoders (0x240960 and 0x23f4d0 in default.xbe) read them, plus constants;
- actors, and how each one's clock runs (actor_flags: the entity's flags
  and loop range, as the game's CBasicCharacterAnimEntity::Init hands them
  to its CAnimCtrl).

Checked against the PS2: its RAM at the quick race's grid (IntroNis06) holds
each spawned actor's root and bone palette; the decoded rotations match them
within 0.2-1.3 degrees at the matching frame (15 frames a second), the
flag girl's root run within 2 cm (median), and the
scene's frame is the grid's front centre: x to the right, y ahead. Each
actor's local pose (48 bytes a bone, the quaternion first) gives its frame:
at EE frame 5600 the flag girl, the couple (loop range 0-185) and the man
with his arms crossed (looping) are all at 39.55; 250 vsyncs later the flag
girl at 114.5, the couple at 22.0-22.15 (114.55 - 92.5: round their range),
the others at 114.55 round their whole animations (87 and 57 frames).

n2ni: 'N2NI', u32 version, counts (textures, models, skeletons, animations,
actors), then each table (see write()).
"""
import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np

from n2formats import archived, chunks, game_hash, unpad, FlatTPK
from convert_car import to_clut8

ROOT = Path(__file__).resolve().parents[1]
INTROS = (1, 2, 3, 5, 6, 7, 8, 10, 12, 13, 14, 15, 19, 20, 21, 22, 24, 25, 26, 27)   # the scenes the game can choose (tools/convert_intro_nis.py)
FPS = 15
SLOTS = 8          # bone matrices per GE draw


def h33(s):
    """The game's case-sensitive name hash (h * 33 + c from 0xffffffff)."""
    h = 0xffffffff
    for c in s.encode():
        h = (h * 33 + c) & 0xffffffff
    return h


# ---------------------------------------------------------------- ELF (EAGL)
def elf_data(blob):
    """(.data bytes, symbols {name: value}) of an EAGL ELF object."""
    b = blob[8:]          # 8 bytes of 0x11 padding
    if b[:4] != b'\x7fELF':
        raise ValueError('not an EAGL ELF object')
    shoff, = struct.unpack_from('<I', b, 32)
    shes, shnum, shstrndx = struct.unpack_from('<3H', b, 46)
    secs = [struct.unpack_from('<10I', b, shoff + i * shes) for i in range(shnum)]
    def name(sec, off):
        o = sec[4] + off
        return b[o:b.index(b'\0', o)].decode('latin-1')
    names = [name(secs[shstrndx], s[0]) for s in secs]
    data = next(b[s[4]:s[4] + s[5]] for s, n in zip(secs, names) if n == '.data')
    syms = {}
    for s, n in zip(secs, names):
        if s[1] == 2:
            st = secs[s[6]]
            for k in range(s[5] // 16):
                nm, val = struct.unpack_from('<II', b, s[4] + 16 * k)
                syms[name(st, nm)] = val
    return data, syms


def skeleton(blob):
    D, syms = elf_data(blob)
    sk = next(v for k, v in syms.items() if k.startswith('__Skeleton'))
    count, = struct.unpack_from('<I', D, sk + 8)
    bones = []
    for k in range(count):
        a = sk + 16 + 112 * k
        parent, = struct.unpack_from('<i', D, a + 12)
        q = struct.unpack_from('<4f', D, a + 16)
        t = struct.unpack_from('<3f', D, a + 32)
        inv = np.frombuffer(D[a + 48:a + 112], '<f4').reshape(4, 4).copy()
        bones.append(dict(parent=parent, q=q, t=t, inv=inv))
    name = next(k for k in syms if k.startswith('__Skeleton')).split(':::')[1]
    return dict(name=name, hash=h33(name), bones=bones)


def deltas(D, ptr, frames):
    """EAGL delta-compressed values (DeltaQ 0x0b, DeltaF3 0x0a): u16 values,
    u16 bits of each delta (8 the people's, 16 the cars'); per value a float
    (min, step, start); then a delta per value per later frame, v += min +
    delta * step. -> (frames x values) of the summed deltas' parts:
    (min, step, start) and the deltas."""
    per, bits = struct.unpack_from('<HH', D, ptr)
    a = ptr + 4
    co = np.frombuffer(D[a:a + per * 12], '<f4').reshape(per, 3).astype(np.float64)
    b = a + per * 12
    if bits == 16:
        d = np.frombuffer(D[b:b + 2 * per * (frames - 1)], '<u2').reshape(frames - 1, per)
    elif bits == 8:
        d = np.frombuffer(D[b:b + per * (frames - 1)], 'u1').reshape(frames - 1, per)
    else:
        raise ValueError(f'{bits}-bit deltas are not handled')
    out = np.empty((frames, per))
    out[0] = co[:, 2]
    out[1:] = co[:, 2] + np.cumsum(co[:, 0] + d * co[:, 1], axis=0)
    return out


def deltaq(D, ptr, frames, nb):
    per, bits = struct.unpack_from('<HH', D, ptr)
    a = ptr + 4
    co = np.frombuffer(D[a:a + nb * 48], '<f4').reshape(nb, 4, 3)
    if bits == 16:
        d = np.frombuffer(D[a + nb * 48:a + nb * 48 + 2 * per * (frames - 1)], '<u2').reshape(frames - 1, per)[:, :nb * 4]
    else:
        d = np.frombuffer(D[a + nb * 48:a + nb * 48 + per * (frames - 1)], 'u1').reshape(frames - 1, per)[:, :nb * 4]
    d = d.reshape(frames - 1, nb, 4).astype(np.float64)
    q = np.empty((frames, nb, 4))
    cur = co[:, :, 2].astype(np.float64)
    q[0] = cur
    for f in range(1, frames):
        cur = cur + co[:, :, 0] + d[f - 1] * co[:, :, 1]
        q[f] = cur
    return q / np.linalg.norm(q, axis=2, keepdims=True)


def _blocks(D, k, comps):
    """The EAGL F3 (0x14, comps 3) / F1 (0x15, comps 1) channels, as the game
    decodes them: header u16 frames and channels at +0xc, u8 shift and
    constant count at +0x10; per channel a base, a range and per component
    u16 delta offset and step codes; then blocks of 2^shift frames, each a u16
    key per component (base + key * range / 65535) and a byte per component
    per later frame, v += offset + byte * step; then the constants: their u16
    pose offsets and values.  -> (frames x channels x comps, offsets,
    {offset: value} for the constants)."""
    w = struct.unpack_from('<5I', D, k)
    fr, n = w[3] & 0xffff, w[3] >> 16
    shift, nconst = D[k + 16], D[k + 17]
    rec = 8 * comps + 4 * comps
    P = np.array([struct.unpack_from(f'<{2 * comps}f{2 * comps}H', D, k + 20 + rec * c) for c in range(n)],
                 float).reshape(n, 4, comps)
    base, rng, lo, hi = P[:, 0], P[:, 1], P[:, 2], P[:, 3]
    kstep, doff, dstep = rng / 65535, lo * rng / 32768 - rng, hi * rng / 32768 / 255
    per = 1 << shift
    blk = ((per + 1) * n * comps + 1) & ~1
    data = k + 20 + rec * n
    m = n * comps
    out = np.zeros((fr, n, comps))
    for f in range(fr):
        p = data + (f >> shift) * blk
        v = base + np.array(struct.unpack_from(f'<{m}H', D, p), float).reshape(n, comps) * kstep
        for i in range(f & (per - 1)):
            v = v + doff + np.frombuffer(D, np.uint8, m, p + 2 * m + m * i).reshape(n, comps) * dstep
        out[f] = v
    nb = ((fr - 1) >> shift) + 1
    last = (fr - 1) - ((nb - 1) << shift)
    end = data + (nb - 1) * blk + ((2 * m + m * last + 1) & ~1) if n else data
    offs = struct.unpack_from(f'<{n}H', D, w[1]) if n else ()
    coffs = struct.unpack_from(f'<{nconst}H', D, end)
    fl = end + 2 * nconst + (-(end + 2 * nconst) % 4)
    consts = {o: struct.unpack_from(f'<{comps}f', D, fl + 4 * comps * i) for i, o in enumerate(coffs)}
    return out, offs, consts


def translations(D, kids, frames):
    """{bone: frames x 3, nan where the channels leave the rest}: the pose
    offsets are floats into a bone's 12-float SQT, the translation at 8."""
    t = {}
    def put(off, comp, val):
        b, c = divmod(off - 8, 12)
        if 0 <= c < 3:
            t.setdefault(b, np.full((frames, 3), np.nan))[:, c + comp] = val
    for k in kids:
        kind = struct.unpack_from('<I', D, k)[0] & 0xffff
        if kind not in (0x14, 0x15):
            continue
        comps = 3 if kind == 0x14 else 1
        out, offs, consts = _blocks(D, k, comps)
        for c, o in enumerate(offs):
            for j in range(comps):
                put(o, j, out[:frames, c, j])
        for o, v in consts.items():
            for j in range(comps):
                put(o, j, v[j])
    return t


def bank(blob):
    """name -> dict(frames, q (frames, bones, 4), trans {bone: frames x 3})."""
    D, syms = elf_data(blob)
    b = syms['__AnimationBank:::bank']
    w = struct.unpack_from('<8I', D, b)
    count = w[1]
    ptrs = struct.unpack_from(f'<{count}I', D, w[4])
    names = struct.unpack_from(f'<{count}I', D, w[5])
    out = {}
    for p, no in zip(ptrs, names):
        name = D[no:D.index(b'\0', no)].decode()
        top = struct.unpack_from('<8I', D, p)
        if top[0] & 0xffff != 0x0f:
            continue        # scenery / car animations
        nch, frames = top[2] & 0xffff, top[2] >> 16
        rot, kids = None, top[3:3 + nch]
        for c in kids:
            cw = struct.unpack_from('<4I', D, c)
            if cw[0] & 0xffff in (0x14, 0x15) and cw[2]:
                raise ValueError(f'{name}: keyed frame times are not handled')
            if cw[0] & 0xffff == 0x0b:
                per, = struct.unpack_from('<H', D, cw[1])
                nb = per // 4
                rot = deltaq(D, cw[1], cw[2] & 0xffff, nb)
        trans = translations(D, kids, frames)
        if rot is not None:
            out[name] = dict(name=name, hash=h33(name), frames=frames, q=rot, trans=trans)
    return out


# ------------------------------------------------------------------- models
GAME_TO_GU = (1, 2, 0)    # GU x, y, z = game y, z, x


def models(data):
    out = {}
    for tag, p, n in chunks(data):
        if tag != 0x80134010:
            continue
        leaves = {t: data[o:o + s] for t, o, s in chunks(data, p, p + n) if not t & 0x80000000}
        if 0x134b01 not in leaves:
            continue
        hb = unpad(leaves[0x134b00])
        stride, = struct.unpack_from('<H', hb, 10)
        if stride != 56:
            continue
        h = unpad(leaves[0x134011])
        key, = struct.unpack_from('<I', h, 0x10)
        name = h[164:].split(b'\0')[0].decode()
        vb, ib, sb = (unpad(leaves[t]) for t in (0x134b01, 0x134b03, 0x134b02))
        tex = [a for a, _ in struct.iter_unpack('<II', leaves[0x134012])]
        V = []
        for a in range(0, len(vb), 56):
            x, y, z, w0, w1, w2 = struct.unpack_from('<6f', vb, a)
            slots = struct.unpack_from('<3H', vb, a + 24)   # u16 each: as bytes, the second weight's bone was the list's first (the flag girl's zigzag arms)
            u, v = struct.unpack_from('<2f', vb, a + 36)
            nrm = struct.unpack_from('<3f', vb, a + 44)
            V.append(((x, y, z), (w0, w1, w2), tuple(s // 3 for s in slots), (u, v), nrm))
        ids = struct.unpack(f'<{len(ib) // 2}H', ib)
        subs = []
        for a in range(0, len(sb), 64):
            w = struct.unpack_from('<16I', sb, a)
            count, ti, first = w[6], w[7], w[9]
            bones = list(sb[a + 45:a + 61])   # byte 44 is not a bone
            subs.append((ids[first:first + count], tex[ti] if ti < len(tex) else tex[0], bones))
        out[key] = dict(name=name, key=key, verts=V, subs=subs)
    return out


def batches(model):
    """Split each submesh into draws of at most SLOTS bones: (texture key,
    bone list, vertices [(weights[SLOTS] u8, u, v, x, y, z)], indices)."""
    out = []
    for ids, texkey, sublist in model['subs']:
        tris = [ids[i:i + 3] for i in range(0, len(ids), 3)]
        def bones_of(t):
            s = set()
            for i in t:
                pos, w, sl, uv, _ = model['verts'][i]
                for k in range(3):
                    if w[k] > 0:
                        s.add(sublist[sl[k]])
            return s
        pending = tris
        while pending:
            used, take, rest = set(), [], []
            for t in pending:
                b = bones_of(t)
                if len(used | b) <= SLOTS:
                    used |= b
                    take.append(t)
                else:
                    rest.append(t)
            if not take:
                raise ValueError('a triangle needs more than %d bones' % SLOTS)
            order = sorted(used)
            remap, verts, idx = {}, [], []
            for t in take:
                for i in t:
                    if i not in remap:
                        pos, w, sl, uv, nrm = model['verts'][i]
                        ws = [0] * SLOTS
                        for k in range(3):
                            if w[k] > 0:
                                ws[order.index(sublist[sl[k]])] += w[k]
                        tot = sum(ws) or 1
                        wq = [int(round(x / tot * 128)) for x in ws]   # GE 8-bit weights: 128 = 1.0
                        wq[wq.index(max(wq))] += 128 - sum(wq)
                        g = tuple(pos[c] for c in GAME_TO_GU)
                        ln = math.sqrt(sum(c * c for c in nrm)) or 1
                        nq = tuple(max(-127, min(127, round(nrm[c] / ln * 127))) for c in GAME_TO_GU)
                        remap[i] = len(verts)
                        verts.append((wq, uv[0], uv[1], nq) + g)
                    idx.append(remap[i])
            out.append((texkey, order, verts, idx))
            pending = rest
    return out


# ------------------------------------------------------------------- scene
def actors(data):
    out = []
    for t, o, n in chunks(data):
        if t != 0x80037020:
            continue
        for t2, o2, n2 in chunks(data, o, o + n):
            if t2 != 0x37040:
                continue
            b = data[o2:o2 + n2]
            f = struct.unpack_from('<38f', b)
            i = struct.unpack_from('<38I', b)
            # a row-vector 4x4 at f[6]: three axis rows, then the place
            rot = [f[6], f[7], f[8], f[10], f[11], f[12], f[14], f[15], f[16]]
            out.append(dict(rot=rot, pos=f[18:21], model=i[22], anim=i[29], skel=i[30], flags=i[5],
                            loop=(i[34], i[35])))
    return out


def scene_end(data):
    """The scene's end, frames at 30: its header's (0x37030) u32 at 0x28, as
    the game's CAnimScene sets it (its countdown's start at 0x30 and end at
    0x2c). At the end, or once the player has driven 40 m of the race, the
    game stops and unloads it (CAnimManager::IntroNIS_SafeStopWhenFinished):
    the people go."""
    for t, o, n in chunks(data):
        if t == 0x80037020:
            for t2, o2, n2 in chunks(data, o, o + n):
                if t2 == 0x37030:
                    return struct.unpack_from('<I', data, o2 + 0x28)[0]
    raise ValueError('no scene header (0x37030)')


def actor_flags(flags, girl):
    """An actor's n2ni flags from its entity's: as the game's animation
    controller takes them, 0x20 loops and 0x40 loops over the range
    (i[34], i[35]) after playing up to its end (frames at 30, the animation's
    15 a second at half), from 0 with the scene; 0x10 (back and forth) and
    0x80 (a delay, i[32], 0 in these scenes) are not used. girl: the flag
    girl, by her animation's name."""
    if flags & 0x10:
        raise ValueError('back-and-forth animations are not handled')
    return (1 if flags & 0x60 else 0) | (2 if flags & 0x40 else 0) | (4 if girl else 0)


# ------------------------------------------------------------------ writing
def write(path, texs, mods, skels, anims, acts, version=1, end=0):
    """N2NI v1 (v2: the race's ending, its actors 68 bytes, below):
    header 'N2NI', u32 1, u32 counts[5] (textures, models, skeletons,
      animations, actors), u32 offsets[5];
    texture: u16 w, h, pad[12], then 1024-byte CLUT, w*h swizzled indices
      (16-aligned, as the GE wants them); the last is the shadow under the
      actors (CHARACTERSHADOW);
    model: u32 key, u32 draws; draw: u16 texture, u8 bones, u8 pad,
      u8 bone[8], u16 verts, u16 indices, verts x (u8 w[8], f32 u, v, s8 normal[3],
      pad, f32 x, y, z: the GE's skinned vertex, 32 bytes),
      u16 indices (padded to 4);
    skeleton: u32 hash, u32 bones; bone: i32 parent, f32 rest t[3], f32 inv[16];
    animation: u32 hash, u16 frames, u8 bones, u8 fixed, u8 moving, pad[3],
      fixed x (u8 bone, pad[3], f32 xyz) (translations off the rest),
      u8 moving bone[moving] (padded to 4), frames x bones x i16 q[4]
      (padded to 4), frames x moving x f32 xyz;
    actor: u32 model, u32 skeleton, u32 animation, f32 rot[9] (row-vector axes
      in the scene frame), f32 pos[3], u16 start frame, u16 flags (1 loops);
      v2 adds u16 loop from, u16 loop to (frames at 30, twice the
      animation's: it plays from 0 to the second, then over and over from
      the first; flags 2), flags 4 a flag girl, start 0 (v1: the one not
      looping is the flag girl);
    v3 (the grid's scenes): v2, and after the offsets u32 the scene's end
      (frames at 30: the game takes its people away there)."""
    blobs = []
    t = bytearray()
    for w, h, pal, idx in texs:
        t += struct.pack('<2H12x', w, h) + bytes(pal) + bytes(idx)
        t += bytes(-len(t) % 16)
    blobs.append(bytes(t))
    m = bytearray()
    for key, draws in mods:
        m += struct.pack('<2I', key, len(draws))
        for ti, order, verts, idx in draws:
            m += struct.pack('<HBB', ti, len(order), 0) + bytes(order + [0] * (SLOTS - len(order)))
            m += struct.pack('<2H', len(verts), len(idx))
            for wq, u, v, nq, x, y, z in verts:
                m += bytes(wq) + struct.pack('<2f3bx3f', u, v, *nq, x, y, z)
            m += struct.pack(f'<{len(idx)}H', *idx) + bytes(-2 * len(idx) % 4)
        m += bytes(-len(m) % 16)
    blobs.append(bytes(m))
    s = bytearray()
    for sk in skels:
        s += struct.pack('<2I', sk['hash'], len(sk['bones']))
        for b in sk['bones']:
            s += struct.pack('<i3f', b['parent'], *b['t']) + b['inv'].astype('<f4').tobytes()
    blobs.append(bytes(s))
    a = bytearray()
    for an in anims:
        q = np.clip(np.round(an['q'] * 32767), -32767, 32767).astype('<i2')
        a += struct.pack('<IHBBB3x', an['hash'], an['frames'], q.shape[1], len(an['fixed']), len(an['moving']))
        for bone, xyz in an['fixed']:
            a += struct.pack('<B3x3f', bone, *xyz)
        a += bytes(an['moving']) + bytes(-len(an['moving']) % 4)
        a += q.tobytes() + bytes(-q.nbytes % 4)
        a += an['tm'].astype('<f4').tobytes()
    blobs.append(bytes(a))
    c = bytearray()
    for ac in acts:
        c += struct.pack('<3I12f2H', ac['model'], ac['skel'], ac['anim'], *ac['rot'], *ac['pos'],
                         ac['start'], ac['flags'])
        if version > 1:
            c += struct.pack('<2H', *ac['loop'])
    blobs.append(bytes(c))
    head = struct.pack('<4sI5I', b'N2NI', version, len(texs), len(mods), len(skels), len(anims), len(acts))
    at = len(head) + 20 + (4 if version > 2 else 0)
    at += -at % 16
    offs, body = [], bytearray()
    for b in blobs:
        offs.append(at + len(body))
        body += b + bytes(-len(b) % 16)
    out = head + struct.pack('<5I', *offs)
    if version > 2:
        out += struct.pack('<I', end)
    out += bytes(at - len(out)) + body
    path.write_bytes(out)
    return len(out)


def convert(game, n, out):
    data = archived(game, f'NIS/Scene_IntroNis{n:02d}_BundleB.bun')
    tpk = FlatTPK(data)
    skels = [skeleton(data[o:o + s]) for t, o, s in chunks(data) if t == 0xe34009]
    anims = {}
    for t, o, s in chunks(data):
        if t == 0xe34010:
            anims.update(bank(data[o:o + s]))
    mods = models(data)
    acts = [a for a in actors(data) if a['model'] in mods]
    # the skeleton an actor names: its hash, else by bone count of the anim
    sk_by = {sk['hash']: k for k, sk in enumerate(skels)}
    an_list = list(anims.values())
    an_by = {a['hash']: k for k, a in enumerate(an_list)}
    texkeys, texs = {}, []
    mod_list = []
    for key, m in mods.items():
        draws = []
        for texkey, order, verts, idx in batches(m):
            if texkey not in texkeys:
                tex = tpk.get(texkey)
                w, h, pal, idx8, _ = to_clut8(tex, 128)
                texkeys[texkey] = len(texs)
                texs.append((w, h, pal, idx8))
            draws.append((texkeys[texkey], order, verts, idx))
        mod_list.append((key, draws))
    shadow = tpk.by_name('CHARACTERSHADOW')   # the blob under each actor, last
    if shadow is None:
        raise ValueError('no CHARACTERSHADOW')
    texs.append(to_clut8(shadow, 32)[:4])
    mod_index = {key: k for k, (key, _) in enumerate(mod_list)}
    rows = []
    for a in acts:
        an = an_by.get(a['anim'], 0xffffffff)
        nb = an_list[an]['q'].shape[1] if an != 0xffffffff else None
        sk = sk_by.get(a['skel'])
        if sk is None:   # by the animation's bone count
            sk = next((k for k, s in enumerate(skels) if len(s['bones']) == nb), 0)
        girl = an != 0xffffffff and 'flag_girl' in an_list[an]['name']
        if an != 0xffffffff:
            an_list[an].setdefault('skel', sk)
        rows.append(dict(model=mod_index[a['model']], skel=sk, anim=an, rot=a['rot'], pos=a['pos'], start=0,
                         flags=actor_flags(a['flags'], girl), loop=a['loop']))
    for an in an_list:      # the channels over the skeleton's rest
        rest = skels[an.get('skel', next((k for k, s in enumerate(skels)
                                           if len(s['bones']) == an['q'].shape[1]), 0))]['bones']
        an['fixed'], an['moving'], tm = [], [], []
        for b, t in sorted(an['trans'].items()):
            if b >= len(rest):
                continue
            t = np.where(np.isnan(t), np.array(rest[b]['t']), t)
            if np.ptp(t, axis=0).max() < 1e-4:
                an['fixed'].append((b, tuple(t[0])))
            else:
                an['moving'].append(b)
                tm.append(t)
        an['tm'] = np.stack(tm, 1) if tm else np.zeros((an['frames'], 0, 3))
    size = write(out / f'nis-{n:02d}.n2ni', texs, mod_list, skels, an_list, rows, version=3, end=scene_end(data))
    return dict(scene=n, bytes=size, actors=len(rows), models=len(mod_list), anims=len(an_list), skeletons=len(skels))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--only', type=int, nargs='*')
    args = ap.parse_args()
    for n in args.only or INTROS:
        try:
            print(convert(args.game, n, args.out))
        except (ValueError, KeyError) as e:
            print(f'IntroNis{n:02d}: {e}', file=sys.stderr)


if __name__ == '__main__':
    main()
