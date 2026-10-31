#!/usr/bin/env python3
"""The game's preset cars (GLOBAL/GLOBALB.BUN chunk 0x30220), slot by slot.

A record is 824 bytes: a list link (8), the car type's name (32), the
preset's name (32), the performance level (s32; -1 = the 68 per-part bytes
at the end), then one u32 per customisation slot (170): 0 leaves the slot
empty, 1 keeps the stock part, anything else is the hash of a part of that
slot's kind in the parts table (see partsdb.py), for that car or a generic
one. The slot names are the game's CAR_SLOT_ID table (CarSlotIDNames in the
GameCube Speed.elf); SLOT_KIND maps each slot to the part kind it holds
(its CarPartSlotMap).

A part (14 bytes in chunk 0x34604): u32 name hash, u8 kind, u8 flags (the
upgrade level in the top 3 bits), u8 how its model is named (0 by itself,
1 after the car type, 2 after its own attribute 0xebb03e66), u8 its car type
(an index into chunk 0x3460b's name hashes), u16 its title (a word index into
the strings, 0x34606), u16 its attribute list (0x3460c: a count, then that
many indices into 0x34605's (key, value) pairs), u16 its model (0x3460a, 36
bytes: u8 templated, u8, u16 the middle string's word index, then 2 pieces x
4 levels of detail, each a string's word index when templated, else a model
hash; a templated name is <car><middle><string>_<A-D>).

    python3 tools/preset_dump.py                  # DDAY_PLAYER_CAR
    python3 tools/preset_dump.py --list           # every preset's name
    python3 tools/preset_dump.py CALEB_GTO --all  # the empty slots too
    python3 tools/preset_dump.py --json           # the slots as JSON
    python3 tools/preset_dump.py --convert        # as a tools/convert_car.py preset

A part may turn other slots off (attribute 0xe80a3b62, a slot's name hash,
RideInfo::UpdatePartsEnabled): Rachel's TWISTER hood the hood's decals. A
wide body kit (a WIDE_BODY part of level > 0) turns the bumpers, skirts and
stock body off (it is the whole shell) and the doors' and quarters' decals
go on its own places (<CAR>_WIDE<n>_DECAL_*), their art still the
DECAL_<ZONE>_TEX<k> slots'. A decal part's art is CARS/TEXTURES.BIN's
<ART>_RECT or _WIDE (by the zone's shape), the part named <ART>_DECAL or
<ART>_BLACK_DECAL.
"""
import argparse
import json
from pathlib import Path
import re
import struct

import convert_hud
import n2feng as fe
from n2formats import SlotTPK, archived, chunks
import n2source

ROOT = Path(__file__).resolve().parents[1]
RECORD = 0x338
DISABLES = 0xe80a3b62   # a part's attribute: a slot it turns off (its name's hash)

SLOT_NAMES = (
    'BASE FRONT_BUMPER REAR_BUMPER LEFT_SIDE_MIRROR RIGHT_SIDE_MIRROR BODY WIDE_BODY ROOF TOP HOOD TRUNK SKIRT '
    'SPOILER ENGINE HEADLIGHT BRAKELIGHT EXHAUST DOOR_LEFT DOOR_RIGHT DOOR_PANEL_LEFT DOOR_PANEL_RIGHT '
    'DOOR_SILL_LEFT DOOR_SILL_RIGHT FENDER QUARTER HOOD_UNDER TRUNK_UNDER FRONT_BRAKE REAR_BRAKE FRONT_WHEEL '
    'REAR_WHEEL SPINNER WING_MIRROR LICENSE_PLATE TRUNK_AUDIO').split() + \
    [f'TRUNK_AUDIO_COMP_{i}' for i in range(12)] + \
    'DAMAGE_FRONT DAMAGE_REAR DAMAGE_LEFT DAMAGE_RIGHT DAMAGE_TOP'.split() + \
    [f'DECAL_{p}' for p in ('HOOD', 'FRONT_WINDOW', 'REAR_WINDOW', 'LEFT_DOOR', 'RIGHT_DOOR', 'LEFT_QUARTER', 'RIGHT_QUARTER')] + \
    [f'WIDEBODY_DECAL_{p}' for p in ('LEFT_DOOR', 'RIGHT_DOOR', 'LEFT_QUARTER', 'RIGHT_QUARTER')] + \
    ['BASE_PAINT'] + [f'VINYL_LAYER{i}' for i in range(4)] + \
    [f'PAINT_{p}' for p in ('ENGINE', 'SPOILER', 'BRAKE', 'EXHAUST', 'AUDIO', 'RIM', 'SPINNER', 'ROOF', 'MIRROR')] + \
    [f'VINYL_COLOUR{i}_{j}' for i in range(4) for j in range(4)] + \
    'KIT_CARBON HOOD_CARBON DOOR_CARBON TRUNK_CARBON'.split() + \
    [f'DECAL_{p}_TEX{i}' for p in ('HOOD', 'FRONT_WINDOW', 'REAR_WINDOW', 'LEFT_DOOR', 'RIGHT_DOOR', 'LEFT_QUARTER',
                                   'RIGHT_QUARTER') for i in range(8)] + \
    ('WINDOW_TINT NEON NEON_ENGINE NEON_CABIN NEON_TRUNK CABIN_NEON_FRAME HEADLIGHT_BULB DOOR_STYLE HYDRAULICS '
     'NOS_PURGE CUSTOM_HUD HUD_BACKING_COLOUR HUD_NEEDLE_COLOUR HUD_CHARACTER_COLOUR CV WHEEL_MANUFACTURER MISC').split()
assert len(SLOT_NAMES) == 170
# the part kind each slot holds, in runs of (first slot, kinds...)
_KIND_RUNS = ((0, *range(0, 27)), (27, 27, 27, 28, 28, 29, 30, 31), (34, 60, 61, 61, 62, 62, 63, 63, 61, 61, 61, 61, 62, 62),
              (47, *range(32, 48)), (63, 48, 50, 50, 50, 50), (68, *[48] * 9), (77, *[49] * 16), (93, *[51] * 4),
              (97, *[52] * 56), (153, 53, 54, 54, 54, 54, 55, 56, 57, 58, 59, 64, 65, 65, 65, 66, 67, 68))
SLOT_KIND = [None] * 170
for first, *kinds in _KIND_RUNS:
    SLOT_KIND[first:first + len(kinds)] = kinds
assert None not in SLOT_KIND


class PartsTable:
    """The parts table read the game's way (CarPart, CarPartModelTable)."""

    def __init__(self, game):
        d = convert_hud.load(game, 'GLOBAL/GLOBALB.BUN')
        self.block = block = {t: d[p:p + n] for t, p, n in chunks(d)}
        self.pool = block[0x34606]
        self.cars = struct.unpack(f'<{len(block[0x3460b]) // 4}I', block[0x3460b])
        a = block[0x34605]
        self.attrs = [struct.unpack_from('<II', a, i) for i in range(0, len(a) - 7, 8)]
        self.lists = struct.unpack_from(f'<{len(block[0x3460c]) // 2}H', block[0x3460c])
        self.models = block[0x3460a]
        p = block[0x34604]
        self.parts = []
        for o in range(14, len(p) - 13, 14):   # (after a 14-byte head)
            key, kind, flags, mode, car, title, at, model = struct.unpack_from('<IBBBBHHH', p, o)
            self.parts.append(dict(key=key, kind=kind, level=flags >> 5, group=flags & 0x1f, mode=mode, car=self.cars[car],
                                   title=self.word(title), at=at, model=model))
        self.by_key = {}
        for q in self.parts:
            self.by_key.setdefault(q['key'], []).append(q)
        # names for hashes: the table's strings, the car types, the game's own
        self.names = {}
        for s in self.pool.split(b'\0'):
            self.learn(s.decode('latin-1'))

    def word(self, w):
        return self.pool[4 * w:self.pool.find(b'\0', 4 * w)].decode('latin-1') if w != 0xffff and 4 * w < len(self.pool) else ''

    def learn(self, s):
        if s:
            self.names.setdefault(fe.bin_hash(s), s)

    def name(self, h):
        return self.names.get(h, '0x%08x' % h)

    def attributes(self, part):
        """[(key, value)] in the part's order."""
        if part['at'] == 0xffff:
            return []
        n = self.lists[part['at']]
        return [self.attrs[i] for i in self.lists[part['at'] + 1:part['at'] + 1 + n] if i < len(self.attrs)]

    def model_names(self, part, lod=0):
        """The part's model names (each piece at that level of detail)."""
        if part['model'] == 0xffff:
            return []
        e = self.models[36 * part['model']:36 * part['model'] + 36]
        templated, middle = e[0], struct.unpack_from('<H', e, 2)[0]
        out = []
        for piece in range(2):
            v = struct.unpack_from('<I', e, 4 + 16 * piece + 4 * lod)[0]
            if v == 0xffffffff:
                continue
            if not templated:
                out.append(self.name(v))
                continue
            head = self.name(part['car']) if part['mode'] == 1 else '' if part['mode'] == 0 else \
                self.name(dict(self.attributes(part)).get(0xebb03e66, 0))
            out.append(head + self.word(middle) + self.word(v) + '_' + 'ABCD'[lod] if middle != 0xffff
                       else head + self.word(v) + '_' + 'ABCD'[lod])
        return out

    def find(self, slot, key, car):
        """The part a preset's slot names, the game's NewGetCarPart way (the
        car's own first, else a generic one of that kind)."""
        hits = [q for q in self.by_key.get(key, []) if q['kind'] == SLOT_KIND[slot]]
        own = [q for q in hits if q['car'] == fe.bin_hash(car)]
        return (own or hits or [None])[0]


def presets(table):
    p = table.block[0x30220]
    for o in range(0, len(p) - RECORD + 1, RECORD):
        car = p[o + 8:o + 0x28].split(b'\0')[0].decode('latin-1')
        name = p[o + 0x28:o + 0x48].split(b'\0')[0].decode('latin-1')
        perf, = struct.unpack_from('<i', p, o + 0x48)
        yield dict(car=car, name=name, perf=perf, slots=struct.unpack_from('<170I', p, o + 0x4c),
                   perf_parts=list(p[o + 0x2f4:o + RECORD]))


def describe(table, preset, every=False):
    """[(slot, name, part title or state, model names, attributes)]."""
    rows = []
    for slot, v in enumerate(preset['slots']):
        if v in (0, 1):
            if every:
                rows.append(dict(slot=slot, name=SLOT_NAMES[slot], state='empty' if v == 0 else 'stock'))
            continue
        q = table.find(slot, v, preset['car'])
        if q is None:
            rows.append(dict(slot=slot, name=SLOT_NAMES[slot], state='?', hash='0x%08x' % v))
            continue
        attrs = {('(desliga)' if k == DISABLES else table.name(k)):
                 (table.names.get(val, '0x%08x' % val) if val > 0xffff else val) for k, val in table.attributes(q)}
        rows.append(dict(slot=slot, name=SLOT_NAMES[slot], hash='0x%08x' % v, part=table.name(v), title=q['title'],
                         car=table.name(q['car']), models=table.model_names(q), attrs=attrs))
    return rows


def learn_names(table, extra=()):
    """Hash names the table does not spell: the slots', its attribute keys,
    and the CARS/ archives' model names."""
    for s in SLOT_NAMES:
        table.learn(s)
    for s in ('RED GREEN BLUE GLOSS DISPRED DISPGREEN DISPBLUE REMAP NAME_OFFSET TEXTURE LOD_BASE_NAME LOD_CHARACTERS_OFFSET '
              'LOD_NAME_PREFIX_SELECTOR MAX_LOD MIN_LOD CARBONFIBRE KITNUMBER STOCK STOCK_MATCHED BLEND MIRROR NAME '
              'LANGUAGEHASH SPINNER_TEXTURE SPECIAL HIGHLIGHTED LOCKED SPEECHCOLOUR SPEECHCOLOR TEXTURE_NAME '
              'PART_NAME_OFFSETS PART_NAME_SELECTOR WHEEL_SIZE OUTER_RADIUS INNER_RADIUS BRAND_NAME DAMAGE_LEVEL '
              'CV RIM_SIZE COLOR0 COLOR1 COLOR2 COLOR3 NUMCOLOURS NUMREMAPCOLOURS TYPE GROUPNUMBER '
              'MODEL_TABLE_OFFSET EXCLUDE_UG1 EXCLUDE_UG2 EXCLUDE_SUV').split():
        table.learn(s)
    for s in extra:
        table.learn(s)
    for p in presets(table):
        table.learn(p['car'])


def learn_decals(table, game):
    """{decal part hash: art} from CARS/TEXTURES.BIN's <ART>_RECT names."""
    store = SlotTPK(archived(game, 'CARS/TEXTURES.BIN'))
    arts = {}
    for k in store.slots:
        n = store.name(k)
        if n.endswith('_RECT'):
            a = n[:-5]
            arts[fe.bin_hash(a + '_DECAL')] = arts[fe.bin_hash(a + '_BLACK_DECAL')] = a
            table.learn(a)
    return arts


ZONES = ('HOOD', 'FRONT_WINDOW', 'REAR_WINDOW', 'LEFT_DOOR', 'RIGHT_DOOR', 'LEFT_QUARTER', 'RIGHT_QUARTER')
TINT_LEVEL = {'LIGHT': 1, 'MEDIUM': 2, 'DARK': 3}
# slots the port does not draw: a part of theirs the table lacks is let go
IGNORED = re.compile(r'NOS_PURGE|NEON|CABIN_NEON|HEADLIGHT_BULB|HYDRAULICS|CUSTOM_HUD|HUD_|CV$|MISC|DOOR_STYLE|TRUNK_AUDIO|SPINNER|PAINT_|LICENSE')   # the black tints: the port's window_tint (convert_car WINDOW_TINTS)


def to_convert_car(table, preset, game):
    """The record as a tools/convert_car.py preset (the keys it reads). What
    the converter cannot draw raises: another vinyl layer, a coloured tint,
    a small decal layout, a shop mirror."""
    car = preset['car']
    parts = {}
    for slot, v in enumerate(preset['slots']):
        if v not in (0, 1):
            q = table.find(slot, v, car)
            if q is None and not IGNORED.match(SLOT_NAMES[slot]):
                raise ValueError(f'{SLOT_NAMES[slot]}: part 0x{v:08x} is not in the table')
            if q is not None:
                parts[SLOT_NAMES[slot]] = q
    off = {table.name(val) for q in parts.values() for k, val in table.attributes(q) if k == DISABLES}
    wide = parts.get('WIDE_BODY')
    if wide is not None and wide['level']:
        off |= {'FRONT_BUMPER', 'REAR_BUMPER', 'SKIRT', 'BODY'}
    else:
        wide = None
    model = lambda slot: table.model_names(parts[slot])[0][:-2] if slot in parts and table.model_names(parts[slot]) else None
    owner = lambda slot, rest: re.match(re.escape(car) + r'_(KITW?\d+|STYLE\d+)_' + rest, model(slot) or '')
    out = {'car': car}
    a = dict(table.attributes(parts['BASE_PAINT']))
    rgb = tuple(a[fe.bin_hash(k)] for k in ('RED', 'GREEN', 'BLUE'))
    out['paint'] = '%02x%02x%02x' % rgb
    tint = parts.get('WINDOW_TINT')
    if tint is not None and tint['title'] != 'STOCK WINDOW TINT':
        level = tint['title'].split(' ', 1)[0]
        if level not in TINT_LEVEL:
            raise ValueError('no matching window tint in the port: ' + tint['title'])
        out['window_tint'] = TINT_LEVEL[level]   # (the port's tints are black: a coloured one by its darkness)
    neon = parts.get('NEON')
    if neon is not None and neon['title'] != 'NO NEON':
        out['neon'] = neon['title']   # the underglow, by the garage's name (RED PULSE): the race draws it (main.c opp_neon)
    kit = owner('FRONT_BUMPER', 'FRONT_BUMPER')
    out['kit'] = kit.group(1) if kit else 'KIT00'
    for key, slot, rest in (('rear_bumper', 'REAR_BUMPER', 'REAR_BUMPER'), ('skirt', 'SKIRT', 'SKIRT')):
        m = owner(slot, rest)
        if m and m.group(1) != out['kit']:
            out[key] = m.group(1)
    if wide is not None:
        out['widebody'] = owner('WIDE_BODY', 'BODY').group(1)
    for key, slot, rest in (('hood', 'HOOD', 'HOOD'), ('headlight', 'HEADLIGHT', 'HEADLIGHT'),
                            ('brakelight', 'BRAKELIGHT', 'BRAKELIGHT')):
        m = owner(slot, rest)
        if m and m.group(1) != 'KIT00':
            out[key] = m.group(1)
    if 'HOOD' in parts and dict(table.attributes(parts['HOOD'])).get(fe.bin_hash('CARBONFIBRE')):
        out['carbon_hood'] = True
    for key, slot in (('spoiler', 'SPOILER'), ('roof', 'ROOF')):
        if model(slot):
            out[key] = model(slot)
    exhaust = model('EXHAUST')
    if exhaust and not exhaust.startswith(car + '_'):   # the shop's tips (the car's own: its KIT00_EXHAUST)
        out['exhaust'] = exhaust
    if 'WING_MIRROR' in parts and parts['WING_MIRROR']['car'] != fe.bin_hash(car):
        raise ValueError('retrovisor da loja: ' + table.name(parts['WING_MIRROR']['key']))
    rims = model('FRONT_WHEEL')
    if rims and not rims.startswith(car + '_'):
        brand, style, size = rims.split('_', 2)
        out['rims'] = [brand, style, size]
    if any(f'VINYL_LAYER{i}' in parts for i in (1, 2, 3)):
        raise ValueError('mais de uma camada de vinil')
    if 'VINYL_LAYER0' in parts:
        v = parts['VINYL_LAYER0']
        out['vinyl'] = table.name(v['key'])
        out['vinyl_remap'] = bool(dict(table.attributes(v)).get(fe.bin_hash('REMAP')))
    arts = learn_decals(table, game)
    decals, small = {}, []
    for zone in ZONES:
        place = parts.get('DECAL_' + zone)
        if place is None or 'DECAL_' + zone in off:
            continue
        layout = next((w for w in ('MEDIUM', 'SMALL') if table.name(place['key']).endswith(w) or place['title'].endswith(w)), None)
        if layout is None:
            raise ValueError(f'{zone} decals do not fit the medium or small layouts: {place["title"]}')
        slots = [parts.get(f'DECAL_{zone}_TEX{k}') for k in range(8)]
        art = [arts.get(q['key']) if q else None for q in slots]
        while art and art[-1] is None:
            art.pop()
        if art:
            decals[zone] = art
            if layout == 'SMALL':
                small.append(zone)
    if decals:
        out['decals'] = decals
    if small:
        out['decal_small'] = small   # these zones on their small layout (more, smaller decals)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('preset', nargs='?', default='DDAY_PLAYER_CAR')
    n2source.game_args(ap)
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--all', action='store_true', help='also show empty and stock slots')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--convert', action='store_true', help='o preset no formato do tools/convert_car.py')
    args = ap.parse_args()
    table = PartsTable(args.game)
    learn_names(table)
    if args.list:
        for p in presets(table):
            print(f"{p['name']:32} {p['car']:12} perf {p['perf']}")
        return
    preset = next((p for p in presets(table) if p['name'] == args.preset.upper()), None)
    if preset is None:
        raise SystemExit('preset inexistente: ' + args.preset)
    if args.convert:
        print(json.dumps(to_convert_car(table, preset, args.game), indent=2))
        return
    rows = describe(table, preset, args.all)
    if args.json:
        print(json.dumps(dict(name=preset['name'], car=preset['car'], perf=preset['perf'], slots=rows), indent=1))
        return
    print(f"{preset['name']}: {preset['car']}, performance level {preset['perf']}")
    for r in rows:
        if 'state' in r:
            print(f"{r['slot']:3} {r['name']:28} {r['state']}")
            continue
        attrs = ' '.join(f'{k}={v}' for k, v in r['attrs'].items())
        print(f"{r['slot']:3} {r['name']:28} {r['part']:28} [{r['title']}] {r['car']}")
        if r['models']:
            print(' ' * 33 + 'modelo: ' + ', '.join(r['models']))
        if attrs:
            print(' ' * 33 + attrs)


if __name__ == '__main__':
    main()
