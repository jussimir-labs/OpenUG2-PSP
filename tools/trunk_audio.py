"""The trunk's audio components (Car Specialties Shop > Trunk Audio > Audio
Components) -> trunk.n2tc.

The original lists, once a trunk layout is installed, its "Trunk Slot n"
(IceSelectionScreen::Setup, its modes 4 and 5; the slots TRUNK_AUDIO_COMP_0
to 11, 0x23-0x2e, RideInfo::TrunkAudioSlotAvailable) and in each the
components of the slot's size: the parts table's kinds 61 (small: the 8"
subs, the 250 W amps, the 5.5" LCDs, the small NOS bottles), 62 (medium:
10" and 12" subs, 500 W amps...) and 63 (large: 15" subs, 1000 W amps, the
dual 7" LCDs), each with its "NO COMPONENTS" mount first (level 0). The
slots' sizes, in order: small, small, medium, medium, large, large, then
small x 4, medium x 2 (preset_dump.SLOT_KIND). Each component's model is
the shared CARS/AUDIO geometry (AUDIO_COMP_*_A; its _PAINT_A twin, the same
shape in the body's skin, is not drawn); it sits at the layout's
MARKER_AUDIO_COMP_<slot> socket (the <CAR>_KIT0n_TRUNK_AUDIO_A part).

trunk.n2tc: 'N2TC', u32 version 1, u32 components, u32 cars; components x
(u8 size 0-2, u8 level 0-3, u16 pad, u32 the part's key, char title[32],
u32 offset, u32 bytes of its N2PV pack in this file); cars x (char
model[16], u8 slots of layouts 1-3 (KIT01-KIT03: the markers the layout
has, 0 none), u8 pad, f32 [3][12][12]: per layout and slot the marker in
GU space, the 3x3 rotation's rows then the translation); then the packs
(16-byte aligned), each one component's meshes in its own space (GU),
flagged as the trunk's audio (F_INSIDE_TRUNK | F_AUDIO: drawn with the lid
open), the textures CLUT8 at 128.
"""
import os
import struct
from pathlib import Path

import convert_car as cc
import n2feng as fe
import preset_dump as pd
from n2formats import FlatTPK, SlotTPK, archived, parts

SLOTS = 12
SIZE_OF_KIND = {61: 0, 62: 1, 63: 2}
SLOT_SIZE = (0, 0, 1, 1, 2, 2, 0, 0, 0, 0, 1, 1)   # TRUNK_AUDIO_COMP_0-11 (preset_dump.SLOT_KIND 61-63)
assert [SIZE_OF_KIND[pd.SLOT_KIND[35 + s]] for s in range(SLOTS)] == list(SLOT_SIZE)
MARKER = 'MARKER_AUDIO_COMP_%d'


class _Textures(cc.Textures):
    """The shared car textures (CARS/AUDIO has no TPK of its own)."""
    def __init__(self, game):
        self.sources = [SlotTPK(archived(game, 'CARS/TEXTURES.BIN')),
                        SlotTPK(archived(game, 'CARS/WHEELS/TEXTURES.BIN')),
                        FlatTPK(archived(game, 'GLOBAL/GLOBALB.BUN'))]
        self.car_tpk, self.car_names = self.sources[0], {}


def components(game):
    """[(size, level, key, title, model)] in the parts table's order, the
    kinds 61-63 (GetIcePartsList walks it as it is)."""
    table = pd.PartsTable(game)
    out = []
    for q in table.parts:
        if q['kind'] not in SIZE_OF_KIND:
            continue
        names = table.model_names(q)
        model = names[0] if names else ''
        model = 'AUDIO' + model[model.index('_COMP_'):] if '_COMP_' in model else model
        out.append((SIZE_OF_KIND[q['kind']], q['level'], q['key'], q['title'], model))
    return out


def gu_marker(m):
    """A socket's column-major matrix (Xbox: x forward, y lateral, z up) in
    GU space (x lateral, y up, z forward: convert_car's cyclic permutation):
    the rotation's rows, then the translation."""
    r = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]]   # r[i][j]: row i, column j
    t = (m[12], m[13], m[14])
    p = (1, 2, 0)   # GU axis a is Xbox axis p[a]
    rows = [r[p[a]][p[b]] for a in range(3) for b in range(3)]
    return rows + [t[p[a]] for a in range(3)]


def pack_of(game, textures, part):
    b = cc.Builder(textures, cc.hexcolor('808080'), None, {}, ())
    b.slot = 0
    b.add(part, cc.identity)
    meshes = [(v, i, ref, colour, flags | cc.F_INSIDE_TRUNK | cc.F_AUDIO, uv) for v, i, ref, colour, flags, uv in b.batched()]
    texdata = {}
    for ref in {m[2] for m in meshes if m[2] is not None}:
        if ref == 'SKIN':   # the body's skin (the car's own, as convert_garage's options)
            texdata[ref] = (16, 8, bytes(1024), bytes(128), 1.0)
            continue
        tex = textures.get(ref)
        if tex is None:
            raise ValueError(f'{part.name}: texture {ref} missing')
        texdata[ref] = cc.to_clut8(tex, 128)
    tmp = Path(os.environ.get('TMPDIR', '/tmp')) / f'trunk-comp-{os.getpid()}.n2pv'
    cc.save_pack(tmp, meshes, texdata, 0)
    blob = tmp.read_bytes()
    tmp.unlink()
    return blob


def layouts(game, car):
    """Per layout KIT01-KIT03: its markers' GU matrices, slot by slot, as far
    as the layout has them in order (none: [])."""
    data = archived(game, f'CARS/{car}/GEOMETRY.BIN')
    out = []
    for k in (1, 2, 3):
        name = f'{car}_KIT{k:02d}_TRUNK_AUDIO_A'[:27]   # (names cut at 27 characters: the lit piece and the UNLIT
        # one of IMPREZAWRX_KIT01_TRUNK_AUDI share it; the markers are the lit one's)
        part = next((p for p in parts(data, lambda n, name=name: n == name) if fe.bin_hash(MARKER % 0) in p.sockets), None)
        marks = []
        if part is not None:
            for s in range(SLOTS):
                m = part.sockets.get(fe.bin_hash(MARKER % s))
                if m is None:
                    break
                marks.append(gu_marker(m))
        out.append(marks)
    return out


def write(game, out, cars):
    comps = components(game)
    geometry = {p.name: p for p in parts(archived(game, 'CARS/AUDIO/GEOMETRY.BIN'))}
    textures = _Textures(game)
    packs, missing = [], []
    for size, level, key, title, model in comps:
        part = geometry.get(model[:27])   # (names hold 27 characters)
        if part is None:
            missing.append(model)
            packs.append(b'')
            continue
        packs.append(pack_of(game, textures, part))
    car_rows = []
    for car in cars:
        try:
            lay = layouts(game, car)
        except ValueError:
            lay = [[], [], []]
        row = struct.pack('<16s3Bx', car.encode()[:15], *(len(m) for m in lay))
        for marks in lay:
            for s in range(SLOTS):
                row += struct.pack('<12f', *(marks[s] if s < len(marks) else [0.0] * 12))
        car_rows.append(row)
    head = struct.pack('<4s3I', b'N2TC', 1, len(comps), len(cars))
    at = len(head) + 48 * len(comps) + sum(len(r) for r in car_rows)
    at += -at % 16
    table, data = b'', b''
    for (size, level, key, title, model), blob in zip(comps, packs):
        table += struct.pack('<2BHI32s2I', size, level, 0, key, title.encode('latin-1')[:31], at + len(data) if blob else 0, len(blob))
        data += blob + bytes(-len(blob) % 16)
    body = head + table + b''.join(car_rows)
    (out / 'trunk.n2tc').write_bytes(body + bytes(-len(body) % 16) + data)
    return dict(components=len(comps), cars=len(cars), missing=missing, bytes=len(body) + (-len(body) % 16) + len(data))
