#!/usr/bin/env python3
"""The city's sound by region, as the game's: envsound.n2es.

What the game does (GameCube Speed.elf, the same code as the Xbox's):
- Ambience: the "Ambience" map of L4RA (parammaps.py) gives a zone (0-20)
  at the car's place; gnAmbientZoneCrossMap (31 pairs of ints, the Xbox
  default.xbe file offset 0x3844f0) turns a zone into a Pathfinder event of
  MUSICSFX.MPF, which starts a loop of MUSICSFX.MUS (convert_audio.py,
  pathfinder_ambience). It plays on the music's own stream, only while no
  song does (the in-game music at 0): a new zone fades the loop out, then
  starts the new one (EAXS_PFController::SetupAmbientStreams). Zones 22-30
  are the shops' (the frontend).
- Reverb: the "Reverb" map gives, per region, a reverb type (int) and an
  offset in hundredths of a dB (int; a float between them unused).
  ReverbZoneCrossMap is the identity over the 12 types; a type above 11 is
  9. The 12 types are the presets of SOUND\\FXEDIT (the names' pointer array
  of the default.xbe, found from the first), each a small graph of delay
  lines, low-pass filters and gains, and a row of g_REVERBFXMODULES (the
  default.xbe file offset 0x38b058: wet and dry levels in hundredths of a
  dB for the engine's and the samples' sends, the time to fade the old
  preset out and the new one in). Entering a tunnel zone overrides it.
- Tunnels: the trigger pack's zones (chunk 0x3414A, convert_career.py) of
  types 5 (Simple_Tunnel), 6 (Simple_Tunnel_Sml) and 7 and 20 (Garage),
  polygons with a ceiling: the car is in one when inside it and not above
  its ceiling (SFXCTL_Tunnel::UpdateParams).
- Wind: the "Wind" map (intensity, direction in degrees) only leans the
  rain (Rain::Wind), blended over 50 m.

envsound.n2es: 'N2ES', u32 version 1; u32 layers (3: Ambience, Reverb,
Wind) each as parammaps.write_layer; u32 zones, s16 music.idx track per
zone (-1 none), padded to 4 bytes; u32 types, each 48 bytes: char name[20],
s16 wet (hundredths of a dB), u16 fade out ms, u16 fade in ms, u16 low-pass
Hz, u8 output gain (/128), u8 delay lines, u16 0, 4 x (u16 ms, u8 feedback /256,
u8 side: 0 both, 1 left, 2 right); u32 tunnels, each u8 type, u8 corners,
u16 0, f32 ceiling, f32 x min, y min, x max, y max, then the corners f32
x, y (the game's axes).

A preset's graph as the port plays it: the delay lines of its room part (the
modules after the input stage that every preset shares: a 50 ms delay and
a 5 kHz low-pass), side by side, each fed back on itself, after the room's
first low-pass.
"""
import argparse
from pathlib import Path
import struct

from n2formats import archived
import convert_career
import parammaps
import n2source

ROOT = Path(__file__).resolve().parents[1]
LAYERS = ('Ambience', 'Reverb', 'Wind')
# Retail Speed.elf filtermodsizetable (8037f8e0), used by MIX_initreverb:
# type 5 is an eight-byte resonant filter, not a four-byte module followed
# by a delay. Its parameter bytes can themselves start with 0x07.
FX_SIZE = (4, 8, 4, 4, 8, 8, 4, 8, 4, 4)
TUNNELS = (5, 6, 7, 20)


def exe_of(game):
    """The executable with the tables the PS2 fills in code run (the
    audio's initialiser holds the zones and the reverb rows too)."""
    import n2exe
    from convert_audio import SOUND_ANCHORS
    return n2exe.of(game, SOUND_ANCHORS)


def xbe_tables(xbe):
    """(ambient zone -> event, the 12 reverb rows, the 12 preset names)"""
    import n2exe
    exe = xbe if isinstance(xbe, n2exe.Exe) else n2exe.Exe(xbe)
    b, va, raw = exe.b, exe.va, exe.raw
    # gnAmbientZoneCrossMap: 31 pairs (event, event + 1), or (-1, -1)
    zones = None
    for o in range(0, len(b) - 248, 4):
        w = struct.unpack_from('<62i', b, o)
        if w[0] == 27 and all((w[2 * k] == -1 == w[2 * k + 1]) or (27 <= w[2 * k] < 80 and w[2 * k + 1] == w[2 * k] + 1)
                              for k in range(31)):
            zones = [w[2 * k] for k in range(31)]
            break
    # g_REVERBFXMODULES: 12 rows of (wet, dry, wet, dry, fade out, fade in)
    rows = None
    for o in range(0, len(b) - 288, 4):
        w = struct.unpack_from('<72i', b, o)
        if all(-10000 <= w[6 * k] <= 0 and w[6 * k] == w[6 * k + 2] and w[6 * k + 1] == 0 == w[6 * k + 3]
               and 0 < w[6 * k + 4] <= 2000 and 0 < w[6 * k + 5] <= 2000 for k in range(12)):
            rows = [w[6 * k:6 * k + 6] for k in range(12)]
            break
    first = b.index(b'sound\\fxedit\\Car_Show_Sml.fx\0')
    ref = b.index(struct.pack('<I', va(first)))
    names = []
    for k in range(12):
        o = raw(struct.unpack_from('<I', b, ref + 4 * k)[0])
        names.append(b[o:b.index(b'\0', o)].decode())
    if not zones or not rows:
        raise ValueError(f'ambience or reverb table not found in {exe.path.name}')
    return zones, rows, names


def fx_modules(data):
    """Read the six counted variants, without mistaking parameters for modules.

    MIX_initreverb copies the 0x38-byte header, walks each variant's module
    count using filtermodsizetable, then skips its counted 12-byte edges.
    The original selects a populated variant by its mixer output count;
    these groups are alternatives, not simultaneous input/room buses.
    Connections are not recognisable by a byte-pattern heuristic: their
    ports and padding vary between the retail presets.
    """
    if len(data) < 0x38 or data[:4] != b'FX\x0b\0':
        raise ValueError('invalid FXEDIT header')
    if struct.unpack_from('>I', data, 4)[0] != len(data):
        raise ValueError('FXEDIT size differs from header')
    groups, o = [], 0x38
    for variant in range(6):
        count = struct.unpack_from('>I', data, 8 + variant * 4)[0]
        edges = struct.unpack_from('>I', data, 0x20 + variant * 4)[0]
        modules = []
        if count > (len(data) - o) // 4:
            raise ValueError('FXEDIT module count exceeds file')
        for _ in range(count):
            t = data[o]
            if t >= len(FX_SIZE):
                raise ValueError(f'unknown FXEDIT module {t} at {o:#x}')
            end = o + FX_SIZE[t]
            if end > len(data):
                raise ValueError('truncated FXEDIT module')
            modules.append((t, data[o:end]))
            o = end
        end = o + edges * 12
        if end > len(data):
            raise ValueError('FXEDIT connection count exceeds file')
        for at in range(o, end, 12):
            source, target = struct.unpack_from('>2I', data, at)
            if source >= count or target >= count:
                raise ValueError('FXEDIT connection references missing module')
        groups.append(modules)
        o = end
    if o != len(data):
        raise ValueError('unconsumed FXEDIT bytes')
    return groups


def preset(data):
    """A SOUND\\FXEDIT preset as the port plays it (the module's doc): (low-pass
    Hz, output gain, [(ms, feedback, side)]). Retain the port's existing
    projection of the last populated variant into a stereo room. This
    does not implement the original's output-count selection or complete
    FXEDIT DSP graph, including resonant filters.
    """
    groups = [group for group in fx_modules(data) if group]
    if not groups:
        raise ValueError('FXEDIT preset has no modules')
    mods = []
    for t, r in groups[-1]:
        if t == 7:
            mods.append(('delay', r[1], *struct.unpack_from('>2H', r, 2)))
        elif t == 1:
            mods.append(('lowpass', r[1], *struct.unpack_from('>2H', r, 4)))
        elif t == 6:
            mods.append(('gain', r[1], struct.unpack_from('>H', r, 2)[0], 0))
    room = mods
    lows = [m for m in room if m[0] == 'lowpass' and m[2]]
    gains = [m[2] for m in room if m[0] == 'gain']
    hz = lows[0][2] if lows else 5000
    gain = (lows[0][3] if lows else 256) / 256 * (gains[0] / 256 if gains else 1)
    lines = [(m[3], min(255, m[2]), m[1]) for m in room if m[0] == 'delay'][:4]
    return hz, gain, lines


def tunnels(game):
    leaf, at, out = convert_career.trigger_pack(game), 0, []
    while at + 0x44 <= len(leaf):
        typ, = struct.unpack_from('<I', leaf, at)
        n, step = struct.unpack_from('<HH', leaf, at + 0x40)
        if step < 0x44:
            break
        if typ in TUNNELS:
            ceiling, = struct.unpack_from('<f', leaf, at + 0x14)
            box = struct.unpack_from('<4f', leaf, at + 0x20)
            pts = [struct.unpack_from('<2f', leaf, at + 0x44 + 8 * i) for i in range(n)]
            out.append((typ, ceiling, box, pts))
        at += step
    return out


def build(game):
    from convert_audio import pathfinder_ambience
    zones, rows, names = xbe_tables(exe_of(game))
    streams = pathfinder_ambience(game)
    maps = parammaps.maps(game)
    out = struct.pack('<4s2I', b'N2ES', 1, len(LAYERS))
    for name in LAYERS:
        out += parammaps.write_layer(maps[parammaps.name_hash(name)])
    track = [streams.get(e, -1) if e >= 0 else -1 for e in zones]
    out += struct.pack('<I', len(track)) + struct.pack(f'<{len(track)}h', *track) + b'\0' * (2 * (len(track) & 1))
    out += struct.pack('<I', len(names))
    report_types = []
    for name, row in zip(names, rows):
        hz, gain, lines = preset(archived(game, name.replace('\\', '/').upper()))
        rec = struct.pack('<20sh3H2B2x', Path(name.replace('\\', '/')).stem.encode()[:19], row[0], row[4], row[5], hz,
                          min(255, round(gain * 128)), len(lines))
        rec += b''.join(struct.pack('<H2B', *ln) for ln in lines) + b'\0' * (4 * (4 - len(lines)))
        assert len(rec) == 48
        out += rec
        report_types.append(dict(name=Path(name.replace('\\', '/')).stem, wet_db=row[0] / 100, fade=row[4:6], lowpass=hz,
                                 gain=round(gain, 2), lines=lines))
    tun = tunnels(game)
    out += struct.pack('<I', len(tun))
    for typ, ceiling, box, pts in tun:
        out += struct.pack('<2BH5f', typ, len(pts), 0, ceiling, *box) + b''.join(struct.pack('<2f', *p) for p in pts)
    report = dict(zones={k: t for k, t in enumerate(track)}, types=report_types,
                  tunnels={t: sum(1 for x in tun if x[0] == t) for t in TUNNELS})
    return out, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    a = ap.parse_args()
    blob, report = build(a.game)
    target = a.out / 'envsound.n2es'
    if target.is_symlink():
        target.unlink()
    target.write_bytes(blob)
    print(f'envsound.n2es: {len(blob)} bytes, tunnels {report["tunnels"]}, zones -> tracks {report["zones"]}')
    for t in report['types']:
        print('  ', t)


if __name__ == '__main__':
    main()
