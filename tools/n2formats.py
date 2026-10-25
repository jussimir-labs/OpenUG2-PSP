"""NFSU2 Xbox data formats. Python stdlib only.

Format research: OpenUG2/src/nfsu2.h (public domain). Xbox layouts were
verified against this project's USA disc; do not silently treat PC as Xbox.
"""
import math
import re
import struct


def u32(d, p=0):
    return struct.unpack_from('<I', d, p)[0]


def game_hash(name):
    h = 0xffffffff
    for c in name.upper().replace('/', '\\').encode('ascii'):
        h = (h * 33 + c) & 0xffffffff
    return h


def archived(root, name):
    z = (root / 'ZDIR.BIN').read_bytes()
    if len(z) % 24:
        raise ValueError('invalid ZDIR length')
    matches = [e for e in struct.iter_unpack('<6I', z) if e[0] == game_hash(name)]
    if not matches:
        raise ValueError(f'not found in ZDIR: {name}')
    # Duplicate entries are streaming copies. Require equal length/content.
    result = None
    for _, archive, sector, _, size, _ in matches:
        if archive > 9 or size > 32 * 1024 * 1024:
            raise ValueError('unsupported archive/size')
        path = root / f'ZZDATA{archive}.BIN'
        if sector * 2048 + size > path.stat().st_size:
            raise ValueError('archive range outside file')
        with path.open('rb') as f:
            f.seek(sector * 2048)
            data = f.read(size)
        if result is not None and result != data:
            raise ValueError(f'different streaming copies: {name}')
        result = data
    return result


def chunks(d, start=0, end=None, depth=0):
    end = len(d) if end is None else end
    if depth > 16:
        raise ValueError('chunk nesting too deep')
    while start + 8 <= end:
        tag, size = struct.unpack_from('<II', d, start)
        p = start + 8
        if p + size > end:
            raise ValueError(f'chunk exceeds container at {start:x}')
        yield tag, p, size
        if tag & 0x80000000:
            yield from chunks(d, p, p + size, depth + 1)
        start = p + size
    if start != end:
        raise ValueError('partial chunk header')


def unpad(d):
    p = 0
    while d[p:p+4] == b'\x11' * 4:
        p += 4
    return d[p:]


# ---------------------------------------------------------------- compression

def jdlz(d, expected):
    if len(d) < 16 or d[:4] != b'JDLZ' or u32(d, 8) != expected:
        raise ValueError('invalid JDLZ wrapper')
    out = bytearray()
    p, f1, f2 = 16, 1, 1
    while len(out) < expected:
        if f1 == 1:
            f1 = d[p] | 256
            p += 1
        if f2 == 1:
            f2 = d[p] | 256
            p += 1
        if f1 & 1:
            a, b = d[p:p+2]
            p += 2
            if f2 & 1:
                count, dist = (b | ((a & 240) << 4)) + 3, (a & 15) + 1
            else:
                count, dist = (a & 31) + 3, (b | ((a & 224) << 3)) + 17
            if dist > len(out) or len(out) + count > expected:
                raise ValueError('invalid JDLZ backreference')
            for _ in range(count):
                out.append(out[-dist])
            f2 >>= 1
        else:
            out.append(d[p])
            p += 1
        f1 >>= 1
    return bytes(out)


class Bits:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def get(self, n):
        if self.pos + n > len(self.data) * 8:
            raise ValueError('truncated Huffman stream')
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.data[self.pos >> 3] >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def var(self):
        n = 2
        while self.get(1) == 0:
            n += 1
            if n > 24:
                raise ValueError('invalid Huffman integer')
        return self.get(n) + (1 << n) - 4


def huff(d, expected):
    if d[:4] != b'HUFF' or u32(d, 8) != expected:
        raise ValueError('invalid HUFF wrapper')
    s = Bits(d[16:])
    method = s.get(16)
    if method & 0xf0ff != 0x30fb:
        raise ValueError('unsupported Huffman method')
    if method & 256:
        s.get(24)
    if s.get(24) != expected:
        raise ValueError('Huffman size mismatch')
    esc = s.get(8)
    counts, code, width = [0] * 17, 0, 0
    while width < 16 and code << (16 - width) < 65536:
        width += 1
        counts[width] = s.var()
        code = code * 2 + counts[width]
    total = sum(counts)
    if not 1 <= total <= 256 or code != 1 << width:
        raise ValueError('invalid Huffman tree')
    values, defined, dat = [], set(), 255
    for _ in range(total):
        n = s.var() + 1
        if n > 256 - len(defined):
            raise ValueError('invalid Huffman symbols')
        while n:
            dat = (dat + 1) & 255
            if dat not in defined:
                n -= 1
        defined.add(dat)
        values.append(dat)
    first, offset, c, idx = {}, {}, 0, 0
    for w in range(1, width + 1):
        first[w], offset[w] = c, idx
        c = (c + counts[w]) << 1
        idx += counts[w]
    out = bytearray()
    while len(out) < expected:
        v = 0
        for w in range(1, width + 1):
            v = (v << 1) | s.get(1)
            if 0 <= v - first[w] < counts[w]:
                sym = values[offset[w] + v - first[w]]
                break
        else:
            raise ValueError('invalid Huffman code')
        if sym != esc:
            out.append(sym)
        else:
            n = s.var()
            if n == 0:
                if s.get(1):
                    break
                out.append(s.get(8))
            else:
                if not out or len(out) + n > expected:
                    raise ValueError('invalid Huffman repeat')
                out.extend(bytes([out[-1]]) * n)
    if len(out) != expected:
        raise ValueError('Huffman output size mismatch')
    if method & 0xfeff in (0x32fb, 0x34fb):
        x = y = 0
        for i, b in enumerate(out):
            x = (x + b) & 255
            y = (y + x) & 255
            out[i] = y if method & 0xfeff == 0x34fb else x
    return bytes(out)


def decompress(blob, expected):
    return jdlz(blob, expected) if blob[:4] == b'JDLZ' else huff(blob, expected)


# ------------------------------------------------------------------- textures

def dxt(raw, w, h, fmt):
    bpb = 8 if fmt == 0x22 else 16
    if fmt not in (0x22, 0x24, 0x26) or len(raw) < ((w+3)//4)*((h+3)//4)*bpb:
        raise ValueError(f'unsupported/truncated Xbox texture: {fmt:x}')
    rgba = bytearray(w * h * 4)
    at = 0
    def rgb(v):
        return [(v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31]
    for y in range(0, h, 4):
        for x in range(0, w, 4):
            b = raw[at:at+bpb]
            at += bpb
            color = b[-8:]
            c0, c1, inds = struct.unpack('<HHI', color)
            p = [rgb(c0)+[255], rgb(c1)+[255]]
            if c0 > c1 or fmt != 0x22:
                p += [[(2*a+b)//3 for a,b in zip(p[0][:3],p[1][:3])]+[255],
                      [(a+2*b)//3 for a,b in zip(p[0][:3],p[1][:3])]+[255]]
            else:
                p += [[(a+b)//2 for a,b in zip(p[0][:3],p[1][:3])]+[255], [0,0,0,0]]
            alphas = [255] * 16
            if fmt == 0x24:
                bits = int.from_bytes(b[:8], 'little')
                alphas = [((bits >> (4*i)) & 15) * 17 for i in range(16)]
            elif fmt == 0x26:
                a0, a1 = b[:2]
                ap = [a0, a1]
                if a0 > a1:
                    ap += [((7-i)*a0+i*a1)//7 for i in range(1,7)]
                else:
                    ap += [((5-i)*a0+i*a1)//5 for i in range(1,5)] + [0,255]
                bits = int.from_bytes(b[2:8], 'little')
                alphas = [ap[(bits >> (3*i)) & 7] for i in range(16)]
            for i in range(16):
                xx, yy = x+i%4, y+i//4
                if xx < w and yy < h:
                    col = p[(inds >> (2*i)) & 3][:]
                    if fmt != 0x22:
                        col[3] = alphas[i]
                    rgba[(yy*w+xx)*4:(yy*w+xx)*4+4] = bytes(col)
    return rgba


def morton_tables(w, h):
    """Xbox swizzle: interleave x/y bits while both axes still have bits."""
    def offset(x, y):
        o = bit = 0
        mx, my = w >> 1, h >> 1
        while mx or my:
            if mx:
                o |= (x & 1) << bit
                x >>= 1
                bit += 1
                mx >>= 1
            if my:
                o |= (y & 1) << bit
                y >>= 1
                bit += 1
                my >>= 1
        return o
    return [offset(x, 0) for x in range(w)], [offset(0, y) for y in range(h)]


def unswizzle(data, w, h, bpp):
    xs, ys = morton_tables(w, h)
    out = bytearray(w * h * bpp)
    for y in range(h):
        row = ys[y]
        for x in range(w):
            q = (xs[x] | row) * bpp
            out[(y*w+x)*bpp:(y*w+x+1)*bpp] = data[q:q+bpp]
    return out


P8_FORMATS = (0x08, 0x80, 0x81)


class Texture:
    """One decoded texture. P8 keeps indices + RGBA palette; others RGBA."""
    def __init__(self, name, key, w, h, fmt, pixels, palette=None):
        self.name, self.key, self.w, self.h, self.fmt = name, key, w, h, fmt
        self.pixels, self.palette = pixels, palette

    def rgba(self):
        if self.palette is None:
            return self.pixels
        out = bytearray(self.w * self.h * 4)
        pal = self.palette
        for i, c in enumerate(self.pixels):
            out[i*4:i*4+4] = pal[c*4:c*4+4]
        return out


def decode_texture(name, key, info, image, palette_bytes):
    """info: bytes starting at the texture key. Shared by slot and flat TPKs.
    key+0x14 size, +0x20 w/h, +0x26 format."""
    w, h = struct.unpack_from('<HH', info, 0x20)
    fmt = info[0x26]
    if not (4 <= w <= 1024 and 4 <= h <= 1024 and w & (w-1) == h & (h-1) == 0):
        raise ValueError('invalid texture dimensions')
    if fmt in (0x22, 0x24, 0x26):
        return Texture(name, key, w, h, fmt, dxt(image, w, h, fmt))
    if fmt == 0x20:
        bgra = unswizzle(image[:w*h*4], w, h, 4)
        for i in range(0, len(bgra), 4):
            bgra[i], bgra[i+2] = bgra[i+2], bgra[i]
        return Texture(name, key, w, h, fmt, bgra)
    if fmt in P8_FORMATS:
        if len(palette_bytes) < 1024:
            raise ValueError('P8 texture without palette')
        pal = bytearray(palette_bytes[:1024])
        for i in range(0, 1024, 4):          # Xbox D3DCOLOR is BGRA
            pal[i], pal[i+2] = pal[i+2], pal[i]
        return Texture(name, key, w, h, fmt, unswizzle(image[:w*h], w, h, 1), bytes(pal))
    raise ValueError(f'unsupported texture format {fmt:#x} ({name})')


class SlotTPK:
    """Car-style TPK: every texture compressed separately (0x33310003 table).
    The info header sits at dec-HeaderFromEnd+0x6c (PC uses +0x88)."""
    def __init__(self, data):
        self.data = data
        leaves = {t: data[p:p+n] for t, p, n in chunks(data) if not t & 0x80000000}
        table = leaves[0x33310003]
        if len(table) % 24:
            raise ValueError('invalid texture table')
        self.slots = {}
        for key, off, enc, dec, hfe, _ in struct.iter_unpack('<6I', table):
            if off + enc > len(data) or dec > 8 * 1024 * 1024 or dec < hfe:
                raise ValueError('invalid texture range')
            self.slots[key] = (off, enc, dec, hfe)
        self.cache = {}

    def raw(self, key):
        off, enc, dec, hfe = self.slots[key]
        raw = decompress(self.data[off:off+enc], dec)
        p = dec - hfe + 0x6c
        if p + 0x34 > dec or u32(raw, p) != key:
            raise ValueError('Xbox texture header mismatch')
        return raw, p

    def name(self, key):
        raw, p = self.raw(key)
        return raw[p-24:p].split(b'\0')[0].decode('ascii')

    def get(self, key):
        if key not in self.slots:
            return None
        if key not in self.cache:
            raw, p = self.raw(key)
            info = raw[p:]
            img_off, pal_off = u32(info, 12), u32(info, 16)
            pal = raw[pal_off-img_off:pal_off-img_off+1024] if u32(info, 24) else b''
            name = raw[p-24:p].split(b'\0')[0].decode('ascii')
            self.cache[key] = decode_texture(name, key, info, raw, pal)
        return self.cache[key]


class FlatTPK:
    """Uncompressed TPK (GLOBALB, ENVMAPS): 124-byte Xbox records with a
    12-byte prefix, name@+0x0c, key@+0x24; pixel block after 0x11 filler."""
    def __init__(self, data):
        self.data, self.records, self.cache = data, {}, {}
        for t, p, n in chunks(data):
            if t != 0xb3300000:
                continue
            recs, base = None, None
            for t2, p2, n2 in chunks(data, p, p + n):
                if t2 == 0x33310004:
                    recs = data[p2:p2+n2]
                elif t2 == 0x33320002:
                    base = p2 + (n2 - len(unpad(data[p2:p2+n2])))
            if recs is None or base is None or len(recs) % 124:
                raise ValueError('invalid flat TPK')
            for at in range(0, len(recs), 124):
                r = recs[at:at+124]
                key = u32(r, 0x24)
                self.records[key] = (r[12:36].split(b'\0')[0].decode('ascii', 'replace'), r[0x24:], base)

    def get(self, key):
        if key not in self.records:
            return None
        if key not in self.cache:
            name, info, base = self.records[key]
            img_off, pal_off, size, pal_size = struct.unpack_from('<4I', info, 12)
            image = self.data[base+img_off:base+img_off+size]
            pal = self.data[base+pal_off:base+pal_off+1024] if pal_size else b''
            self.cache[key] = decode_texture(name, key, info, image, pal)
        return self.cache[key]

    def by_name(self, name):
        for key, rec in self.records.items():
            if rec[0] == name:
                return self.get(key)
        return None


# ------------------------------------------------------------------- geometry

class Part:
    """One 0x80134010 object. verts: (x,y,z,u,v,nx,ny,nz) in Xbox car space
    (X forward, Y lateral/left, Z up). subs: (indices, texkey, matkey)."""
    def __init__(self, name, verts, subs, sockets):
        self.name, self.verts, self.subs, self.sockets = name, verts, subs, sockets
        self.triangles = sum(len(s[0]) for s in subs) // 3


# Parts parts() left out: (name, reason). Two retail cars carry one odd part
# each, neither drawn: 3000GT_KIT00_ENGINE_B (a vertex buffer of 238.5
# vertices) and RSX_W01_CV (empty buffers).
skipped = []


def parts(data, accept=lambda name: True):
    """Yield Parts of an Xbox geometry file whose name passes accept()."""
    for tag, p, n in chunks(data):
        if tag != 0x80134010:
            continue
        leaves = {t: data[o:o+s] for t, o, s in chunks(data, p, p+n) if not t & 0x80000000}
        name = unpad(leaves[0x134011])[164:].split(b'\0')[0].decode('ascii')
        if not accept(name) or 0x134b01 not in leaves:
            continue
        vb, ib, sb = [unpad(leaves[t]) for t in (0x134b01, 0x134b03, 0x134b02)]
        if not vb and not ib:
            skipped.append((name, 'empty'))
            continue
        if len(vb) % 32 and name.endswith(('_ENGINE', '_ENGINE_A', '_ENGINE_B')):
            skipped.append((name, f'vertex buffer of {len(vb)} bytes'))
            continue
        if len(vb) % 32 or len(ib) % 2 or len(sb) % 64:
            raise ValueError(f'Xbox geometry layout mismatch: {name}')
        verts = list(struct.iter_unpack('<8f', vb))  # position3, UV2, normal3
        ids = struct.unpack('<' + str(len(ib)//2) + 'H', ib)
        if not verts or max(ids) >= len(verts) or any(not math.isfinite(f) for v in verts for f in v):
            raise ValueError(f'invalid vertices/indices: {name}')
        tex = [a for a, b in struct.iter_unpack('<II', leaves[0x134012])]
        mats = [a for a, b in struct.iter_unpack('<II', leaves[0x134013])]
        subs, expected = [], 0
        for at in range(0, len(sb), 64):
            words = struct.unpack_from('<16I', sb, at)
            count, ti, first, mi = words[6], words[7], words[9], words[11]
            if first != expected or count % 3 or first + count > len(ids) or ti >= len(tex) or mi >= len(mats):
                raise ValueError(f'invalid submesh partition: {name}')
            expected += count
            subs.append((ids[first:first+count], tex[ti], mats[mi]))
        if len(ids) - expected >= 3:
            raise ValueError(f'unclaimed indices: {name}')
        sockets = {}
        if 0x13401a in leaves:
            s = unpad(leaves[0x13401a])
            for at in range(0, len(s) - 79, 80):
                m = struct.unpack_from('<16f', s, at + 16)
                if abs(m[15] - 1) > 1e-3 or any(abs(m[i]) > 1e-3 for i in (3, 7, 11)):
                    raise ValueError(f'invalid socket matrix: {name}')
                sockets[u32(s, at)] = m
        yield Part(name, verts, subs, sockets)


def lod_family(name):
    """Name without the _A.._D level suffix. Names are truncated at 27
    characters, so a family may also end in '_' with no level letter."""
    return re.sub(r'_[ABCD]$', '', name)


def car_record(global_data, car):
    """GLOBALB 0x34600 car table: 8 filler bytes, then 0x890-byte records.
    Wheels at +0x120+0x30*k: x, y, z, ?, radius, width, index."""
    tables = [global_data[p:p+n] for t, p, n in chunks(global_data) if t == 0x34600]
    if len(tables) != 1 or tables[0][:8] != b'\x11' * 8 or (len(tables[0]) - 8) % 0x890:
        raise ValueError('GLOBALB car table mismatch')
    table = tables[0][8:]
    for at in range(0, len(table), 0x890):
        if table[at:at+32].split(b'\0')[0] != car.encode():
            continue
        wheels = []
        for k in range(4):
            x, y, z, _, r, w, i = struct.unpack_from('<6fI', table, at + 0x120 + 0x30 * k)
            if i != k or not (.2 < r < .5 and .1 < w < .4 and .4 < abs(y) < 1.3 and .3 < abs(x) < 2.5):
                raise ValueError('wheel record mismatch')
            wheels.append((x, y, z, r, w))
        return wheels
    raise ValueError(car + ' physics record absent')


def envmap_faces(data):
    """FRONTEND/ENVMAPS/*.BIN: six BGRA cube faces named XPOS..ZNEG."""
    tpk = FlatTPK(data)
    return {rec[0]: tpk.get(key) for key, rec in tpk.records.items()}
