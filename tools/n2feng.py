"""NFSU2 FEng (EA's frontend engine) packages: the HUD and menu screens.

Layout from reading the data, with the field names of the FEngLib project
(github.com/NFSTools/FEngLib) as a guide. A package is chunk 0x30210
(JDLZ compressed) or 0x30203 (plain: the race HUDs) of a UI bundle
(Global\\InGameCommon.bun, InGameRace.bun, ...), holding FEng chunks
(u32 id, u32 size):
  FEn\\xe7 package, inside it:
    PkHd header: 0x20000, 0, resource name count, ?, name length, file
         length, name, file name (4-aligned)
    TypS, TLs\\xf4, MsgN, EDat, PkgR, Targ: type sizes, messages (skipped)
    Res\\xcc resources: RsNm (names, by byte offset) + RsRq (u32 count, then
         24-byte requests: id, name offset, type, 3 x u32)
    Obj\\xcc objects: FOb\\xea per object holding ObjD (tags), Scrp (one per
         script, tags), MsgR; Butn
Tags are u16 id, u16 length, data. ObjD tags: Ot type, OP (guid, name hash,
flags, resource index), PA parent guid, SA data (colour as 4 x i32 BGRA,
pivot xyz, position xyz, rotation quaternion xyzw, size xyz; images add
upper-left and lower-right uv, coloured images four corner colours,
multi-images three uv pairs and a pivot rotation), If image flags, On
name, St text (UTF-16), Sb/Sj/Sl/Sw/SH/SL text buffer settings.
Scrp tags: Sh (id, length ms, flags: 1 loop, track count), Sn name, Sc
chained script id, FI track header (param type, size, interpolation,
action, u32 length), To track offset (words into the object data), Kd keys
(i32 time, value; the first key, time -1, is the base, the rest deltas
added to it), EV events.
"""
import struct

from n2formats import chunks, jdlz

OBJECT_TYPES = {1: 'image', 2: 'text', 5: 'group', 7: 'movie', 9: 'coloured', 11: 'simple', 12: 'multi'}
RESOURCE_TYPES = {1: 'image', 2: 'font', 4: 'movie', 6: 'animated', 7: 'multi'}
# word offsets of the animatable fields in the object data
TRACK_FIELDS = {0: 'color', 4: 'pivot', 7: 'position', 10: 'rotation', 14: 'size', 17: 'uv0', 19: 'uv1'}
TRACK_ORDER = ['color', 'pivot', 'position', 'rotation', 'size', 'uv0', 'uv1', 'c0', 'c1', 'c2', 'c3']
PARAM_SIZE = {0: 4, 1: 4, 2: 4, 3: 8, 4: 12, 5: 16, 6: 16}


def bin_hash(s):
    """FEng name hash (bin hash of the upper-case name)."""
    h = 0xffffffff
    for c in s.upper().encode('latin-1'):
        h = (h * 33 + c) & 0xffffffff
    return h


def packages(bundle):
    """Decompressed FEng packages of a UI bundle."""
    out = []
    for t, p, n in chunks(bundle):
        if t not in (0x30210, 0x30203):       # compressed / plain package
            continue
        d = bundle[p:p + n]
        j = d.find(b'JDLZ')
        if j >= 0:
            out.append(jdlz(d[j:], struct.unpack_from('<I', d, j + 8)[0]))
        else:
            k = d.find(b'FEn\xe7')
            if k >= 0:
                out.append(d[k:])
    return out


def _chunks(d, p, end):
    while p + 8 <= end:
        cid, size = struct.unpack_from('<4sI', d, p)
        yield cid, p + 8, size
        p += 8 + size


def _tags(d, p, end):
    while p + 4 <= end:
        tid, n = struct.unpack_from('<2sH', d, p)
        yield tid, d[p + 4:p + 4 + n]
        p += 4 + n


def _vec(b, o, n):
    return struct.unpack_from(f'<{n}f', b, o)


def _object_data(kind, b):
    c = struct.unpack_from('<4i', b, 0)
    o = {'color': (c[2], c[1], c[0], c[3]),           # RGBA
         'pivot': _vec(b, 16, 3), 'position': _vec(b, 28, 3),
         'rotation': _vec(b, 40, 4), 'size': _vec(b, 56, 3)}
    if len(b) >= 84:
        o['uv0'], o['uv1'] = _vec(b, 68, 2), _vec(b, 76, 2)
    if kind == 'coloured' and len(b) >= 148:
        for k in range(4):
            cc = struct.unpack_from('<4i', b, 84 + 16 * k)
            o[f'c{k}'] = (cc[2], cc[1], cc[0], cc[3])
    return o


def _track_key(ptype, b, o):
    if ptype == 6:
        c = struct.unpack_from('<4i', b, o)
        return (c[2], c[1], c[0], c[3])
    return struct.unpack_from(f'<{PARAM_SIZE[ptype] // 4}f', b, o)


def _script(b, end, start):
    s = {'tracks': {}, 'events': []}
    tracks = []
    for tid, v in _tags(b, start, end):
        if tid == b'Sh':
            s['id'], s['length'], s['flags'], _ = struct.unpack_from('<4I', v)
        elif tid == b'Sn':
            s['name'] = v.split(b'\0')[0].decode('latin-1')
        elif tid == b'Sc':
            s['chain'] = struct.unpack_from('<I', v)[0]
        elif tid == b'FI':
            ptype, _, interp, action, bits = struct.unpack_from('<4BI', v)
            tracks.append({'type': ptype, 'interp': interp, 'action': action, 'length': bits & 0xffffff, 'keys': []})
        elif tid == b'To':
            tracks[-1]['offset'] = struct.unpack_from('<I', v)[0]
        elif tid == b'Kd':
            t = tracks[-1]
            step = PARAM_SIZE[t['type']] + 4
            for o in range(0, len(v) - step + 1, step):
                t['keys'].append((struct.unpack_from('<i', v, o)[0], _track_key(t['type'], v, o + 4)))
        elif tid == b'EV':
            s['events'] = [struct.unpack_from('<3I', v, o) for o in range(0, len(v), 12)]
    for i, t in enumerate(tracks):
        name = TRACK_FIELDS.get(t['offset']) if 'offset' in t else (TRACK_ORDER[i] if i < len(TRACK_ORDER) else None)
        if name:
            s['tracks'][name] = t
    return s


def parse(d):
    """A package as {name, file, resources: [(id, type, name)], objects:
    [{guid, type, name, hash, flags, parent, resource, data, text, scripts}]}."""
    pkg = {'resources': [], 'objects': []}
    def walk(p, end):
        for cid, q, n in _chunks(d, p, end):
            if cid == b'FEn\xe7':
                walk(q, q + n)
            elif cid == b'PkHd':
                _, _, _, _, ln, lf = struct.unpack_from('<6I', d, q)
                pkg['name'] = d[q + 24:q + 24 + ln].split(b'\0')[0].decode('latin-1')
                pkg['file'] = d[q + 24 + ln:q + 24 + ln + lf].split(b'\0')[0].decode('latin-1')
            elif cid == b'Res\xcc':
                names = {}
                for c2, q2, n2 in _chunks(d, q, q + n):
                    if c2 == b'RsNm':
                        o = 0
                        while o < n2:
                            e = d.index(b'\0', q2 + o)
                            if e > q2 + o:
                                names[o] = d[q2 + o:e].decode('latin-1')
                            o = e - q2 + 1
                    elif c2 == b'RsRq':
                        count = struct.unpack_from('<I', d, q2)[0]
                        for k in range(count):
                            rid, off, rtype = struct.unpack_from('<3I', d, q2 + 4 + 24 * k)
                            pkg['resources'].append((rid, RESOURCE_TYPES.get(rtype, rtype), names.get(off, '')))
            elif cid == b'Obj\xcc':
                for c2, q2, n2 in _chunks(d, q, q + n):
                    if c2 != b'FOb\xea':
                        continue
                    obj = {'scripts': [], 'parent': None, 'resource': -1}
                    for c3, q3, n3 in _chunks(d, q2, q2 + n2):
                        if c3 == b'ObjD':
                            for tid, v in _tags(d, q3, q3 + n3):
                                if tid == b'Ot':
                                    obj['type'] = OBJECT_TYPES.get(struct.unpack_from('<I', v)[0], struct.unpack_from('<I', v)[0])
                                elif tid == b'OP':
                                    obj['guid'], obj['hash'], obj['flags'], obj['resource'] = struct.unpack_from('<3Ii', v)
                                elif tid == b'PA':
                                    obj['parent'] = struct.unpack_from('<I', v)[0]
                                elif tid == b'SA':
                                    obj['data'] = _object_data(obj.get('type'), v)
                                elif tid == b'On':
                                    obj['name'] = v.split(b'\0')[0].decode('latin-1')
                                elif tid == b'St':
                                    obj['text'] = v.decode('utf-16-le').split('\0')[0]
                                elif tid == b'Sj':
                                    obj['format'] = struct.unpack_from('<I', v)[0]
                                elif tid == b'Sl':
                                    obj['leading'] = struct.unpack_from('<i', v)[0]
                                elif tid == b'Sw':
                                    obj['max_width'] = struct.unpack_from('<I', v)[0]
                                elif tid == b'SH':
                                    obj['label_hash'] = struct.unpack_from('<I', v)[0]
                                elif tid == b'SL':
                                    obj['label'] = v.split(b'\0')[0].decode('latin-1')
                                elif tid == b'If':
                                    obj['image_flags'] = struct.unpack_from('<I', v)[0]
                                elif tid == b'Oh':
                                    obj['hash'] = struct.unpack_from('<I', v)[0]
                                elif tid in (b'M1', b'M2', b'M3'):
                                    obj.setdefault('multi', {})[tid.decode()] = struct.unpack_from('<I', v)[0]
                        elif c3 == b'Scrp':
                            obj['scripts'].append(_script(d, q3 + n3, q3))
                    pkg['objects'].append(obj)
    walk(0, len(d))
    return pkg
