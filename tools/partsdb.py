#!/usr/bin/env python3
"""The game's parts table (GLOBAL/GLOBALB.BUN), as the shops read it.

Chunks: 0x34604 the parts (after 8 bytes, 14 per part: u32 name hash, u8
type, level, group, u8, then u16 no, attribute list, template - the u16s
open the next 14-byte slot), 0x3460c the attribute lists (u16 indices),
0x34605 the attributes (u32 key hash, u32 value), 0x34606 the names (each
4-byte aligned; a part's `no` is its title's place there in words: the
rims' style names, "GRAVIS I"). A
part's list runs to the next list's start; its template (another part)
adds the attributes it lacks. Types: 48 paints (GLOSS_, METAL_ and
PEARL<n>_PAINT for the body, then the other paintable parts), 49 the
vinyls' colours (VINYL_L<n>_COLOR<nn>), 50 the vinyls.

Run alone it prints the paints' C tables for psp-viewer/garage.c; with
--specialties, the Car Specialties Shop's (the headlight bulbs, the Nitrous
Purge's kits).
"""
import bisect
import colorsys
from pathlib import Path
import struct
import sys

import convert_hud
import n2feng as fe
from n2formats import chunks

ROOT = Path(__file__).resolve().parents[1]


class Parts:
    def __init__(self, game):
        d = convert_hud.load(game, 'GLOBAL/GLOBALB.BUN')
        block = {t: d[p:p + n] for t, p, n in chunks(d) if t in (0x34604, 0x34605, 0x34606, 0x3460c)}
        self.names = {fe.bin_hash(s.decode('latin-1')): s.decode('latin-1') for s in block[0x34606].split(b'\0') if s}
        pool = block[0x34606]
        title = lambda w: pool[4 * w:pool.find(b'\0', 4 * w)].decode('latin-1') if 4 * w < len(pool) else ''
        a = block[0x34605]
        self.attrs = [struct.unpack_from('<II', a, i) for i in range(0, len(a) - 7, 8)]
        self.lists = struct.unpack_from(f'<{len(block[0x3460c]) // 2}H', block[0x3460c])
        p = block[0x34604][8:]
        raw = [struct.unpack_from('<HHHIBBBB', p, o) for o in range(0, len(p) - 13, 14)]
        self.parts = []
        for i, (_, _, _, key, kind, level, group, _) in enumerate(raw):
            no, at, template = raw[i + 1][:3] if i + 1 < len(raw) else (0, 0xffff, 0xffff)
            self.parts.append(dict(key=key, name=self.names.get(key, ''), kind=kind, level=level, group=group,
                                   at=at, template=template, title=title(no) if i + 1 < len(raw) else ''))
        self.starts = sorted({q['at'] for q in self.parts if q['at'] != 0xffff} | {len(self.lists)})

    def attributes(self, part):
        """{key hash: value}, the part's own over its templates'."""
        out, seen = {}, set()
        while part is not None and id(part) not in seen:
            seen.add(id(part))
            if part['at'] != 0xffff:
                end = self.starts[bisect.bisect_right(self.starts, part['at'])]
                for i in self.lists[part['at']:end]:
                    if i < len(self.attrs):
                        out.setdefault(*self.attrs[i])
            t = part['template']
            part = self.parts[t] if t != 0xffff and t < len(self.parts) else None
        return out

    def get(self, part, key):
        return self.attributes(part).get(fe.bin_hash(key))

    def of_kind(self, kind):
        return [q for q in self.parts if q['kind'] == kind]

    def rgb(self, part):
        return tuple(self.get(part, k) for k in ('RED', 'GREEN', 'BLUE'))


def palette(parts, prefix):
    """<prefix>_L1_..., L2, L3 (COLOR01, NEW_01, TEST01... as named), each
    level in the table's order: the paint shop's grid order."""
    family = [q for q in parts.of_kind(48) + parts.of_kind(49) if q['name'].startswith(prefix + '_L')]
    return sorted(family, key=lambda q: q['name'][len(prefix) + 2])   # stable: the table's order within a level


def c_table(name, colours):
    rows = [','.join('0x%02x%02x%02x' % (b, g, r) for r, g, b in colours[i:i + 10]) for i in range(0, len(colours), 10)]
    return f'static const uint32_t {name}[{len(colours)}]={{\n    ' + ',\n    '.join(rows) + ',\n};'


PURGE_KEY = (140, 140, 150)   # the purge's smoke at its densest (XES_NOSBO_*'s second colour key, alpha 150)
PURGE_NOZZLES = ('HOODLEFT', 'HOODRIGHT', 0x6ab42adf, 0xc1a84e52)   # the left and right hood's, the left and right wheel's
PURGE_EMITTER = {0xbcade4c3: {3390731228: 1, 3429866621: 2, 3469002014: 3}}   # HOODEMITTER: CARFX_NOSBO_A/B/C_HOOD


def shifted(rgb, hue, sat, lum):
    """afxShiftEmitterColours: the colour's hue turned by hue (wrapping),
    its saturation and lightness moved by sat and lum (clamped to 0-1)."""
    h, l, s = colorsys.rgb_to_hls(*(c / 255 for c in rgb))
    h, s, l = (h + hue) % 1, min(1, max(0, s + sat)), min(1, max(0, l + lum))
    return tuple(round(c * 255) for c in colorsys.hls_to_rgb(h, l, s))


def specialties(parts):
    """The Car Specialties Shop's lists for psp-viewer/garage.c: the headlight
    bulbs (type 56: name, level, RED, GREEN, BLUE) and the Nitrous Purge's
    kits (type 59: name, level, the nozzles' bits, the type's effect 1-3 and
    the hood's and the wheels' smoke shifted by the kit's HOOD/TIRE HUE, SAT
    and LUM)."""
    print('static const struct { const char *name;uint8_t level,rgb[3]; } bulbs[GARAGE_BULBS]={')
    print(',\n'.join('    {"%s",%d,{%d,%d,%d}}' % (q['name'], q['level'], *parts.rgb(q)) for q in parts.of_kind(56)) + '};')
    f = lambda q, k: struct.unpack('<f', struct.pack('<I', parts.get(q, k) or 0))[0]
    rows = []
    for q in parts.of_kind(59):
        a = parts.attributes(q)
        bits = sum(1 << i for i, k in enumerate(PURGE_NOZZLES) if a.get(k if isinstance(k, int) else fe.bin_hash(k)))
        hood = shifted(PURGE_KEY, *(f(q, 'HOOD' + k) for k in ('HUE', 'SAT', 'LUM')))
        tire = shifted(PURGE_KEY, *(f(q, 'TIRE' + k) for k in ('HUE', 'SAT', 'LUM')))
        rows.append('    {"%s",%d,%d,%d,0x%02x%02x%02x,0x%02x%02x%02x}' % (q['name'], q['level'], bits, PURGE_EMITTER[0xbcade4c3][a[0xbcade4c3]],
                                                                       *hood[::-1], *tire[::-1]))
    print('static const struct { const char *name;uint8_t level,nozzles,type;uint32_t hood,wheel; } purges[GARAGE_PURGES]={')
    print(',\n'.join(rows) + '};')


def main():
    parts = Parts(ROOT / 'game/xbox/NFSUNDER')
    if sys.argv[1:] == ['--specialties']:
        specialties(parts)
        return
    by_key = {q['key']: q for q in parts.of_kind(48)}   # PEARL11_PAINT on: no name in the table, the hash alone
    pearl = [by_key[fe.bin_hash(f'PEARL{k}_PAINT')] for k in range(1, 31)]
    # a pearl with DISPRED/GREEN/BLUE over a neutral base (grey, black: the
    # colour its shift shows) is drawn in it, the port having no shift with
    # the angle
    shown = lambda q: tuple(parts.get(q, 'DISP' + k) for k in ('RED', 'GREEN', 'BLUE')) \
        if parts.get(q, 'DISPRED') is not None and len(set(parts.rgb(q))) == 1 else parts.rgb(q)
    for name, colours, rgb in (('gloss', palette(parts, 'GLOSS'), parts.rgb), ('metal', palette(parts, 'METAL'), parts.rgb),
                               ('pearl', pearl, shown), ('vinyl_cols', palette(parts, 'VINYL'), parts.rgb),
                               ('rim_cols', palette(parts, 'RIMS'), parts.rgb), ('caliper_cols', palette(parts, 'CALIPERS'), parts.rgb),
                               ('hose_cols', palette(parts, 'HOSES'), parts.rgb), ('muffler_cols', palette(parts, 'MUFFLERS'), parts.rgb)):
        print(c_table(name, [rgb(q) for q in colours]))


if __name__ == '__main__':
    main()
