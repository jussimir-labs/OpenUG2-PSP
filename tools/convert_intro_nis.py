#!/usr/bin/env python3
"""The race's intro scenes (NIS\\Scene_IntroNis<NN>_BundleB.bun) -> nis-<NN>.n2ni
(the people: tools/convert_nis.py) and intro.n2in (what picks a scene,
where it plays, its cars, its camera and its events).

What the game reads to choose and play one (the GameCube's CAnimChooser,
CRaceStartAnimSelectionContext, CAnimLocator and CAnimScene):
- GLOBAL\\INGAMECOMMON.BUN 0x80037050 -> 0x37070: the race start's
  candidates, 12-byte records from +0x1c (count at +0): the scene's hash,
  its camera track, flags (0x40 a race's start; the low six bits the grid
  positions a human may start from, bit k position k+1), the fewest and
  the most racers, the track's grid layout it is made for; and two tables
  by event (going forward at +0x37c, backward at +0x91c; 12-byte records:
  u16 event, a u32 mask of the candidates 0-31 at +4, a u16 of 32-47 at
  +8; an event not listed takes them all);
- GLOBAL\\GLOBALB.BUN 0x34201 (TrackInfo, 0x128 bytes, the event at
  +0x8a): the grid layout (+0xf6);
- GLOBALB 0x34A11 (career races, 0x88 bytes): a race with its byte +0x0d
  set plays only IntroNis08 or IntroNis12;
- TRACKS\\ROUTES<world>\\TRACKPOSMARKERSALL.BIN 0x34146: the scene's origin is
  the event's ForwardStartLine (BackwardStartLine reversed), turned as the
  marker; the grid's ForwardStartingPosition / BackwardStartingPosition
  slots (their index at +12) are the positions 1-8 the cars start from;
- GLOBAL\\INGAME*.BUN 0x8003b201: the camera tracks (convert_ending_nis.py);
- the scene's header (0x37030): its StartCountdown marker (+0x30: the
  camera's span and where the countdown starts), the countdown's end
  (+0x2c) and the scene's end (+0x28), frames at 30;
- the cars' paths ZPM_Car<k>_t/_q (car k the one starting from position k);
- NIS\\Scene_IntroNis<NN>_EvtTrk.txt: their events (convert_ending_nis.py).

The engines' rev patterns of the scenes' CAR_AUDIO_PATTERN events (the
game's SFXCTL_Physics::UpdateNIS: RevDynoEnter, RevDynoLeave, RevShopEnter,
RevShopLeave, RevPat5-12, RevEnding1-3; their steps in its code) are the
Xbox executable's data (default.xbe, from 0x387958, each table aligned to 8
bytes): a step a u16 rpm on the game's 1000-10000 scale, a u8 throttle in
percent and a pad -> revpat.n2rp: 'N2RP', u32 16 (by pattern: 0 and 1 the
first table), then each u32 steps, (steps + 1) x (u16 rpm, u8 throttle, u8
pad).

The flag girls with flares (CBasicCharacterAnimEntity::CreateFlares): a
35-bone actor whose model is named in the game's FlagGirlFlareMap (Xbox
default.xbe 0x37fbf0, 4 records of 52 bytes: a pointer to the model's name,
then the tip and base of the torch in each hand, model bind space): only
IntroNis 12, 18, 20 (FLAG_GIRL_MODEL_9_FLARES) and 22 (_8_FLARES).

The scenes' sound (SOUND\\SPEECH\\NISAUDIO.BIG, its .EVT and .IDX laid out
as the speech's, tools/convert_audio.py speech()): banks of SCHl streams,
EA-XA v2 stereo at 36 kHz, picked as the game's NIS_AudioParser asks
(events NIS_Select_Start and NIS_Select_End; a line plays when each of its
parameters is a bit set in the request's): the start's p0 the scene (bit
scene-1), p1 the camera track (track 0 bit 13, track k bit k-1; the
track 4 on for an SUV of the player), p2 the long version (bit 0, from
the scene's start) or the short one (bit 2, from its StartCountdown: a
skip, a restart); the ending's p0 EndingNis01 bit 0, 02 bit 1, 03 and 04
bit 2, p1 its track. -> nis.big (the streams the scenes can ask for,
copied as they are) and nis.n2sp (speech.n2sp's layout, events "Start"
and "End").

intro.n2in (little endian): 'N2IN', u32 2, then
  u32 n, n x candidate (u8 scene, u8 camera track, u8 flags, u8 fewest
    racers, u8 most, u8 grid layout, 2 pad);
  u32 n, n x (u16 event, u16 pad, u32 mask of 0-31, u16 mask of 32-47, u16
    pad) going forward; the same going backward;
  u32 n, n x (u16 event, u8 grid layout, u8 pad);
  u32 n, n x (u16 event, u8 career stage, u8 pad): the career races that
    play only IntroNis08 or 12;
  u32 n, n x marker (u16 event, u8 kind: 0 forward start line, 2 backward
    start line, 4 a forward grid slot, 5 a backward one; u8 the slot's
    index, f32 game x, y, z, u16 angle, u16 pad);
  u32 scenes, an index of them: each u8 its number, u8 1 when it has a
    flag girl with flares, 2 pad, u32 its block's offset in the file, u32
    its block's bytes, u32 the flag girl's actor record in nis-<NN>.n2ni,
    f32 her torches' tip and base, A then B [12];
  then the blocks: each scene's scene_block (convert_ending_nis.py,
    psp-viewer/nisscene.h). The game reads the small tables, the markers in
    passing and only the chosen scene's block (the PSP-1000's room at a
    race's start is tight).
"""
import argparse

from n2formats import game_hash
from convert_audio import pt_header
import re
import struct
import sys
from pathlib import Path

from n2formats import archived, chunks
import convert_nis as nis
import convert_ending_nis as en
import n2exe
import n2source

ROOT = Path(__file__).resolve().parents[1]
START_MARKERS = {'ForwardStartLine': 0, 'BackwardStartLine': 2, 'ForwardStartingPosition': 4,
                 'BackwardStartingPosition': 5}


def candidates(game):
    """[(scene, camera, flags, fewest, most, grid)], forward masks,
    backward masks: [(event, mask)]."""
    d = archived(game, 'GLOBAL/INGAMECOMMON.BUN')
    D = next(d[o:o + s] for t, o, s in chunks(d) if t == 0x37070)
    counts = struct.unpack_from('<7I', D, 0)
    scene_of = {nis.h33(f'IntroNis{n:02d}'): n for n in range(1, 29)}
    cand = []
    for i in range(counts[0]):
        h, cam, flags, few, most, grid = struct.unpack_from('<I5B', D, 0x1c + 12 * i)
        cand.append((scene_of[h], cam, flags, few, most, grid))
    def masks(at, n):
        out = []
        for i in range(n):
            ev, lo, hi = struct.unpack_from('<H2xIH', D, at + 12 * i)
            out.append((ev, lo | hi << 32))
        return out
    return cand, masks(0x37c, counts[5]), masks(0x91c, counts[6])


def grid_layouts(game):
    d = archived(game, 'GLOBAL/GLOBALB.BUN')
    blob = next(d[p:p + n] for t, p, n in chunks(d) if t == 0x34201)
    out = {}
    for k in range(len(blob) // 0x128):
        r = blob[k * 0x128:(k + 1) * 0x128]
        out.setdefault(struct.unpack_from('<H', r, 0x8a)[0], r[0xf6])
    return sorted(out.items())


def career_restricted(game):
    """(event, stage) of the career races whose byte +0x0d is set."""
    d = archived(game, 'GLOBAL/GLOBALB.BUN')
    races = next(d[p:p + n] for t, p, n in chunks(d) if t == 0x34A11)
    out = set()
    for k in range(len(races) // 0x88):
        r = races[k * 0x88:(k + 1) * 0x88]
        if r[0x0d] == 1:
            for i in range(min(r[0x7e], 4)):
                out.add((struct.unpack_from('<H', r, 0x18 + 4 * i)[0], r[0x37]))
    return sorted(out)


def markers(game):
    want = {nis.h33(n): k for n, k in START_MARKERS.items()}
    out = []
    for level in en.LEVELS:
        try:
            d = archived(game, f'TRACKS/ROUTES{level}/TRACKPOSMARKERSALL.BIN')
        except ValueError:
            continue
        for t, o, s in chunks(d):
            if t != 0x34146:
                continue
            p = (o + 15) & ~15
            for i in range((o + s - p) // 48):
                r = d[p + 48 * i:p + 48 * i + 48]
                h, idx = struct.unpack_from('<2I', r, 8)
                if h in want:
                    x, y, z = struct.unpack_from('<3f', r, 16)
                    ang, = struct.unpack_from('<H', r, 32)
                    ev, = struct.unpack_from('<I', r, 36)
                    out.append((ev, want[h], idx if want[h] >= 4 else 0, x, y, z, ang))
    return sorted(set(out))


FLAREMAP_AT = 0x37fbf0   # default.xbe: FlagGirlFlareMap


def xbe_offset(b, va):
    base, = struct.unpack_from('<I', b, 0x104)
    n, at = struct.unpack_from('<2I', b, 0x11c)
    for i in range(n):
        _, vaddr, _, raw, size, _ = struct.unpack_from('<6I', b, at - base + 56 * i)
        if vaddr <= va < vaddr + size:
            return raw + va - vaddr
    raise ValueError(f'default.xbe: {va:#x} in no section')


def flare_map(xbe):
    """{model name: tip A, base A, tip B, base B (12 floats)}. The table
    is the first word pointing at a FLAG_GIRL_MODEL_ name (FLAREMAP_AT on
    the Xbox)."""
    import n2exe
    exe = n2exe.Exe(xbe)
    b = exe.b
    names = sorted(set(m.group().decode() for m in re.finditer(rb'FLAG_GIRL_MODEL_[0-9A-Z_]+', b)))
    first = min(r for n in names for r in exe.refs(n))
    out = {}
    for k in range(4):
        at = first + 52 * k
        name = exe.cstr(struct.unpack_from('<I', b, at)[0])
        if not name.startswith('FLAG_GIRL_MODEL_'):
            raise ValueError(f'default.xbe {at:#x}: not FlagGirlFlareMap')
        out[name] = struct.unpack_from('<12f', b, at + 4)
    return out


def flag_girls(data, flares):
    """[(record, points)]: the scene's actors (as convert_nis writes them)
    that hold flares."""
    mods = nis.models(data)
    acts = [a for a in nis.actors(data) if a['model'] in mods]
    nbones = {sk['hash']: len(sk['bones']) for sk in (nis.skeleton(data[o:o + s]) for t, o, s in chunks(data) if t == 0xe34009)}
    return [(i, flares[mods[a['model']]['name']]) for i, a in enumerate(acts)
            if mods[a['model']]['name'] in flares and nbones.get(a['skel']) == 35]


def track_bit(track):
    """The game's camera track's bit (NIS_AudioParser::QueStream)."""
    return 13 if track == 0 else track - 1


def nis_audio(game, out, cand, endings):
    """nis.big and nis.n2sp: the start's lines of the candidates' scenes and
    tracks (long and short, an SUV's too) and the ending's of its own."""
    z = (game / 'ZDIR.BIN').read_bytes()
    _, arc, sector, _, size, _ = next(e for e in struct.iter_unpack('<6I', z)
                                      if e[0] == game_hash(r'SOUND\SPEECH\NISAUDIO.BIG'))
    evt, idx = (archived(game, rf'SOUND\SPEECH\NISAUDIO.{e}') for e in ('EVT', 'IDX'))
    nev = struct.unpack_from('<H', evt, 0x10)[0]
    table_at = struct.unpack_from('<I', evt, 4)[0]
    names = [evt[table_at + 32 * nev + struct.unpack_from('<I', evt, table_at + 32 * k)[0]:].split(b'\0')[0].decode()
             for k in range(nev)]
    tags = [evt[4 * o + 28] for o in struct.unpack_from(f'<{nev}H', evt, 0x18)]
    banks = [struct.unpack_from('<4I', idx, 0x58 + 16 * k) for k in range(struct.unpack_from('<I', idx, 0x54)[0])]
    start_ok = {(sc - 1, track_bit(cam + suv)) for sc, cam, *_ in cand for suv in (0, 4)}
    end_ok = {(min(sc, 3) - 1, track_bit(cam)) for sc, cam in endings}
    want = {'NIS_Select_Start': ('Start', lambda p: (p[0], p[1]) in start_ok and p[2] in (0, 2)),
            'NIS_Select_End': ('End', lambda p: (p[0], p[1]) in end_ok)}
    lines = {'Start': [], 'End': []}
    big, rates = bytearray(), set()
    with (game / f'ZZDATA{arc}.BIN').open('rb') as f:
        def read(at, n):
            f.seek(sector * 2048 + at)
            return f.read(n)
        for b, (_, tsize, toff, base) in enumerate(banks):
            t = idx[toff:toff + tsize]
            ev = names[tags.index(t[0])]
            if ev not in want:
                continue
            name, keep = want[ev]
            np_, unit = t[4] & 0x7f, 256 * (t[7] + 1)
            for i in range(t[5]):
                rec = t[12 + (2 + np_) * i:12 + (2 + np_) * (i + 1)]
                params = list(rec[2:])
                if not keep(params):
                    continue
                at = base + struct.unpack_from('>H', rec)[0] * unit
                head = read(at, 0x100)
                if head[:4] != b'SCHl':
                    raise ValueError(f'NISAUDIO bank {b} line {i}: no SCHl at {at:#x}')
                fields = pt_header(head, 8)
                if fields.get(0x82, 1) != 2 or fields.get(0x84) not in (32000, 36000):
                    raise ValueError(f'NISAUDIO bank {b} line {i}: not stereo at 36 kHz (the PS2\'s: 32)')
                rates.add(fields[0x84])
                p = at + struct.unpack_from('<I', head, 4)[0]
                while True:   # to its end block
                    tag, n = struct.unpack('<4sI', read(p, 8))
                    p += n
                    if tag == b'SCEl' or n == 0:
                        break
                lines[name].append((len(big), fields[0x85], b, np_, *(params + [0] * 4)[:4]))
                big += read(at, p - at)
    (out / 'nis.big').write_bytes(bytes(big))
    blob, table, n = bytearray(), bytearray(), 0
    for name in ('Start', 'End'):
        table += struct.pack('<16s2H', name.encode(), n, len(lines[name]))
        for line in lines[name]:
            blob += struct.pack('<2I6B2x', *line)
        n += len(lines[name])
    if len(rates) > 1:
        raise ValueError(f'NISAUDIO: streams at {sorted(rates)} Hz')
    rate = rates.pop() if rates else 36000
    # version 1 at the Xbox's 36 kHz; 2 adds the rate (the PS2's 32 kHz)
    head = struct.pack('<4s3I', b'N2SP', 1, 2, n) if rate == 36000 else struct.pack('<4s4I', b'N2SP', 2, 2, n, rate)
    (out / 'nis.n2sp').write_bytes(head + table + blob)
    return dict(start=len(lines['Start']), end=len(lines['End']), mb=round(len(big) / 1e6, 1))


REV_AT = 0x387958   # default.xbe: the first table, RevDynoEnter
REV_STEPS = (72, 39, 26, 30, 27, 22, 19, 67, 56, 34, 30, 29, 47, 27, 24)


def rev_at(exe):
    """The rev patterns' first table and the tables' alignment (REV_AT and
    8 on the Xbox): where all 15 tables read as rev patterns, one after
    the other, each aligned in memory."""
    b = exe.b

    def nxt(at, steps, align):
        v = exe.va(at)
        return at + ((v + 4 * (steps + 1) + align - 1) & ~(align - 1)) - v

    def ok(at, steps):
        return at + 4 * (steps + 1) <= len(b) and all(1000 <= r <= 10000 and a <= 100 and not pad
                                                      for r, a, pad in struct.iter_unpack('<HBB', b[at:at + 4 * (steps + 1)]))

    def chain(at, align):
        for steps in REV_STEPS:
            if not ok(at, steps):
                return False
            at = nxt(at, steps, align)
        return True
    if b[:4] == b'XBEH' and chain(REV_AT, 8):
        return REV_AT, 8
    for at in range(0, len(b) - 4 * 73, 4):
        r, = struct.unpack_from('<H', b, at)
        if 1000 <= r <= 10000 and ok(at, REV_STEPS[0]):
            for align in (8, 4, 16):
                if chain(at, align):
                    return at, align
    raise ValueError('the rev patterns (RevDynoEnter...) not found in the executable')


def rev_patterns(exe):
    """revpat.n2rp from the executable (n2exe.Exe; the PS2's fills the
    tables in the sound tables' initialiser)."""
    b = exe.b
    (at, align), tables = rev_at(exe), []
    for steps in REV_STEPS:
        t = [struct.unpack_from('<HBB', b, at + 4 * k) for k in range(steps + 1)]
        if any(not 1000 <= r <= 10000 or a > 100 or pad for r, a, pad in t):
            raise ValueError(f'{exe.path.name} {at:#x}: not a rev pattern')
        tables.append(t)
        v = exe.va(at)
        at += ((v + 4 * (steps + 1) + align - 1) & ~(align - 1)) - v
    out = bytearray(struct.pack('<4sI', b'N2RP', 16))
    for t in [tables[0]] + tables:   # pattern 0 and 1: the first
        out += struct.pack('<I', len(t) - 1) + b''.join(struct.pack('<HBB', *e) for e in t)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--no-people', action='store_true', help='leave the nis-<NN>.n2ni as they are')
    ap.add_argument('--no-sound', action='store_true', help='leave nis.big and nis.n2sp as they are')
    args = ap.parse_args()
    cand, fwd, back = candidates(args.game)
    scenes = sorted({c[0] for c in cand})
    groups = en.camera_groups(args.game)
    marks = markers(args.game)
    blob = bytearray(struct.pack('<4sI', b'N2IN', 2))
    blob += struct.pack('<I', len(cand)) + b''.join(struct.pack('<6B2x', *c) for c in cand)
    for table in (fwd, back):
        blob += struct.pack('<I', len(table)) + b''.join(struct.pack('<H2xIH2x', ev, m & 0xffffffff, m >> 32)
                                                         for ev, m in table)
    grids = grid_layouts(args.game)
    blob += struct.pack('<I', len(grids)) + b''.join(struct.pack('<HBx', ev, g) for ev, g in grids)
    only = career_restricted(args.game)
    blob += struct.pack('<I', len(only)) + b''.join(struct.pack('<HBx', ev, st) for ev, st in only)
    blob += struct.pack('<I', len(marks)) + b''.join(struct.pack('<H2B3fH2x', *m) for m in marks)
    flares = flare_map(n2source.executable(args.game))
    index, blocks = [], []
    for n in scenes:
        data = archived(args.game, f'NIS/Scene_IntroNis{n:02d}_BundleB.bun')
        if args.no_people:
            size = (args.out / f'nis-{n:02d}.n2ni').stat().st_size
            stats = {}
        else:
            stats = nis.convert(args.game, n, args.out)
            size = stats.pop('bytes')
            stats.pop('scene')
        cars = en.car_paths(data)
        if len(cars) > 6:
            raise ValueError(f'IntroNis{n:02d}: {len(cars)} cars')
        cams = [en.camera_track(t) for t in groups[nis.h33(f'IntroNis{n:02d}')]]
        try:
            text = archived(args.game, f'NIS/Scene_IntroNis{n:02d}_EvtTrk.txt').decode('latin-1')
        except ValueError:
            text = ''
        evs = en.track_events(text, f'IntroNis{n:02d}')
        girls = flag_girls(data, flares)
        if len(girls) > 1:
            raise ValueError(f'IntroNis{n:02d}: {len(girls)} flag girls with flares')
        index.append((n, girls[0] if girls else None))
        blocks.append(en.scene_block(size, data, cars, cams, evs))
        print(dict(scene=n, bytes=size, cars=len(cars), car_frames=[len(c) for c in cars], cameras=len(cams),
                   events=len(evs), flares=[i for i, _ in girls], times=[round(v, 2) for v in en.scene_times(data)], **stats))
    blob += struct.pack('<I', len(scenes))
    at = len(blob) + 64 * len(scenes)
    for (n, girl), block in zip(index, blocks):
        i, pts = girl if girl else (0, (0.,) * 12)
        blob += struct.pack('<2B2x3I12f', n, 1 if girl else 0, at, len(block), i, *pts)
        at += len(block)
    blob += b''.join(blocks)
    (args.out / 'intro.n2in').write_bytes(bytes(blob))
    from convert_audio import SOUND_ANCHORS
    (args.out / 'revpat.n2rp').write_bytes(rev_patterns(n2exe.of(args.game, SOUND_ANCHORS)))
    if not args.no_sound:
        ends = {(c[0], c[1]) for c in en.candidates(args.game, {nis.h33(f'EndingNis{k:02d}'): k for k in en.SCENES})[0]}
        print(dict(sound=nis_audio(args.game, args.out, cand, ends)))
    print(dict(candidates=len(cand), forward=len(fwd), backward=len(back), grids=len(grids), restricted=len(only),
               markers=len(marks), scenes=len(scenes), bytes=len(blob)))


if __name__ == '__main__':
    sys.exit(main())
