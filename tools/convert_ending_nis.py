#!/usr/bin/env python3
"""The race's ending scenes (NIS\\Scene_EndingNis0<N>_BundleB.bun, N 1-4) ->
nisend-0<N>.n2ni (the crowd) and ending.n2ed (what picks a scene, where it
plays, its cars, its camera and its events).

Each ending is a bundle like the intros' (tools/convert_nis.py: skinned
crowd, EAGL skeletons and animations, the scene 0x80037020) plus the cars'
paths: ZPM_Car<k>_t and _q, the root of car k (k 1 the race's winner, then
the others in their finishing order) in the scene frame, 15 frames a second.
The game reads, outside the bundle:
- GLOBAL\\INGAMECOMMON.BUN 0x80037050: the scenes' directory (0x37060) and
  their candidates (0x37070): for the race's end, 8-byte records (scene
  hash, camera track, flags: 0x40 a race's end, 0x80 when a human won; then
  the car slots the player takes when it won and when it lost), and two
  tables by event (going forward, going backward) of which candidates may
  play there (12-byte records: u16 event, ..., u16 mask at +10; an event not
  listed takes them all);
- GLOBAL\\INGAME*.BUN 0x8003b201: the scenes' camera tracks (a group per
  scene hash; a track is a 32-byte head, its key count at +0x10, then
  0x88-byte keys: parameter (the scene's time over its length), eye and
  look interpolation, their spaces (3: the scene's frame), start and end
  tangent lengths, start and end eye, look and lens);
- TRACKS\\ROUTES<world>\\TRACKPOSMARKERSALL.BIN 0x34146: 48-byte markers (name
  hash at +8, the place at +16, a binary angle at +32, the event at +36):
  the scene's origin is the event's ForwardFinishLine (BackwardFinishLine
  reversed), or its start line when it has none (a circuit);
- NIS\\Scene_EndingNis0<N>_EvtTrk.txt: the cars' events by time (frames at
  30 a second): burnout, brake lock, the engine's sound pattern, and the
  compound SEQ_CAR_NITRO and SEQ_CAR_TORQUE, which the game expands from
  templates set at its start (below).

ending.n2ed (little endian): 'N2ED', u32 2, then
  u32 n, n x candidate (u8 scene 1-4, u8 camera track, u8 flags, u8 the
    player's car slot when it won, u8 when it lost, 3 pad);
  u32 n, n x (u16 event, u16 mask) going forward; the same going backward;
  u32 n, n x marker (u16 event, u8 kind: 0 forward start, 1 forward
    finish, 2 backward start, 3 backward finish, pad, f32 game x, y, z,
    u16 angle, u16 pad);
  u32 scenes, each a scene_block (psp-viewer/nisscene.h): u32 its n2ni's
    bytes, f32 the camera's span, the countdown's end and the scene's end
    (s), u32 cars, u32 cameras, u32 events; cars x (u32 frames, frames x
    f32 x, y, heading: the scene's frame, 15 a second); cameras x (u32
    keys, keys x (f32 parameter, u8 eye cubic, u8 look cubic, u8 eye
    space, u8 look space (3 the scene's frame), f32 eye and look slopes at
    the start and the end [12], f32 eye from [3] and to [3], look from [3]
    and to [3], f32 horizontal field of view from and to, rad)); events x
    (f32 seconds, u8 type, u8 car, u8 sub, u8 pad, f32 a, f32 b): types 1
    an effect (sub 10: nitrous, a its intensity), 4 brake lock, 5 burnout,
    6 the engine's torque on the body (a, b), 13 the engine's sound
    pattern (sub).
"""
import argparse
import math
import re
import struct
import sys
from pathlib import Path

import numpy as np

from n2formats import archived, chunks, FlatTPK
from convert_car import to_clut8
import convert_nis as nis
import n2source

ROOT = Path(__file__).resolve().parents[1]
SCENES = (1, 2, 3, 4)
MARKERS = ('ForwardStartLine', 'ForwardFinishLine', 'BackwardStartLine', 'BackwardFinishLine')
CAMERA_BUNDLES = ('GLOBAL/INGAMERACE.BUN', 'GLOBAL/INGAMEDRAG.BUN')
LEVELS = ('L4RA', 'L4RB', 'L4RC', 'L4RD', 'L4RE', 'L4RF', 'L4RG', 'L4RH')   # the city and the closed tracks' worlds

# The compound events, as the game's static initializer fills them: each
# (type, frames after the event, sub, a, b); the car the event's. SEQ_CAR_NITRO
# (car, intensity): the engine's torque, the nitrous flame from frame 2 to 5
# (a scaled by the intensity), the torque back off. SEQ_CAR_BURNOUT (car,
# duration): the burnout's throttle and the body pitching back and forth,
# from the event (BURNOUT_START) and up to the end of the duration
# (BURNOUT_END, frames before it). SEQ_CAR_TORQUE (car, duration, scale,
# period): the torque, then rocking it every period until the duration,
# then off.
SEQ_NITRO = ((6, 0, 0, .5, .5), (1, 2, 10, 2., 0), (1, 5, 10, 0, 0), (6, 10, 0, 0, 0))
BURNOUT_START = ((5, 0, 0, 5., 0), (6, 0, 0, .1, -2.), (5, 3, 0, 10., 0), (6, 3, 0, 0, -2.), (6, 8, 0, 0, 2.),
                 (6, 12, 0, 0, -1.), (6, 17, 0, 0, 1.), (6, 22, 0, 0, -.5), (6, 27, 0, 0, .5), (6, 32, 0, 0, 1.))
BURNOUT_END = ((5, -20, 0, 10., 0), (5, -15, 0, 8., 0), (5, -10, 0, 6., 0), (6, -10, 0, 0, 0), (5, -5, 0, 3., 0),
               (5, 0, 0, 0, 0))
TORQUE_HEAD, TORQUE_A, TORQUE_B, TORQUE_END = (.5, .5), (-.5, .5), (.5, -.5), (0, 0)
# The game's event types (CAnimEventDecoder), its car effects and wheels.
TYPES = {'CAR_TRIGGER_EFFECT': 1, 'CAR_HYDRAULIC_BOUNCE': 2, 'CAR_HYDRAULIC_EXTEND': 3, 'CAR_BRAKE_LOCK': 4,
         'CAR_BURNOUT_ADDITIVE': 5, 'CAR_ENGINE_TORQUE': 6, 'SEQ_CAR_NITRO': 7, 'SEQ_CAR_BURNOUT': 8,
         'SEQ_CAR_TORQUE': 9, 'SCREEN_FLASH': 10, 'WORLD_ANIM_EVENT': 11, 'WORLD_MESSAGE': 12,
         'CAR_AUDIO_PATTERN': 13, 'CAR_AUDIO_REV': 14}
EFFECTS = {'CARFX_NITRO': 10, 'CARFX_EXHAUST_SMOKE': 11, 'CARFX_EXHAUST_BLOWOFF': 12, 'CARFX_NOS_BLOWOFF': 13}
WHEELS = {'CAR_WHEEL_FL': 0, 'CAR_WHEEL_FR': 1, 'CAR_WHEEL_RR': 2, 'CAR_WHEEL_RL': 3}


def by_hash(game, h):
    """A file of the archive by its name hash (names not known)."""
    z = (game / 'ZDIR.BIN').read_bytes()
    for e in struct.iter_unpack('<6I', z):
        if e[0] == h:
            with (game / f'ZZDATA{e[1]}.BIN').open('rb') as f:
                f.seek(e[2] * 2048)
                return f.read(e[4])
    raise ValueError(f'not in ZDIR: {h:08x}')


# ------------------------------------------------------------ the choosing
def candidates(game, scene_of):
    d = archived(game, 'GLOBAL/INGAMECOMMON.BUN')
    D = next(d[o:o + s] for t, o, s in chunks(d) if t == 0x37070)
    counts = struct.unpack_from('<7I', D, 0)
    cand = []
    for i in range(counts[1]):
        h, cam, flags, won, lost = struct.unpack_from('<I4B', D, 0x25c + 8 * i)
        cand.append((scene_of[h], cam, flags, won, lost))
    def masks(at, n):
        return [struct.unpack_from('<H8xH', D, at + 12 * i) for i in range(n)]
    return cand, masks(0x37c, counts[5]), masks(0x91c, counts[6])


def markers(game):
    want = {nis.h33(n): k for k, n in enumerate(MARKERS)}
    out = []
    for level in LEVELS:
        try:
            d = archived(game, f'TRACKS/ROUTES{level}/TRACKPOSMARKERSALL.BIN')
        except ValueError:
            continue
        out += level_markers(d, want)
    return sorted(out)


def level_markers(d, want):
    out = []
    for t, o, s in chunks(d):
        if t != 0x34146:
            continue
        p = (o + 15) & ~15
        for i in range((o + s - p) // 48):
            r = d[p + 48 * i:p + 48 * i + 48]
            h, = struct.unpack_from('<I', r, 8)
            if h in want:
                x, y, z = struct.unpack_from('<3f', r, 16)
                ang, = struct.unpack_from('<H', r, 32)
                ev, = struct.unpack_from('<I', r, 36)
                out.append((ev, want[h], x, y, z, ang))
    return out


# ------------------------------------------------------------ the cameras
def camera_groups(game):
    for name in CAMERA_BUNDLES:
        try:
            d = archived(game, name)
        except ValueError:
            continue
        for t, o, s in chunks(d):
            if t != 0x8003b201:
                continue
            groups, p = {}, o
            while p < o + s:
                _, size = struct.unpack_from('<II', d, p)
                h, n = struct.unpack_from('<II', d, p + 8)
                q, tracks = p + 16, []
                for _ in range(n):
                    nk = struct.unpack_from('<b', d, q + 0x10)[0]
                    tracks.append([d[q + 0x20 + 0x88 * j:q + 0x20 + 0x88 * (j + 1)] for j in range(nk)])
                    q += 0x20 + 0x88 * nk
                groups[h] = tracks
                p += 8 + size
            return groups
    raise ValueError('no camera set')


def lens_fov(lens):
    """The game's lens (mm) to its horizontal field of view: 2 atan(15.96 / lens)."""
    return 2 * math.atan2(15.96, lens)


def camera_track(raw):
    keys = []
    for r in raw:
        f = struct.unpack_from('<34f', r)
        keys.append(dict(t=f[3], kind=r[0], eye_cubic=r[2], look_cubic=r[3], space=(r[4], r[5]),
                         tan=(f[4], f[5]), eye=(np.array(f[6:9]), np.array(f[9:12])),
                         look=(np.array(f[12:15]), np.array(f[15:18])), lens=(f[18], f[19])))
    def span(j):
        return (keys[j + 1]['t'] if j + 1 < len(keys) else 1.) - keys[j]['t']
    def shared(a, ea, b, eb):
        return all(np.allclose(a[w][ea], b[w][eb], atol=1e-3) for w in ('eye', 'look'))
    out = []
    for j, k in enumerate(keys):
        slopes = []
        for end in (0, 1):   # the game's slope at a key's start (0) and end (1)
            tl = k['tan'][end]
            dk = {w: k[w][1] - k[w][0] for w in ('eye', 'look')}
            nb = keys[j - 1] if end == 0 and j > 0 else keys[j + 1] if end == 1 and j + 1 < len(keys) else None
            if k['kind'] and nb is not None and nb['kind'] and shared(k, end, nb, end ^ 1):
                ik, inb = span(j), span(keys.index(nb))
                f1 = ik / (ik + inb)
                f3 = 1 - f1
                if inb > 1e-6:
                    f1 *= ik / inb
                slopes.append({w: (nb[w][1] - nb[w][0]) * f1 * tl + dk[w] * f3 * tl for w in dk})
            else:
                slopes.append({w: dk[w] * tl for w in dk})
        out.append((k['t'], k['eye_cubic'], k['look_cubic'], k['space'],
                    [*slopes[0]['eye'], *slopes[0]['look'], *slopes[1]['eye'], *slopes[1]['look']],
                    [*k['eye'][0], *k['eye'][1], *k['look'][0], *k['look'][1]],
                    (lens_fov(k['lens'][0]), lens_fov(k['lens'][1]))))
    return out


# ------------------------------------------------------------ the cars
def car_paths(data):
    """[(frames x (x, y, heading))] for Car1.. in the scene frame."""
    paths = {}
    for t, o, s in chunks(data):
        if t != 0xe34010:
            continue
        D, syms = nis.elf_data(data[o:o + s])
        b = syms['__AnimationBank:::bank']
        w = struct.unpack_from('<8I', D, b)
        ptrs = struct.unpack_from(f'<{w[1]}I', D, w[4])
        names = struct.unpack_from(f'<{w[1]}I', D, w[5])
        for p, no in zip(ptrs, names):
            name = D[no:D.index(b'\0', no)].decode()
            m = re.fullmatch(r'ZPM_Car(\d)_([tq])', name)
            if not m:
                continue
            top = struct.unpack_from('<8I', D, p)
            ch = struct.unpack_from('<4I', D, top[3])
            if ch[0] & 0xffff not in (0x0a, 0x0b):
                raise ValueError(f'{name}: channel {ch[0] & 0xffff:#x}')
            paths.setdefault(int(m[1]), {})[m[2]] = nis.deltas(D, ch[1], ch[2] & 0xffff)
    out = []
    for k in sorted(paths):
        t, q = paths[k]['t'], paths[k]['q']
        q = q / np.linalg.norm(q, axis=1, keepdims=True)
        x, y, z, w = q.T
        heading = np.arctan2(2 * (x * y + z * w), 1 - 2 * (y * y + z * z))   # the root's forward (x) axis
        out.append(np.stack([t[:, 0], t[:, 1], heading], 1))
    if [k for k in sorted(paths)] != list(range(1, len(out) + 1)):
        raise ValueError('the cars are not numbered from 1')
    return out


# ------------------------------------------------------------ the events
def track_events(text, name):
    """An _EvtTrk.txt's events, as the game's CAnimEventTrack reads them:
    [(seconds, type, car, sub, a, b)] sorted by time, the compound ones
    expanded (above); its times are frames at 30 a second."""
    out = []
    for line in text.split('\n'):
        f = line.split('//')[0].split()
        if len(f) < 3 or f[0] != 'EVENT:':
            continue
        num = lambda v: float(v.rstrip('f'))
        at, kind, args = num(f[1]), TYPES.get(f[2]), f[3:]
        if kind is None:
            raise ValueError(f'{name}: {f[2]} is not an event')
        car = int(args[0]) if kind != 10 else 0
        if kind == 7:      # SEQ_CAR_NITRO car intensity
            for typ, dt, sub, a, b in SEQ_NITRO:
                out.append((at + dt, typ, car, sub, a * num(args[1]), b))
        elif kind == 8:    # SEQ_CAR_BURNOUT car duration
            out += [(at + dt, typ, car, sub, a, b) for typ, dt, sub, a, b in BURNOUT_START]
            out += [(at + num(args[1]) + dt, typ, car, sub, a, b) for typ, dt, sub, a, b in BURNOUT_END]
        elif kind == 9:    # SEQ_CAR_TORQUE car duration scale period
            dur, scale, period = (num(v) for v in args[1:4])
            out.append((at, 6, car, 0, TORQUE_HEAD[0] * scale, TORQUE_HEAD[1]))
            t, flip = 0., True
            while dur > 0:
                t += period
                a, b = TORQUE_A if flip else TORQUE_B
                out.append((at + t, 6, car, 0, a * scale, b))
                flip = not flip
                if t >= dur:
                    break
            out.append((at + t + period, 6, car, 0, TORQUE_END[0] * scale, TORQUE_END[1]))
        elif kind == 1:    # car effect intensity parameter
            out.append((at, 1, car, EFFECTS[args[1]], num(args[2]), 0))
        elif kind in (2, 3):   # car wheel amount
            out.append((at, kind, car, WHEELS[args[1]], num(args[2]), 0))
        elif kind in (4, 5):   # car amount
            out.append((at, kind, car, 0, num(args[1]), 0))
        elif kind == 6:    # car a b
            out.append((at, 6, car, 0, num(args[1]), num(args[2])))
        elif kind == 10:   # SCREEN_FLASH kind intensity
            out.append((at, 10, 0, int(args[0]), num(args[1]), 0))
        elif kind == 13:   # CAR_AUDIO_PATTERN car pattern level
            out.append((at, 13, car, int(args[1]), num(args[2]), 0))
        else:
            raise ValueError(f'{name}: {f[2]} is not handled')
    return sorted((t / 30., *rest) for t, *rest in out)


def events(game, n):
    try:
        text = archived(game, f'NIS/Scene_EndingNis{n:02d}_EvtTrk.txt').decode('latin-1')
    except ValueError:
        return []
    return track_events(text, f'EndingNis{n:02d}')


# ------------------------------------------------------------ the crowd
def actors(data):
    """The scene's people: model, skeleton, animation, place, and how it
    loops (entity flags 0x20 loops, 0x40 between two times, frames at 30)."""
    out = []
    for t, o, n in chunks(data):
        if t != 0x37040:
            continue
        b = data[o:o + n]
        f = struct.unpack_from('<38f', b)
        i = struct.unpack_from('<38I', b)
        rot = [f[6], f[7], f[8], f[10], f[11], f[12], f[14], f[15], f[16]]
        out.append(dict(rot=rot, pos=f[18:21], model=i[22], anim=i[29], skel=i[30], flags=i[5], loop=(i[34], i[35])))
    return out


def people(game, n, out):
    data = archived(game, f'NIS/Scene_EndingNis{n:02d}_BundleB.bun')
    tpk = FlatTPK(data)
    skels = [nis.skeleton(data[o:o + s]) for t, o, s in chunks(data) if t == 0xe34009]
    anims = {}
    for t, o, s in chunks(data):
        if t == 0xe34010:
            anims.update({k: v for k, v in nis.bank(data[o:o + s]).items() if not k.startswith(('ZPM_', 'XO_'))})
    mods = nis.models(data)
    acts = [a for a in actors(data) if a['model'] in mods and a['anim']]
    sk_by = {sk['hash']: k for k, sk in enumerate(skels)}
    an_list = list(anims.values())
    an_by = {a['hash']: k for k, a in enumerate(an_list)}
    texkeys, texs, mod_list = {}, [], []
    for key, m in mods.items():
        draws = []
        for texkey, order, verts, idx in nis.batches(m):
            if texkey not in texkeys:
                w, h, pal, idx8, _ = to_clut8(tpk.get(texkey), 128)
                texkeys[texkey] = len(texs)
                texs.append((w, h, pal, idx8))
            draws.append((texkeys[texkey], order, verts, idx))
        mod_list.append((key, draws))
    shadow = tpk.by_name('CHARACTERSHADOW')
    if shadow is None:
        raise ValueError('no CHARACTERSHADOW')
    texs.append(to_clut8(shadow, 32)[:4])
    mod_index = {key: k for k, (key, _) in enumerate(mod_list)}
    rows = []
    for a in acts:
        an = an_by[a['anim']]
        sk = sk_by[a['skel']]
        an_list[an].setdefault('skel', sk)
        rows.append(dict(model=mod_index[a['model']], skel=sk, anim=an, rot=a['rot'], pos=a['pos'], start=0,
                         flags=nis.actor_flags(a['flags'], False), loop=a['loop']))
    for an in an_list:
        rest = skels[an['skel']]['bones']
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
    size = nis.write(out / f'nisend-{n:02d}.n2ni', texs, mod_list, skels, an_list, rows, version=2)
    return size, dict(actors=len(rows), models=len(mod_list), anims=len(an_list), textures=len(texs), skeletons=len(skels))


def scene_times(data):
    """The scene's header (0x37030), frames at 30 a second -> seconds: the
    camera's span (+0x30: the intro's StartCountdown marker, the ending's
    length; the track's parameter is the scene's time over it), the
    countdown's end (+0x2c) and the scene's end (+0x28)."""
    h = next(data[o:o + s] for t, o, s in chunks(data) if t == 0x37030)
    span, cd_end, end = (struct.unpack_from('<I', h, a)[0] / 30. for a in (0x30, 0x2c, 0x28))
    return span, cd_end, end


def scene_block(size, data, cars, cams, evs):
    """A scene's block (psp-viewer/nisscene.h): its n2ni's bytes, its
    times, its cars' paths, its camera tracks and its events."""
    blob = bytearray(struct.pack('<I3f3I', size, *scene_times(data), len(cars), len(cams), len(evs)))
    for c in cars:
        blob += struct.pack('<I', len(c)) + c.astype('<f4').tobytes()
    for keys in cams:
        blob += struct.pack('<I', len(keys))
        for t, ec, lc, space, slopes, pts, fov in keys:
            blob += struct.pack('<f4B12f12f2f', t, ec, lc, *space, *slopes, *pts, *fov)
    for t, typ, car, sub, a, b in evs:
        blob += struct.pack('<f3Bx2f', t, typ, car, sub, a, b)
    return bytes(blob)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    scene_of = {nis.h33(f'EndingNis{n:02d}'): n for n in SCENES}
    cand, fwd, back = candidates(args.game, scene_of)
    marks = markers(args.game)
    groups = camera_groups(args.game)
    blob = bytearray(struct.pack('<4sI', b'N2ED', 2))
    blob += struct.pack('<I', len(cand)) + b''.join(struct.pack('<5B3x', *c) for c in cand)
    for table in (fwd, back):
        blob += struct.pack('<I', len(table)) + b''.join(struct.pack('<2H', *e) for e in table)
    blob += struct.pack('<I', len(marks)) + b''.join(struct.pack('<HBx3fH2x', ev, k, x, y, z, a) for ev, k, x, y, z, a in marks)
    blob += struct.pack('<I', len(SCENES))
    for n in SCENES:
        data = archived(args.game, f'NIS/Scene_EndingNis{n:02d}_BundleB.bun')
        size, stats = people(args.game, n, args.out)
        cars = car_paths(data)
        cams = [camera_track(t) for t in groups[nis.h33(f'EndingNis{n:02d}')]]
        evs = events(args.game, n)
        blob += scene_block(size, data, cars, cams, evs)
        print(dict(scene=n, bytes=size, cars=len(cars), car_frames=[len(c) for c in cars], cameras=len(cams),
                   events=len(evs), **stats))
    (args.out / 'ending.n2ed').write_bytes(bytes(blob))
    print(dict(candidates=len(cand), forward=len(fwd), backward=len(back), markers=len(marks), bytes=len(blob)))


if __name__ == '__main__':
    main()
