#!/usr/bin/env python3
"""Xbox NFSU2 data -> tuned car preset -> PSP GU pack (N2PV v3).

Python stdlib only. Format decoding lives in n2formats.py; this file picks
the parts named by a preset, composites the body skin (paint + vinyl +
panel lines), mounts library rims and the spoiler, and writes CLUT8 textures
and float vertices the PSP draws directly.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import struct

import n2feng
from n2formats import *  # noqa: F401,F403  (tests import names through here)
from n2formats import (FlatTPK, SlotTPK, archived, car_record, envmap_faces,
                       game_hash, lod_family, parts)

ROOT = Path(__file__).resolve().parents[1]
CAR_IDS = {'240SX': 0, '350Z': 1}

# Material hashes (game_hash of the shader name; unnamed ones by usage).
CARSKIN = 0xd6d6080a
SKIRTSKIN = 0xa7366ae6        # unnamed; side skirts share the body skin UVs
CARBON = 0xb57aef2e           # unnamed; always paired with CARBONFIBRE texture
CHROME, MAGCHROME = 0x54949afd, 0xfd102a92
EXHAUST_TIP = 0x3b3a1f8d   # the shop's exhaust tips' light material: no diffuse, all reflection, as CHROME
METALS = {0x22719fa9, 0x2e65e067, 0x12049251, 0xf0a43b67}  # MAGSILVER ALUMINUM MAGGUNMETAL BRAKEDISC
MATTE = {0x78743ca1, 0x0fedee40, 0x12c9453c, 0x3ed70c43, 0x52bf4c34, 0x2787edab, 0x010cb64a}
WINDOW_TINTS = {1: (20, 30, 40, 200), 2: (14, 20, 26, 230), 3: (8, 10, 12, 248)}   # = the viewer's GARAGE_TINTS (ABGR there)
GLASS = {0x471a1dca: (28, 40, 50, 165),   # WINDSHIELD
         0x1ff8c329: (28, 40, 50, 165),   # TRAFFICWINDOWS
         0xa6348ee3: (210, 220, 230, 45), # HEADLIGHTGLASS
         0xd79597d6: (200, 40, 40, 120),  # BRAKELIGHTGLASS
         0x33a26cb6: (230, 230, 230, 60)} # CLEARPLASTIC
HEADLIGHT, BRAKELIGHT, LENS = 0x9c645529, 0x05bc3a3c, 0xf7fc7674
RUBBER, CALIPER = 0x78743ca1, 0xd6640dff
CARBONFIBRE_TEX = 0x721aff7c

# Pack v3 mesh flags, mirrored in psp-viewer/main.c.
F_GLASS, F_PAINT, F_CHROME, F_CARBON, F_EMISSIVE, F_MATTE, F_METAL = 1, 2, 4, 8, 16, 32, 64
# Wheel/brake meshes of GLOBALB wheel k (bits 8-9): kept apart and moved by
# that wheel's suspension compression at run time, spun about its axle and
# (front) steered; F_WHEEL_FIXED parts (the brake caliper) do not spin.
F_WHEEL = 128
F_WHEEL_FIXED = 0x400
# Customisation slot of a mesh (bits 12-15): what a garage option replaces
# (tools/convert_garage.py packs, merged on the PSP over these).
SLOT_SHIFT = 12
SLOT_KIT, SLOT_HOOD, SLOT_RIMS, SLOT_SPOILER, SLOT_HEADLIGHT, SLOT_BRAKELIGHT = 1, 2, 3, 4, 5, 6
SLOT_REAR_BUMPER, SLOT_SKIRT = 7, 15   # the kit's other pieces (SLOT_KIT its front bumper), each bought on its own as the PS2's
SLOT_ROOF, SLOT_EXHAUST = 8, 9   # the roof scoop and the exhaust tips (8-14 are the decals' only on F_DECAL meshes)
SLOT_BODY, SLOT_DOOR, SLOT_TRUNK, SLOT_DOOR_RIGHT = 10, 12, 13, 14   # the body, its doors (a wide body kit's (KITWnn) pieces; the left's, the right's), the trunk lid: the carbon fibre's, the garage opens them
SLOT_MIRROR = 11   # the side mirrors (the cars' own where they are parts apart, the CARS/MIRRORS_* shop's)
MIRROR_SOCKETS = ('MIRROR_BODY_LEFT', 'MIRROR_BODY_RIGHT')
# what the garage shows with the hood or the trunk lid open (the detailed
# pack alone, preset 'interior'): drawn only then (psp-viewer/main.c)
F_INSIDE_HOOD, F_INSIDE_TRUNK, F_AUDIO = 1 << 24, 1 << 25, 1 << 26   # the engine bay; the trunk; its audio (a layout's)
F_RIGHT = 1 << 27   # the right side mirror's (they break apart: psp-viewer/main.c)


def inside_flags(rest, audio_owner, owner):
    """An inside piece's flags (rest: the name past CAR_ and its owner), 0 when it is not one."""
    rest = re.sub(r'_[ABCD]$', '', rest)
    if owner == 'KIT00' and (rest.startswith('ENGINE') or rest == 'HOOD_UNDER'):
        return F_INSIDE_HOOD
    if owner == 'KIT00' and rest == 'TRUNK_UNDER':
        return F_INSIDE_TRUNK
    if owner == audio_owner and rest.startswith('TRUNK_AUDIO'):
        return F_INSIDE_TRUNK | F_AUDIO
    return 0   # every car's (by name: bin_hash), whatever the mirrors' kind

# Hidden or interior-only families of a closed car in a showroom.
HIDDEN = ('FULLROOF', 'UNDER', 'AUDIO', 'DOOR_PANEL', 'DOOR_SILL', 'ENGINE', 'DECAL')   # (the exhaust tips show from the chase camera)


def rgba_u32(c):
    r, g, b, a = c
    return r | (g << 8) | (b << 16) | (a << 24)


def hexcolor(s):
    s = s.lstrip('#')
    if len(s) != 6:
        raise ValueError('cor precisa de 6 dígitos hex: ' + s)
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


# --------------------------------------------------------------- part choice

def slot_of(rest):
    """Customisation slot of a car part name (without CAR_ and owner token).
    Names stop at 27 characters: IMPREZAWRX_KIT01_FRONT_BUMP is a bumper."""
    for key in ('FRONT_BUMPER', 'REAR_BUMPER', 'SKIRT', 'HEADLIGHT', 'BRAKELIGHT', 'BODY', 'DOOR'):
        if rest.startswith(key) or (len(rest) >= 6 and key.startswith(rest)):
            return key
    if rest.startswith('HOOD') and not rest.startswith('HOOD_UNDER'):
        return 'HOOD'
    return None


def is_mirror(rest):
    """A car's own side mirror (LEFT_SIDE_MIRROR, cut at 27 characters)."""
    return rest.startswith('LEFT_SIDE_MIR') or rest.startswith('RIGHT_SIDE_MIR')


def choose_car_parts(all_parts, car, preset):
    """Return {family: [Part,...]} for the preset. KIT00/BASE is the stock car;
    KITnn overrides bumpers/skirts, KITWnn body/doors, STYLEnn hood/lights."""
    kit = preset.get('kit', 'KIT00')
    owners = {'FRONT_BUMPER': kit, 'REAR_BUMPER': preset.get('rear_bumper', kit), 'SKIRT': preset.get('skirt', kit),
              'BODY': preset.get('widebody', 'KIT00'), 'DOOR': preset.get('widebody', 'KIT00'),
              'HOOD': preset.get('hood', 'KIT00'), 'HEADLIGHT': preset.get('headlight', 'KIT00'),
              'BRAKELIGHT': preset.get('brakelight', 'KIT00')}
    # A wide body kit is the whole shell, bumpers and skirts its own: under
    # it the game turns the kit's bumpers and skirts off (the RideInfo's
    # enabled parts), as the KITW02 body holds the rear's exhaust markers.
    shell = ('FRONT_BUMPER', 'REAR_BUMPER', 'SKIRT') if preset.get('widebody') else ()
    families = collections.defaultdict(list)
    for part in all_parts:
        m = re.match(re.escape(car) + r'_(BASE|KITW?\d+|STYLE\d+)_?(.*)', part.name)
        if not m:
            continue
        owner, rest = m.groups()
        if preset.get('interior') and inside_flags(rest, preset.get('trunk_audio', 'KIT00'), owner):
            families[lod_family(part.name)].append(part)   # the engine bay and the trunk, the garage's
            continue
        if any(s in part.name for s in HIDDEN):
            continue
        if preset.get('rims') and rest.startswith('FRONT_WH'):
            continue
        if preset.get('mirror') and is_mirror(rest):   # the shop's in their place
            continue
        slot = slot_of(rest)
        if slot in shell:
            continue
        wanted = owners.get(slot, 'KIT00')
        if owner == 'BASE' or owner == wanted:
            families[lod_family(part.name)].append(part)
    # A requested option missing for this car is an error, not a silent stock.
    for slot, owner in owners.items():
        if slot in shell:
            continue
        head = f'{car}_{owner}_'
        if owner != 'KIT00' and not any(f.startswith(head) and slot_of(f[len(head):]) == slot for f in families):
            raise ValueError(f'{car}: {owner} não tem peça {slot}')
    return families


def pick_lod(choices, lod):
    choices = sorted(choices, key=lambda p: p.triangles, reverse=True)
    return choices[min(lod, len(choices) - 1)]


def has_lod(choices, lod):
    """The family has its own level lod (levels by size: a name cut at 27
    characters keeps no letter)."""
    return len(choices) > lod


def lod_letters(choices):
    """level -> part when every part of the family ends in its level's
    letter (_A to _D), else None (a name cut at 27 characters)."""
    out = {}
    for p in choices:
        if len(p.name) < 3 or p.name[-2] != '_' or p.name[-1] not in 'ABCD':
            return None
        out['ABCD'.index(p.name[-1])] = p
    return out if len(out) == len(choices) else None


def far_has(choices, lod):
    """has_lod for the street's far levels, by the letters where the names
    keep them: a D can have more triangles than its C (the 206's body: 216
    against 174, its C only the sides), and by size the two swap."""
    letters = lod_letters(choices)
    return lod in letters if letters is not None else has_lod(choices, lod)


def far_pick(choices, lod):
    """pick_lod for the far levels: the level's part by its letter (past the
    family's last, its last), by size where the names lost the letters."""
    letters = lod_letters(choices)
    if letters is None:
        return pick_lod(choices, lod)
    return letters[max(k for k in letters if k <= lod)] if any(k <= lod for k in letters) else pick_lod(choices, lod)


# ------------------------------------------------------------------- textures

def box_downscale(rgba, w, h, limit):
    while max(w, h) > limit:
        nw, nh = max(1, w // 2), max(1, h // 2)
        out = bytearray(nw * nh * 4)
        for y in range(nh):
            for x in range(nw):
                p = [(min(h-1, y*2+dy)*w + min(w-1, x*2+dx))*4 for dy in (0, 1) for dx in (0, 1)]
                out[(y*nw+x)*4:(y*nw+x)*4+4] = bytes(sum(rgba[q+c] for q in p)//4 for c in range(4))
        rgba, w, h = out, nw, nh
    return rgba, w, h


def quantize(rgba):
    """Median cut to <=256 RGBA colours. Returns (palette bytes, indices)."""
    counts = collections.Counter(bytes(rgba[i:i+4]) for i in range(0, len(rgba), 4))
    colours = list(counts)
    if len(colours) <= 256:
        index = {c: i for i, c in enumerate(colours)}
        pal = b''.join(colours) + bytes(4 * (256 - len(colours)))
        return pal, bytes(index[bytes(rgba[i:i+4])] for i in range(0, len(rgba), 4))
    boxes = [colours]
    while len(boxes) < 256:
        def score(box):
            if len(box) < 2:
                return -1
            return max(max(c[k] for c in box) - min(c[k] for c in box) for k in range(4)) * sum(counts[c] for c in box)
        i = max(range(len(boxes)), key=lambda j: score(boxes[j]))
        box = boxes[i]
        if score(box) <= 0:
            break
        k = max(range(4), key=lambda k: max(c[k] for c in box) - min(c[k] for c in box))
        box.sort(key=lambda c: c[k])
        half, acc, cut = sum(counts[c] for c in box) / 2, 0, 1
        for j, c in enumerate(box):
            acc += counts[c]
            if acc >= half:
                cut = max(1, min(len(box) - 1, j))
                break
        boxes[i:i+1] = [box[:cut], box[cut:]]
    pal, index = bytearray(), {}
    for i, box in enumerate(boxes):
        n = sum(counts[c] for c in box)
        pal += bytes(round(sum(c[k] * counts[c] for c in box) / n) for k in range(4))
        for c in box:
            index[c] = i
    pal += bytes(4 * (256 - len(boxes)))
    return bytes(pal), bytes(index[bytes(rgba[i:i+4])] for i in range(0, len(rgba), 4))


def psp_swizzle(linear, row_bytes, h):
    """PSP GE swizzle: 16-byte x 8-row blocks."""
    out = bytearray()
    for y in range(0, h, 8):
        for x in range(0, row_bytes, 16):
            for row in range(8):
                pos = (y + row) * row_bytes + x
                out.extend(linear[pos:pos+16])
    return bytes(out)


def carbon_tile(tex):
    """CARBONFIBRE repeats every 128 rows but not across. Its weave is a
    very fine, high-contrast pattern that the original's mipmaps average to
    an almost flat dark grey; the PSP draws without mipmaps, so box-filter
    the full-width band to 128x32 here (drawn with v repeated 4x)."""
    rgba, w, h = box_downscale(bytearray(tex.rgba()[:tex.w * 128 * 4]), tex.w, 128, 128)
    pal, idx = quantize(rgba)
    return w, h, pal, psp_swizzle(idx, w, h), 4.0


def to_clut8(tex, limit):
    """Texture -> (w, h, palette RGBA 1024 B, swizzled indices). P8 sources
    keep their palette (nearest downscale); others are box-filtered and
    median-cut. PSP T8 needs >=16 texels per row."""
    w, h = tex.w, tex.h
    if tex.palette is not None:
        idx = tex.pixels
        while max(w, h) > limit:
            nw, nh = max(1, w // 2), max(1, h // 2)
            idx = bytes(idx[(y*2)*w + x*2] for y in range(nh) for x in range(nw))
            w, h = nw, nh
        pal = tex.palette
    else:
        rgba, w, h = box_downscale(tex.rgba(), w, h, limit)
        pal, idx = quantize(rgba)
    while w < 16 or h < 8:
        sx, sy = 2 if w < 16 else 1, 2 if h < 8 else 1
        idx = bytes(idx[(y//sy)*w + x//sx] for y in range(h*sy) for x in range(w*sx))
        w, h = w * sx, h * sy
    return w, h, pal, psp_swizzle(idx, w, h), 1.0


class Textures:
    """All texture sources by key: car TPK, shared car TPK, rims, GLOBALB."""
    def __init__(self, game, car):
        self.sources = [SlotTPK(archived(game, f'CARS/{car}/TEXTURES.BIN')),
                        SlotTPK(archived(game, 'CARS/TEXTURES.BIN')),
                        SlotTPK(archived(game, 'CARS/WHEELS/TEXTURES.BIN'))]
        glob = archived(game, 'GLOBAL/GLOBALB.BUN')
        self.sources.append(FlatTPK(glob))
        self.car_tpk = self.sources[0]
        self.car_names = {self.car_tpk.name(k): k for k in self.car_tpk.slots}

    def get(self, key):
        for s in self.sources:
            t = s.get(key)
            if t is not None:
                return t
        return None

    def car(self, name):
        # TPK names hold 23 characters (IMPREZAWRX_DOORLINE_MAS).
        key = self.car_names.get(name) or self.car_names[name[:23]]
        return self.car_tpk.get(key)


# ----------------------------------------------------------------------- skin

def vinyl_index(game, car):
    """name -> key for CARS/<car>/VINYLS.BIN. Names live inside each
    compressed slot, so the index is built once and cached locally."""
    cache = ROOT / '.tools/cache' / f'{car}-vinyls.json'
    store = SlotTPK(archived(game, f'CARS/{car}/VINYLS.BIN'))
    if cache.exists():
        index = json.loads(cache.read_text())
    else:
        index = {store.name(k): k for k in store.slots}
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps(index, indent=0, sort_keys=True))
    return store, index


def mask_name(name):
    """A vinyl's mask texture: <name>_MASK, cut at the TPK's 23 characters."""
    return (name + '_MASK')[:23]


def composite_skin(textures, game, car, preset, size=256):
    """Paint, then vinyl (RGB channels weight three player colours, the _MASK
    gives coverage), then DOORLINE panel lines/details through its mask."""
    paint = hexcolor(preset.get('paint', '2a8a20'))
    w = h = 512
    out = bytearray(bytes((*paint, 255)) * (w * h))
    vinyl = preset.get('vinyl')
    if vinyl:
        store, index = vinyl_index(game, car)
        name = f'{car}_{vinyl}'
        if name not in index or mask_name(name) not in index:
            if not preset.get('vinyl_optional'):
                raise ValueError('vinil inexistente: ' + name)
            vinyl = None
    if vinyl:
        design = store.get(index[name]).rgba()
        mask = store.get(index[mask_name(name)]).rgba()
        cols = [hexcolor(c) for c in preset.get('vinyl_colors', ['5a1a8c', '2a0a40', 'b070ff'])]
        remap = preset.get('vinyl_remap', True)   # the part's REMAP: 0 keeps the art's own colours
        for i in range(0, w * h * 4, 4):
            a = mask[i]
            if not a:
                continue
            r, g, b = design[i], design[i+1], design[i+2]
            if not remap:
                for k in range(3):
                    out[i+k] = (out[i+k] * (255 - a) + design[i+k] * a) // 255
                continue
            s = r + g + b
            if not s:
                continue
            for k in range(3):
                v = (r * cols[0][k] + g * cols[1][k] + b * cols[2][k]) // s
                out[i+k] = (out[i+k] * (255 - a) + v * a) // 255
    lines = preset.get('doorline', 'DOORLINE')
    detail = textures.car(f'{car}_{lines}').rgba()
    dmask = textures.car(f'{car}_{lines}_MASK').rgba()
    # Detail regions use mask ~0x80, black panel lines only 0x10-0x30.
    for i in range(0, w * h * 4, 4):
        a = min(255, dmask[i] * 3)
        if a:
            for k in range(3):
                out[i+k] = (out[i+k] * (255 - a) + detail[i+k] * a) // 255
    rgba, w, h = box_downscale(out, w, h, size)
    pal, idx = quantize(rgba)
    return w, h, pal, psp_swizzle(idx, w, h), 1.0, out


# ------------------------------------------------------------------- assembly

class Builder:
    def __init__(self, textures, paint, window=None):
        self.textures, self.paint = textures, paint
        self.window = window      # the windows' tint (RGBA), None: the stock glass
        self.slot = 0             # customisation slot of the parts being added
        self.meshes = []          # (vertex bytes list, indices, texref, colour, flags, uvscale)
        self.missing = set()

    def material(self, mat, tex, library=False, carbon_hood=False):
        """-> (texref or None, RGBA colour, flags, uv scale)."""
        white = (255, 255, 255, 255)
        if mat in (CARSKIN, SKIRTSKIN):
            if carbon_hood and mat == CARSKIN:
                return CARBONFIBRE_TEX, white, F_CARBON, 3.0
            # Body skin keys (<CAR>_SKIN1 and its alias) are composited at run
            # time in retail, so no TPK holds them: those get our composite.
            # CARSKIN slices bound to a real texture (audio boxes) are painted.
            if library or (mat == CARSKIN and self.textures.get(tex) is not None):
                return None, (*self.paint, 255), F_PAINT, 1.0
            return 'SKIN', white, F_PAINT, 1.0
        if mat in GLASS:
            # a window with its own art (the 206's REAR_DEFROSTER: dark, its
            # heater lines in the alpha) is drawn with it, as the PS2's
            if mat == 0x471a1dca and self.window:   # tinted: every window, the rear's too
                return None, self.window, F_GLASS, 1.0
            if mat == 0x471a1dca and self.textures.get(tex) is not None:
                return tex, (255, 255, 255, 255), F_GLASS, 1.0
            return None, GLASS[mat], F_GLASS, 1.0
        if mat in (CARBON, CARBONFIBRE_TEX):   # the latter the shop's carbon mirrors' (MIRROR_*_CF)
            return CARBONFIBRE_TEX, white, F_CARBON, 1.0
        flags = F_CHROME if mat in (CHROME, MAGCHROME, EXHAUST_TIP) else F_METAL if mat in METALS else F_MATTE if mat in MATTE else 0
        if mat == HEADLIGHT:
            flags = F_METAL
        if mat == WINDOW_RIM:   # the rear window's black edge: drawn with the defroster's art it came out pale grey
            return None, (40, 40, 42, 255), F_MATTE, 1.0
        resolved = tex if self.textures.get(tex) is not None else None
        if resolved is None:
            self.missing.add(tex)
            if mat in (BRAKELIGHT, LENS):
                return None, (170, 18, 18, 255), F_EMISSIVE, 1.0
            if flags & F_CHROME:
                return None, (150, 150, 155, 255), flags, 1.0
            if flags & F_METAL:
                return None, (170, 170, 175, 255), flags, 1.0
            if mat == CALIPER:
                return None, (180, 20, 20, 255), 0, 1.0
            return None, (40, 40, 42, 255), F_MATTE, 1.0
        if mat == CALIPER:
            return resolved, (230, 40, 40, 255), 0, 1.0
        return resolved, white, flags, 1.0

    def add(self, part, xform, mirror=False, library=False, carbon_hood=False, wheel=None):
        """xform(x,y,z,nx,ny,nz) -> transformed tuple in Xbox car space;
        wheel = GLOBALB wheel index for tyres, rims and brakes."""
        for ids, tk, mk in part.subs:
            if not ids:
                continue
            texref, colour, flags, uvscale = self.material(mk, tk, library, carbon_hood)
            if wheel is not None:
                flags |= F_WHEEL | (wheel << 8) | (F_WHEEL_FIXED if mk == CALIPER else 0)
            flags |= self.slot << SLOT_SHIFT
            remap = {v: i for i, v in enumerate(sorted(set(ids)))}
            vertices = []
            for vi in remap:
                x, y, z, u, v, nx, ny, nz = part.verts[vi]
                x, y, z, nx, ny, nz = xform(x, y, z, nx, ny, nz)
                # cyclic permutation: Xbox X forward/Y lateral/Z up -> GU x,y,z
                vertices.append(struct.pack('<8f', u, v, ny, nz, nx, y, z, x))
            ii = [remap[i] for i in ids]
            if mirror:
                for j in range(0, len(ii), 3):
                    ii[j+1], ii[j+2] = ii[j+2], ii[j+1]
            self.meshes.append((vertices, ii, texref, rgba_u32(colour), flags, uvscale))

    def batched(self):
        """Merge opaque slices with equal GU state; glass stays separate."""
        grouped, glass = {}, []
        for m in self.meshes:
            vertices, ids, texref, colour, flags, uvscale = m
            if flags & F_GLASS:
                glass.append(m)
                continue
            key = (texref, colour, flags, uvscale)
            g = grouped.setdefault(key, ([], [], texref, colour, flags, uvscale))
            if len(g[0]) + len(vertices) > 65535:
                raise ValueError('batch exceeds 16-bit index capacity')
            base = len(g[0])
            g[0].extend(vertices)
            g[1].extend(i + base for i in ids)
        return list(grouped.values()) + glass


def identity(x, y, z, nx, ny, nz):
    return x, y, z, nx, ny, nz


def matrix_xform(m):
    """Column-major 4x4 socket matrix: v' = M v."""
    def f(x, y, z, nx, ny, nz):
        return (m[0]*x + m[4]*y + m[8]*z + m[12], m[1]*x + m[5]*y + m[9]*z + m[13],
                m[2]*x + m[6]*y + m[10]*z + m[14], m[0]*nx + m[4]*ny + m[8]*nz,
                m[1]*nx + m[5]*ny + m[9]*nz, m[2]*nx + m[6]*ny + m[10]*nz)
    return f


RIM_NATIVE = .03   # a rim within 3% of the axle's tyre radius: drawn at its own size (assemble)


def wheel_xform(mount, mid, scale=1.0, front_flip=True, yscale=None):
    """Wheel source: axle along Y with the face toward +Y. Rotate half a
    turn about Z so the face points outward, then mirror for the right side.
    yscale: the width's own scale (the radius keeps scale)."""
    mx, my, mz = mount[:3]
    side = 1 if my > 0 else -1
    ys = scale if yscale is None else yscale
    def f(x, y, z, nx, ny, nz):
        if front_flip:
            x, y, nx, ny = -x, mid - y, -nx, -ny
        x, y, z = x * scale, y * ys, z * scale
        return x + mx, y * side + my, z + mz, nx, ny * side, nz
    return f, side < 0


SPOILER_SOCKET = 0xc93b73fd
ROOF_SOCKET = 'ROOF_SCOOP'   # the roof scoop's socket (by name: bin_hash)
WINDOW_RIM = 0x3ed70c43   # unnamed; the rear window's edge, paired with REAR_DEFROSTER
PLATE_MARKER = 0xc805f7e0   # REAR_LICENSE_PLATE (the rear bumpers'; no car has a front one)
EXHAUST_MARKERS = (0xbcf8a18b, 0xbd7cf15e)   # the exhaust flames, left and right (the rear bumper's): where the PS2 shows the tips


def assemble(game, preset, lod, strict=False):
    """strict: a far level of the street's (C, D: CarRenderInfo::Render),
    each part at that level or left out, as the original's, whose parts
    have no model there; the wheels keep their last level, but at D, whose
    body is the whole car alone (its wheels boxes in it)."""
    car = preset['car']
    data = archived(game, f'CARS/{car}/GEOMETRY.BIN')
    textures = Textures(game, car)
    paint = hexcolor(preset.get('paint', '2a8a20'))
    wheels = car_record(archived(game, 'GLOBAL/GLOBALB.BUN'), car)
    all_parts = list(parts(data, lambda n: n.startswith(car + '_')))
    wheels = widebody_track(all_parts, car, preset.get('widebody'), wheels, bool(preset.get('rims')))
    families = choose_car_parts(all_parts, car, preset)
    # window_tint: the garage's tint level (1..3, the viewer's GARAGE_TINTS),
    # as the PS2's DDAY_PLAYER_CAR, its cabin unseen behind dark glass
    b = Builder(textures, paint, WINDOW_TINTS.get(preset.get('window_tint', 0)))
    report = []
    hood_owner = preset.get('hood', 'KIT00')
    coarser = preset.get('light_lod', {}) if lod and not strict else {}   # family -> level of detail in the light pack (60 Hz on the street)
    if strict:
        families = {f: c for f, c in families.items() if far_has(c, lod) or ('_FRONT_WH' in f and lod < 3)}   # (D: wheels in the body's box)
    pick = far_pick if strict else pick_lod
    chosen = [pick(choices, coarser.get(family, lod)) for family, choices in sorted(families.items())]
    for (family, choices), part in zip(sorted(families.items()), chosen):
        report.append({'name': part.name, 'triangles': part.triangles})
        b.slot = family_slot(car, family)
        carbon = preset.get('carbon_hood') and re.search(r'_' + hood_owner + r'_HOOD', part.name) is not None
        # Names are truncated at 27 characters (IMPREZAWRX_KIT00_FRONT_WHE).
        if any(s in family for s in ('_FRONT_WH', '_FRONT_BR', '_REAR_BR')):
            first = 0 if '_FRONT_WH' in family or '_FRONT_BR' in family else 2
            mounts = wheels if '_FRONT_WH' in family else wheels[:2] if '_FRONT_BR' in family else wheels[2:]
            mid = (min(v[1] for v in part.verts) + max(v[1] for v in part.verts)) / 2
            for k, mount in enumerate(mounts):
                f, mirror = wheel_xform(mount, mid, front_flip='_FRONT_WH' in family)
                b.add(part, f, mirror, wheel=first + k)
        elif '_EXHAUST' in family:
            if preset.get('exhaust'):   # the shop's tips (CARS/EXHAUST) in place of the car's own
                lib = list(parts(archived(game, 'CARS/EXHAUST/GEOMETRY.BIN'), lambda n: lod_family(n) == preset['exhaust']))
                if not lib:
                    raise ValueError('ponteira inexistente: ' + preset['exhaust'])
                if strict and not far_has(lib, lod):
                    continue
                part = (far_pick if strict else pick_lod)(lib, lod)
                report[-1] = {'name': part.name, 'triangles': part.triangles}
            # the tips at the rear bumper's two exhaust flame markers (the
            # chosen bumper's or wide body's, else any), flush with its face
            # as the PS2's, the right one mirrored: at the model's origin
            # they sat inside the car, unseen. The game's own matrix (a
            # quarter turn about y, then the marker's) comes to the same.
            marks = {}
            for p in chosen + all_parts:
                for k in EXHAUST_MARKERS:
                    if k in p.sockets:
                        marks.setdefault(k, p.sockets[k])
            for k, side in zip(EXHAUST_MARKERS, (1, -1)):
                if k not in marks:
                    continue
                t = marks[k][12:15]   # the flame marker's place; the tip's own axes (the marker's turn the flame's)
                f = matrix_xform((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t[0], t[1], t[2], 1))
                b.add(part, f if side > 0 else (lambda x, y, z, nx, ny, nz, f=f: f(x, -y, z, nx, -ny, nz)), side < 0)
        else:
            first = len(b.meshes)
            b.add(part, identity, carbon_hood=carbon)
            m = re.match(re.escape(car) + r'_(BASE|KITW?\d+|STYLE\d+)_?(.*)', family)
            inside = inside_flags(m.group(2), preset.get('trunk_audio', 'KIT00'), m.group(1)) if m and preset.get('interior') else 0
            if m and is_mirror(m.group(2)) and m.group(2).startswith('RIGHT'):
                inside |= F_RIGHT
            if inside:
                b.meshes[first:] = [x[:4] + (x[4] | inside,) + x[5:] for x in b.meshes[first:]]
    # the rear licence plate (CARS/PLATES' LICENSE_PLATE_STYLE01_A, its
    # LICENSE_PLATE art in CARS/TEXTURES.BIN) at the chosen rear bumper's
    # REAR_LICENSE_PLATE marker's place: it was left out, the plate's
    # recess a dark patch where the PS2 shows the plate
    plate = None if strict else next((p.sockets[PLATE_MARKER] for p in chosen + all_parts if PLATE_MARKER in p.sockets), None)   # (one level)
    if plate is not None:
        b.slot = 0
        for part in parts(archived(game, 'CARS/PLATES/GEOMETRY.BIN'), lambda n: n.startswith('LICENSE_PLATE')):
            t = plate[12:15]   # its place; upright (the marker's turn lays the plate flat inside the bumper)
            x = plate_face(chosen, part, t)
            b.add(part, matrix_xform((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, t[1], t[2], 1)))
            report.append({'name': part.name, 'triangles': part.triangles})
            break
    if preset.get('decals'):
        report += add_decals(b, game, car, preset)
    sockets = {}
    for p in all_parts:
        for k, m in p.sockets.items():
            sockets.setdefault(k, m)
    if preset.get('spoiler'):
        lib = list(parts(archived(game, 'CARS/SPOILER/GEOMETRY.BIN'),
                         lambda n: lod_family(n) == preset['spoiler']))
        if not lib or SPOILER_SOCKET not in sockets:
            raise ValueError('aerofólio ou socket ausente: ' + preset['spoiler'])
        if not strict or far_has(lib, lod):
            part = (far_pick if strict else pick_lod)(lib, coarser.get(preset['spoiler'], lod))
            report.append({'name': part.name, 'triangles': part.triangles})
            b.slot = SLOT_SPOILER
            b.add(part, matrix_xform(sockets[SPOILER_SOCKET]), library=True)
    if preset.get('roof'):   # a roof scoop (CARS/ROOF) at the roof's socket, in the body's paint as the spoilers
        lib = list(parts(archived(game, 'CARS/ROOF/GEOMETRY.BIN'), lambda n: lod_family(n) == preset['roof']))
        key = n2feng.bin_hash(ROOF_SOCKET)
        if not lib or key not in sockets:
            raise ValueError('entrada de ar ou socket ausente: ' + preset['roof'])
        if not strict or far_has(lib, lod):
            part = (far_pick if strict else pick_lod)(lib, lod)
            report.append({'name': part.name, 'triangles': part.triangles})
            b.slot = SLOT_ROOF
            b.add(part, matrix_xform(sockets[key]), library=True)
    if preset.get('mirror'):   # side mirrors (CARS/MIRRORS_<KIND>: MIRROR_<KIND>_LEFT_STYLEnn and RIGHT) at the car's two
        kind, style = preset['mirror'].split('_', 1)   # BODY_STYLE03
        lib = list(parts(archived(game, f'CARS/MIRRORS_{kind}/GEOMETRY.BIN')))
        b.slot = SLOT_MIRROR
        cf = style.endswith('_CF')
        plain = style[:-3] if cf else style
        for side, socket in zip(('LEFT', 'RIGHT'), MIRROR_SOCKETS):
            # a style's parts and its carbon ones (_CF, the right's cut to
            # "_C" at 27 characters, as a plain level C), told by their carbon
            want = f'MIRROR_{kind}_{side}_{plain}'
            mine = [p for p in lib if p.name.startswith(want) and len(p.name) <= len(want) + 4
                    and any(m == CARBONFIBRE_TEX for _, _, m in p.subs) == cf]
            key = n2feng.bin_hash(socket)
            if not mine or key not in sockets:
                raise ValueError('retrovisor ou socket ausente: ' + want)
            if strict and not far_has(mine, lod):
                continue
            part = (far_pick if strict else pick_lod)(mine, lod)
            report.append({'name': part.name, 'triangles': part.triangles})
            first = len(b.meshes)
            b.add(part, matrix_xform(sockets[key]), library=True)
            if side == 'RIGHT':
                b.meshes[first:] = [x[:4] + (x[4] | F_RIGHT,) + x[5:] for x in b.meshes[first:]]
    if preset.get('rims'):
        brand, style, size = preset['rims']
        prefix = f'{brand}_{style}_{size}'
        lib = list(parts(archived(game, f'CARS/WHEELS/GEOMETRY_{brand}.BIN'),
                         lambda n: lod_family(n) == prefix))
        if not lib:
            raise ValueError('roda inexistente: ' + prefix)
        if strict and lod >= 3:
            return b, textures, report
        part = (far_pick if strict else pick_lod)(lib, coarser.get(prefix, lod))
        report.append({'name': part.name, 'triangles': part.triangles})
        # The PS2 draws a rim at its own size (its name's outer diameter:
        # RACINGHART_STYLE07_20_26, 20 in rim, 26 in tyre, radius 0.330 m),
        # never scaled to the car's tyre; only its width follows the car's
        # record for that axle. Its RAM, Rachel's 350Z: the rear wheels'
        # matrices scale 1.0 round the axle and 1.2772 along it (0.235 m over
        # the model's 0.184), the outer face 0.949 m out. Fitted to the stock
        # wheel's radius they came out 2.4 to 4% too big and 20% too narrow.
        # Its shop offers each car the rims of its own tyre size; a size far
        # from the car's (the garage's options are a few fixed sizes for
        # every car) is drawn at the record's radius instead.
        mid = (min(v[1] for v in part.verts) + max(v[1] for v in part.verts)) / 2
        full = pick_lod(lib, 0)   # every level of detail at the full one's size (the coarser's own extents run wider)
        native = radius(full)
        width = max(v[1] for v in full.verts) - min(v[1] for v in full.verts)
        b.slot = SLOT_RIMS
        for k, mount in enumerate(wheels):
            scale = 1.0 if abs(native - mount[3]) <= RIM_NATIVE * mount[3] else mount[3] / native
            f, mirror = wheel_xform(mount, mid, scale, yscale=mount[4] / width)
            b.add(part, f, mirror, library=True, wheel=k)
    return b, textures, report


def add_decals(b, game, car, preset):
    """The preset's decals ({zone: [art per slot of its medium layout]}, the
    art CARS/TEXTURES.BIN's <ART>_RECT or _WIDE by the zone's shape) on the
    car's decal places, flagged as the garage's (tools/convert_garage.py):
    a wide body kit's own places on the doors and quarters (the game turns
    the stock body's off under it)."""
    import convert_garage as cg   # (it imports this module)
    store = SlotTPK(archived(game, 'CARS/TEXTURES.BIN'))
    names = {store.name(k): k for k in store.slots}
    every = list(parts(archived(game, f'CARS/{car}/GEOMETRY.BIN'), lambda n: '_DECAL_' in n))
    m = re.match(r'KITW0?(\d+)$', preset.get('widebody') or '')
    zones = [z for z, _ in cg.DECAL_ZONES]
    report = []
    for zone, arts in sorted(preset['decals'].items()):
        if zone not in zones:
            raise ValueError('zona de decalque inexistente: ' + zone)
        z = zones.index(zone)
        shape = cg.DECAL_ZONES[z][1]
        wide = int(m.group(1)) if m and zone in ('LEFT_DOOR', 'RIGHT_DOOR', 'LEFT_QUARTER', 'RIGHT_QUARTER') else 0
        head = f'{car}_WIDE{wide}_DECAL_' if wide else f'{car}_DECAL_'
        n = cg.DECAL_SLOTS[z][0]   # the medium layout: told from the small by its slot count (names cut at 27)
        place = next((p for p in every if p.name.startswith(head) and p.name[len(head):].startswith(zone[:27 - len(head)])
                      and len({t for _, t, _ in p.subs}) == n and any(t == cg.DUMMY_DECAL1 for _, t, _ in p.subs)), None)
        if place is None or len(arts) > n:
            raise ValueError(f'{car}: sem lugar de decalque {zone} para {len(arts)}')
        for k, art in enumerate(arts):
            if not art:
                continue
            key = names.get(f'{art}_{shape}')
            ids = [i for i_, t, _ in place.subs if t == cg.DUMMY_DECAL1 + k for i in i_]
            if key is None or not ids:
                raise ValueError(f'decalque inexistente: {art}_{shape} em {zone} {k}')
            remap = {v: i for i, v in enumerate(sorted(set(ids)))}
            vertices = []
            for vi in remap:
                x, y, z3, u, v, nx, ny, nz = place.verts[vi]
                vertices.append(struct.pack('<8f', u, v, ny, nz, nx, y, z3, x))   # as Builder.add
            b.meshes.append((vertices, [remap[i] for i in ids], key, rgba_u32((255, 255, 255, 255)),
                             cg.F_DECAL | (cg.SLOT_DECAL + z) << SLOT_SHIFT | k << cg.DECAL_SLOT_SHIFT, 1.0))
            report.append({'name': f'{place.name}:{k} {art}_{shape}', 'triangles': len(ids) // 3})
    return report


def plate_face(chosen, plate, at):
    """The plate's x: the marker's, or just behind the car's rearmost
    surface over the plate (rays along x through a grid on it, against the
    chosen parts' triangles): at the marker alone it sat inside the
    bumper's sloped face, unseen."""
    ys = [v[1] for v in plate.verts]
    zs = [v[2] for v in plate.verts]
    front = max(v[0] for v in plate.verts)   # the plate's face toward the car
    best = at[0]
    for i in range(5):
        for j in range(3):
            py = at[1] + min(ys) + (max(ys) - min(ys)) * (i + .5) / 5
            pz = at[2] + min(zs) + (max(zs) - min(zs)) * (j + .5) / 3
            for part in chosen:
                for ids, *_ in part.subs:
                    for k in range(0, len(ids) - 2, 3):
                        a, b_, c = (part.verts[ids[k + n]] for n in range(3))
                        d = (b_[1] - a[1]) * (c[2] - a[2]) - (c[1] - a[1]) * (b_[2] - a[2])
                        if abs(d) < 1e-9:
                            continue
                        u = ((py - a[1]) * (c[2] - a[2]) - (c[1] - a[1]) * (pz - a[2])) / d
                        w = ((b_[1] - a[1]) * (pz - a[2]) - (py - a[1]) * (b_[2] - a[2])) / d
                        if u < 0 or w < 0 or u + w > 1:
                            continue
                        x = a[0] + u * (b_[0] - a[0]) + w * (c[0] - a[0])
                        if x < 0 and abs(x - at[0]) < .25:   # the rear face near the marker (not the far side of the car)
                            best = min(best, x - front - .005)
    return best


def family_slot(car, family):
    """The garage slot of a car part family: bumpers and skirts (the kit),
    the hood, the wheels (tyre and rim; the brakes stay), the head and tail
    lights."""
    m = re.match(re.escape(car) + r'_(?:BASE|KITW?\d+|STYLE\d+)_?(.*)', family)
    rest = m.group(1) if m else ''
    if rest.startswith('FRONT_WH'):
        return SLOT_RIMS
    if rest.startswith('EXHAUST'):
        return SLOT_EXHAUST
    if is_mirror(rest):
        return SLOT_MIRROR
    slot = slot_of(rest)
    return (SLOT_KIT if slot == 'FRONT_BUMPER' else SLOT_REAR_BUMPER if slot == 'REAR_BUMPER' else SLOT_SKIRT if slot == 'SKIRT' else
            SLOT_HOOD if slot == 'HOOD' else SLOT_BODY if slot == 'BODY' else
            (SLOT_DOOR_RIGHT if 'RIG' in rest else SLOT_DOOR) if slot == 'DOOR' else
            SLOT_TRUNK if rest == 'TRUNK' else
            SLOT_HEADLIGHT if slot == 'HEADLIGHT' else SLOT_BRAKELIGHT if slot == 'BRAKELIGHT' else 0)


def fender_width(all_parts, family, axle_x):
    """How far the body reaches sideways over an axle (the full detail's
    vertices within 0.35 m of it, 0.1 to 0.6 m up)."""
    reach = 0.0
    for p in all_parts:
        if lod_family(p.name) == family and p.name.endswith('_A'):
            for v in p.verts:
                if abs(v[0] - axle_x) < 0.35 and 0.1 < v[2] < 0.6:
                    reach = max(reach, abs(v[1]))
    return reach


def widebody_track(all_parts, car, widebody, wheels, rims=False):
    """A wide body kit's wheels out to its fenders, the tyre's outer face
    flush with the fender over its axle, as the PS2's: its RAM holds Rachel's
    350Z's rear tyre faces at 0.945 m under a 0.943 m fender (KITW02), and
    from behind they barely show. On the stock mounts (0.755 m) they sat 5 to
    7 cm deep in the arches."""
    if not widebody:
        return wheels
    tyre = pick_lod(families_stock_wheel(all_parts, car), 0)
    stock_half = (max(v[1] for v in tyre.verts) - min(v[1] for v in tyre.verts)) / 2   # the stock wheel's half width
    out = []
    for x, y, z, r, w in wheels:
        half = w / 2 if rims else stock_half   # aftermarket rims: the record's width for the axle (assemble)
        wide = fender_width(all_parts, f'{car}_{widebody}_BODY', x)
        track = max(abs(y), wide - half) if wide else abs(y)
        out.append((x, track if y > 0 else -track, z, r, w))
    return out


def families_stock_wheel(all_parts, car):
    return [p for p in all_parts if lod_family(p.name).startswith(f'{car}_KIT00_FRONT_WH')]


def radius(part):
    xs = [v[0] for v in part.verts]
    zs = [v[2] for v in part.verts]
    return .25 * ((max(xs) - min(xs)) + (max(zs) - min(zs)))


# ----------------------------------------------------------------------- pack

def used_textures(meshes, used=None):
    used = list(used or [])
    for m in meshes:
        if m[2] is not None and m[2] not in used:
            used.append(m[2])
    return used


def save_pack(path, meshes, texdata, car_id, extra=()):
    """N2PV v3. Header 32 B; textures (key,w,h,fmt=1 CLUT8,bytes,vrepeat f32)
    + CLUT 1024 B + swizzled indices; meshes (nv,ni,tex,colour,flags,uvscale f32).
    extra: meshes of its far levels (save_lods), their textures at the end."""
    used = used_textures(extra, used_textures(meshes))
    out = bytearray(struct.pack('<4s6I', b'N2PV', 3, len(used), len(meshes),
                                sum(len(m[1]) // 3 for m in meshes), car_id, 0))
    def align():
        out.extend(bytes((-len(out)) % 16))
    align()
    for ref in used:
        w, h, pal, idx, vrepeat = texdata[ref]
        out.extend(struct.pack('<5If', ref if isinstance(ref, int) else 0, w, h, 1, len(idx), vrepeat))
        align()
        out.extend(pal)
        out.extend(idx)
        align()
    for vertices, ids, ref, colour, flags, uvscale in meshes:
        out.extend(struct.pack('<5If', len(vertices), len(ids), used.index(ref) if ref is not None else 0xffffffff,
                               colour, flags, uvscale))
        align()
        out.extend(b''.join(vertices))
        align()
        out.extend(struct.pack('<' + str(len(ids)) + 'H', *ids))
        align()
    path.write_bytes(out)
    return dict(format_version=3, bytes=len(out), textures=len(used), draws=len(meshes), used=used,
                reflective_draws=sum(bool(m[4] & (F_GLASS | F_PAINT | F_CHROME | F_CARBON | F_METAL)) for m in meshes),
                reflection_triangles=sum(len(m[1]) // 3 for m in meshes if m[4] & (F_GLASS | F_PAINT | F_CHROME | F_CARBON | F_METAL)),
                triangles=sum(len(m[1]) // 3 for m in meshes), vertices=sum(len(m[0]) for m in meshes),
                texture_bytes=sum(1024 + len(texdata[r][3]) for r in used),
                sha256=hashlib.sha256(out).hexdigest())


# The street's far levels of a car (CarRenderInfo::Render picks its body's
# by its size on screen): N2PL v1, a file beside its pack (<pack>.n2pl),
# drawing with the pack's textures. Header 32 B: magic, version, meshes,
# triangles, the pack's bytes and textures (a pack converted again without
# it: the file is left alone), the pack's own level (the racers' B: 1);
# then the meshes as the pack's, the level in bits 28-29 of the flags, in
# order of level.
F_LOD_SHIFT = 28


def save_lods(path, levels, used, pack_bytes, base):
    """levels: [(level, meshes)] past base, used: the pack's textures in order."""
    meshes = [(v, i, ref, colour, flags | level << F_LOD_SHIFT, uv) for level, ms in levels for v, i, ref, colour, flags, uv in ms]
    out = bytearray(struct.pack('<4s6I', b'N2PL', 1, len(meshes), sum(len(m[1]) // 3 for m in meshes), pack_bytes, len(used), base))
    def align():
        out.extend(bytes((-len(out)) % 16))
    align()
    for vertices, ids, ref, colour, flags, uvscale in meshes:
        out.extend(struct.pack('<5If', len(vertices), len(ids), used.index(ref) if ref is not None else 0xffffffff,
                               colour, flags, uvscale))
        align()
        out.extend(b''.join(vertices))
        align()
        out.extend(struct.pack('<' + str(len(ids)) + 'H', *ids))
        align()
    path.write_bytes(out)
    return dict(bytes=len(out), draws=len(meshes),
                triangles={level: sum(len(m[1]) // 3 for m in ms) for level, ms in levels})


def save_envmap(path, game, name='SHOWROOM', size=64):
    """N2EV: magic, face size, then XPOS XNEG YPOS YNEG ZPOS ZNEG RGBA8888."""
    faces = envmap_faces(archived(game, f'FRONTEND/ENVMAPS/{name}.BIN'))
    out = bytearray(struct.pack('<4sI', b'N2EV', size) + bytes(8))
    for face in ('XPOS', 'XNEG', 'YPOS', 'YNEG', 'ZPOS', 'ZNEG'):
        t = faces[face]
        rgba, w, h = box_downscale(t.rgba(), t.w, t.h, size)
        for i in range(3, len(rgba), 4):
            rgba[i] = 255
        out.extend(rgba)
    path.write_bytes(out)
    return len(out)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preset', type=Path, default=ROOT / 'presets/350z-showroom.json')
    parser.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    parser.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    parser.add_argument('--skin-png', type=Path, help='grava a skin composta (debug)')
    parser.add_argument('--tag', help='grava só a versão leve como <tag>.n2pv (oponentes)')
    parser.add_argument('--detail', action='store_true', help='com --tag: também a detalhada, <tag>-detail.n2pv (frota)')
    args = parser.parse_args()
    preset = json.loads(args.preset.read_text())
    args.out.mkdir(parents=True, exist_ok=True)
    car = preset['car']
    report = {'preset': preset, 'source': 'Xbox USA', 'material_note': 'GU approximation, not the original Xbox shaders', 'packs': {}}
    textures = None
    lods = ([(0, args.tag + '-detail')] if args.detail else []) + [(1, args.tag)] if args.tag else enumerate(('detail', 'light'))
    for lod, label in lods:
        b, textures, selected = assemble(args.game, dict(preset, interior=bool(preset.get('interior')) and lod == 0), lod)   # the inside: the garage's pack
        meshes = b.batched()
        # the street's pack (the racers', the fleet's, Rachel's car-light): its far levels C and D beside it
        far = [(level, assemble(args.game, dict(preset, interior=False), level, strict=True)[0].batched())
               for level in ((2, 3) if lod == 1 else ())]
        extra = [m for _, ms in far for m in ms]
        texdata = {}
        for ref in {m[2] for m in meshes + extra if m[2] is not None}:
            if ref == 'SKIN':
                if 'SKIN' not in texdata:
                    w, h, pal, idx, vrepeat, full = composite_skin(textures, args.game, car, preset)
                    texdata['SKIN'] = (w, h, pal, idx, vrepeat)
                    if args.skin_png:
                        from n2formats import Texture  # noqa
                        write_png(args.skin_png, 512, 512, full)
            elif ref == CARBONFIBRE_TEX:
                texdata[ref] = carbon_tile(textures.get(ref))
            else:
                texdata[ref] = to_clut8(textures.get(ref), 128)
        pack = args.out / (f'{label}.n2pv' if args.tag else f'car-{label}.n2pv')
        info = save_pack(pack, meshes, texdata, CAR_IDS.get(car, 0), extra)
        used = info.pop('used')
        if far:
            info['far_levels'] = save_lods(pack.with_suffix('.n2pl'), far, used, info['bytes'], lod)
        info['selected_parts'] = selected
        info['missing_texture_hashes'] = sorted(f'{k:08x}' for k in b.missing)
        report['packs'][label] = info
        print(label, info['triangles'], 'triangles,', info['draws'], 'draws,', info['bytes'], 'bytes')
    if args.tag:
        (args.out / f'{args.tag}.json').write_text(json.dumps(report, indent=2) + '\n')
        return
    print('envmap', save_envmap(args.out / 'showroom.n2ev', args.game), 'bytes')
    (args.out / 'conversion.json').write_text(json.dumps(report, indent=2) + '\n')


def write_png(path, w, h, rgba):
    import zlib
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    scan = b''.join(b'\0' + bytes(rgba[y*w*4:(y+1)*w*4]) for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', w, h, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    main()
