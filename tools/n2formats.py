"""NFSU2 Xbox data formats. Python stdlib only; numpy, when installed,
gathers decoded texels faster (gather()).

Format research: OpenUG2/src/nfsu2.h (public domain). Xbox layouts were
verified against this project's USA disc; do not silently treat PC as Xbox.

The PS2 disc (SLUS_210.65) shares the container and most chunks; its own
layouts (CLUT textures, VIF meshes) are read when PLATFORM is 'ps2', which
tools/n2source.py sets from the disc a converter was given.
"""
import functools
import math
import os
import re
import struct

PLATFORM = os.environ.get('NFSU2_PLATFORM', 'xbox')   # n2source sets both, the workers a converter spawns inherit it


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
            start = len(out) - dist
            if count <= dist:      # a plain copy; an overlapping one repeats its bytes
                out += out[start:start + count]
            else:
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
    # Canonical codes: a table of every width-bit prefix -> (symbol, length),
    # read from a '0'/'1' string (the per-bit reader made this the
    # converters' slowest loop). Same codes, same output as before.
    table, c, idx = [None] * (1 << width), 0, 0
    for w in range(1, width + 1):
        for k in range(counts[w]):
            lo = (c + k) << (width - w)
            table[lo:lo + (1 << (width - w))] = [(values[idx + k], w)] * (1 << (width - w))
        c = (c + counts[w]) << 1
        idx += counts[w]
    data = d[16:]
    nbits = len(data) * 8
    bits = ''.join(map(BYTE_BITS.__getitem__, data)) + '0' * 32
    pos = s.pos

    def take(n):
        nonlocal pos
        if pos + n > nbits:
            raise ValueError('truncated Huffman stream')
        v = int(bits[pos:pos + n], 2) if n else 0
        pos += n
        return v

    def var():
        nonlocal pos
        n = 2
        while take(1) == 0:
            n += 1
            if n > 24:
                raise ValueError('invalid Huffman integer')
        return take(n) + (1 << n) - 4

    out = bytearray()
    while len(out) < expected:
        entry = table[int(bits[pos:pos + width], 2)]
        if entry is None:
            raise ValueError('invalid Huffman code')
        sym, w = entry
        if pos + w > nbits:
            raise ValueError('truncated Huffman stream')
        pos += w
        if sym != esc:
            out.append(sym)
        else:
            n = var()
            if n == 0:
                if take(1):
                    break
                out.append(take(8))
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


BYTE_BITS = [format(b, '08b') for b in range(256)]


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


try:
    import numpy
except ImportError:
    numpy = None


def gather(data, order, size=1):
    """bytes: data's size-byte item order[i] for each i (order: a tuple of
    item indices, or a numpy array of them)."""
    if numpy is not None:
        src = numpy.frombuffer(bytes(data[:len(data) // size * size]), numpy.uint8).reshape(-1, size)
        return src[order].tobytes()
    if size == 1:
        return bytes(map(bytes(data).__getitem__, order))
    items = [bytes(data[i:i + size]) for i in range(0, len(data) - size + 1, size)]
    return b''.join(map(items.__getitem__, order))


def index_table(values):
    """A gather() order from a list of indices."""
    return numpy.array(values, numpy.intp) if numpy is not None else tuple(values)


@functools.lru_cache(maxsize=32)
def morton_order(w, h):
    """The swizzled texel of each row-major texel."""
    xs, ys = morton_tables(w, h)
    return index_table([xs[x] | ys[y] for y in range(h) for x in range(w)])


def unswizzle(data, w, h, bpp):
    return bytearray(gather(data[:w * h * bpp], morton_order(w, h), bpp))


P8_FORMATS = (0x08, 0x80, 0x81)


class Texture:
    """One decoded texture. P8 keeps indices + RGBA palette; others RGBA."""
    def __init__(self, name, key, w, h, fmt, pixels, palette=None):
        self.name, self.key, self.w, self.h, self.fmt = name, key, w, h, fmt
        self.pixels, self.palette = pixels, palette

    def rgba(self):
        if self.palette is None:
            return self.pixels
        pal = bytes(self.palette[:1024]).ljust(1024, b'\0')
        return bytearray(gather(pal, index_table(self.pixels) if numpy is None else numpy.frombuffer(bytes(self.pixels), numpy.uint8), 4))


def decode_texture(name, key, info, image, palette_bytes):
    """info: bytes starting at the texture key. Shared by slot and flat TPKs.
    key+0x14 size, +0x20 w/h, +0x26 format."""
    if PLATFORM == 'ps2':
        return decode_ps2_texture(name, key, info, image, palette_bytes)
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



def ps2_unswizzle8(idx, w, h):
    """A PS2 8-bit texture as the game uploads it, PSMT8 written as PSMCT32
    (w/2 x h/2): its 16x16 blocks hold columns of 16x4 texels, rows paired
    and every other column pair shifted by 4. Returns row-major indices."""
    if w < 16 or h < 4:
        return bytes(idx[:w*h])
    return gather(idx[:w*h], ps2_order8(w, h))


@functools.lru_cache(maxsize=32)
def ps2_order8(w, h):
    """ps2_unswizzle8's source byte of each row-major texel."""
    order = []
    for y in range(h):
        block_row = (y & ~0xf) * w
        shift = (((y + 2) >> 2) & 1) * 4
        col = ((((y & ~3) >> 1) + (y & 1)) & 7) * w * 2
        byte = (y >> 1) & 1
        order += [block_row + (x & ~0xf) * 2 + col + ((x + shift) & 7) * 4 + byte + ((x >> 2) & 2) for x in range(w)]
    return index_table(order)


PS2_CLUT8 = [(i & ~0x18) | ((i & 8) << 1) | ((i & 16) >> 1) for i in range(256)]


def decode_ps2_texture(name, key, info, image, palette_bytes):
    """PS2 TPK record (the Xbox's fields, but its own +0x0c..+0x2a): +0x14
    pixel bytes, +0x18 palette bytes, +0x20 w/h, +0x26 bits per texel (4,
    8; 0x81 an 8-bit one too; 0x20 RGBA), u16 +0x28 palette entries, one
    level only (no mips). 4- and 8-bit texels are stored swizzled the
    PSMT8 way (a 4-bit one 32 or wider with each byte's two texels
    spread first),
    256-entry palettes in the GS's CSM1 order; colours RGBA with alpha
    0x80 = opaque, doubled here. Returns P8 Textures (fmt 0x100 | bits)
    with a 256-entry RGBA palette, or RGBA for 0x20."""
    w, h = struct.unpack_from('<HH', info, 0x20)
    fmt, colours = info[0x26], struct.unpack_from('<H', info, 0x28)[0]
    size = u32(info, 0x14)
    if not (1 <= w <= 1024 and 1 <= h <= 1024 and w & (w-1) == h & (h-1) == 0):
        raise ValueError(f'invalid PS2 texture dimensions ({name})')
    def alpha(c):
        return bytes((c[0], c[1], c[2], min(255, c[3] * 2)))
    if fmt == 0x20:
        if len(image) < w * h * 4:
            raise ValueError(f'truncated PS2 texture ({name})')
        px = bytearray(image[:w*h*4])
        px[3::4] = bytes(min(255, a * 2) for a in px[3::4])
        return Texture(name, key, w, h, 0x120, bytes(px))
    if fmt == 4:
        if size < w * h // 2 or colours != 16 or len(palette_bytes) < 64:
            raise ValueError(f'invalid PS2 4-bit texture ({name})')
        idx = bytearray(w * h)
        idx[0::2] = bytes(b & 15 for b in image[:w*h//2])
        idx[1::2] = bytes(b >> 4 for b in image[:w*h//2])
        pal = b''.join(alpha(palette_bytes[i*4:i*4+4]) for i in range(16)) + bytes(4 * 240)
    elif fmt in (8, 0x80, 0x81):
        if size < w * h or colours != 256 or len(palette_bytes) < 1024:
            raise ValueError(f'invalid PS2 8-bit texture ({name})')
        idx = image[:w*h]
        pal = b''.join(alpha(palette_bytes[j*4:j*4+4]) for j in PS2_CLUT8)
    else:
        raise ValueError(f'unsupported PS2 texture format {fmt:#x} ({name})')
    if fmt == 4 and w < 32:    # a 4-bit one narrower than 32 is stored as it is
        return Texture(name, key, w, h, 0x100 | fmt, bytes(idx), pal)
    return Texture(name, key, w, h, 0x100 | fmt, ps2_unswizzle8(idx, w, h), pal)


class SlotTPK:
    """Car-style TPK: every texture compressed separately (0x33310003 table).
    The info header sits at dec-HeaderFromEnd+0x6c (PC uses +0x88, the
    PS2 +0x54)."""
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
        p = dec - hfe + (0x54 if PLATFORM == 'ps2' else 0x6c)   # the PS2's header 0x18 bytes earlier
        if p + 0x34 > dec or u32(raw, p) != key:
            raise ValueError(f'{PLATFORM} texture header mismatch')
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

# ------------------------------------------------------------- PS2 meshes

def vif_unpacks(d):
    """The UNPACKs of a VIF stream, split at each MSCAL (one batch the
    VU1 draws): [[(vn, vl, num, addr, data)...]...]. vn 0-3 = S, V2, V3,
    V4; vl 0-2 = 32, 16, 8 bits."""
    out, cur, p = [], [], 0
    while p + 4 <= len(d):
        w = u32(d, p)
        p += 4
        cmd, num, imm = w >> 24, (w >> 16) & 0xff, w & 0xffff
        if cmd & 0x60 == 0x60:
            vn, vl, n = (cmd >> 2) & 3, cmd & 3, num or 256
            if vl == 3:
                raise ValueError('VIF V4-5 unpack in a mesh')
            size = ((32 >> vl) * (vn + 1) * n + 31) // 32 * 4
            cur.append((vn, vl, n, imm & 0x3ff, d[p:p+size]))
            p += size
        elif cmd in (0x14, 0x15, 0x17):          # MSCAL, MSCALF, MSCNT: the batch is drawn
            out.append(cur)
            cur = []
        elif cmd == 0x20:                        # STMASK
            p += 4
        elif cmd in (0x30, 0x31):                # STROW, STCOL
            p += 16
        elif cmd in (0x50, 0x51):                # DIRECT
            p += (imm or 65536) * 16
        elif cmd == 0x4a:                        # MPG
            p += (num or 256) * 8
    if cur:
        raise ValueError('VIF stream ends without MSCAL')
    return out


def _unpacked(vn, vl, n, data):
    count = n * (vn + 1)
    return struct.unpack_from('<%d%s' % (count, 'fhb'[vl]), data)


def ps2_adc(n, g):
    """The vertices of a batch that start a strip again (the GS's ADC, no
    triangle drawn), from the batch's flag words (its qword at VU address
    1, word 1 being the 0.0002 position scale). The words a vertex's bit
    lands in follow the batch's 4-vertex blocks B, as the shipped data
    holds them: v <= 14 at bit 14 - v of word 0 (word 3 for B = 4, word
    2 for B = 8), later ones at bit 4B + 1 - v of word 3. The 16th
    vertex of an 8-block batch has no bit of its own (ps2_mesh() finds it
    from the batch's triangle count)."""
    blocks, out = (n + 3) // 4, set()
    for v in range(n):
        if blocks <= 3:
            w, b = 0, 14 - v
        elif blocks == 4:
            w, b = 3, 14 - v
        elif v <= 14:
            w, b = (2 if blocks >= 8 else 0), 14 - v
        elif blocks >= 8 and v == 15:
            continue
        else:
            w, b = 3, 4 * blocks + 1 - v
        if 0 <= b < 32 and g[w] >> b & 1:
            out.add(v)
    return out


def ps2_batch(ops):
    """One batch's vertices: n, positions, UVs, RGBA colours (or None),
    normals (or None), its ADC flag words. VU addresses: 0 header (n), 1
    flags, 2-3 bounding box, 4 the culling cone (moving parts), 5-28
    positions, 29-44 UVs, 45-76 colours, 77- normals. Positions: u16 x
    the flags' word 1 (0.0002 on the cars, 1/256 on the city's big
    pieces) plus the box's minimum truncated less 1, per axis; positions
    and normals in 4-vertex blocks of 4 x, 4 y, 4 z; UVs 1/4096, a block
    being u0 v0 v1 u1 u2 v2 v3 u3; colours a vertex each, 0x80 = 1.0."""
    by = {}
    for vn, vl, num, addr, data in ops:
        by.setdefault(addr, []).append((vn, vl, num, data))
    n = by[0][0][3][0]
    g = struct.unpack_from('<4I', by[1][0][3])
    scale = struct.unpack_from('<f', by[1][0][3], 4)[0]
    box = _unpacked(*by[2][0][:3], by[2][0][3])
    base = [math.trunc(c) - 1 for c in box[:3]]

    def stream(lo, hi):
        vals = []
        for addr in sorted(a for a in by if lo <= a < hi):
            for vn, vl, num, data in by[addr]:
                vals.append((vn + 1, vl, _unpacked(vn, vl, num, data)))
        return vals

    pos = []
    for comps, vl, v in stream(5, 29):
        for blk in range(len(v) // (3 * comps)):
            xs, ys, zs = (v[(blk * 3 + k) * comps:(blk * 3 + k + 1) * comps] for k in range(3))
            if vl == 0:          # f32 positions (a few city pieces): as they are
                pos += list(zip(xs, ys, zs))
            else:
                pos += [((xs[i] & 0xffff) * scale + base[0], (ys[i] & 0xffff) * scale + base[1], (zs[i] & 0xffff) * scale + base[2])
                        for i in range(comps)]
    if len(pos) < n:
        raise ValueError(f'PS2 batch: {len(pos)} positions for {n} vertices')
    uvs = [x for _, _, v in stream(29, 45) for x in v]
    uv = []
    for i in range(n):
        b, k = divmod(i, 4)
        blk = uvs[b * 8:b * 8 + 8]
        uv.append((blk[(0, 3, 4, 7)[k]] / 4096, blk[(1, 2, 5, 6)[k]] / 4096))
    cols = [x for _, _, v in stream(45, 77) for x in v]
    rgba = [tuple(c & 0xff for c in cols[i * 4:i * 4 + 4]) for i in range(n)] if len(cols) >= 4 * n else None
    nv = [x for _, _, v in stream(77, 1024) for x in v]
    normals = None
    if len(nv) >= 12 * ((n - 1) // 4) + 9:
        normals = []
        for i in range(n):
            b, k = divmod(i, 4)
            normals.append(tuple(nv[b * 12 + 4 * a + k] / 127 for a in range(3)))
    return n, pos[:n], uv, rgba, normals, g


def ps2_mesh(leaves):
    """A PS2 object (0x134a00-a02) as the Xbox reader gives it: vertices
    (x, y, z, u, v, nx, ny, nz), colours (RGBA) and submeshes
    (indices, texture index, material index, flags). 0x134a01 holds 16
    bytes a batch: u8 flags @+0 (0x40: the wet street reflects on it, as
    the Xbox submesh's word @+0x20), u8 vertices @+2, u8 triangles @+3,
    u8 submesh @+13, texture @+14, material @+15. Each batch is a triangle strip; a vertex flagged
    by ps2_adc() starts it again, and the winding alternates from there.
    The prelit colours keep the Xbox's scale (its 0..255 equals the PS2's
    value up to 0x80; the PS2 clips the brighter ~20% of the city's
    vertices at 128, the Xbox goes on to 255); alpha 0x80 = 255."""
    table = unpad(leaves[0x134a01])
    batches = vif_unpacks(unpad(leaves[0x134a02]))
    if len(table) < 16 * len(batches):
        raise ValueError('PS2 mesh: fewer batch records than batches')
    verts, colours, subs = [], [], {}
    for bi, ops in enumerate(batches):
        rec = table[bi * 16:bi * 16 + 16]
        n, pos, uv, rgba, normals, g = ps2_batch(ops)
        if n != rec[2]:
            raise ValueError(f'PS2 mesh: batch of {n} vertices, its record says {rec[2]}')
        adc = ps2_adc(n, g)
        tris = [v for v in range(2, n) if v not in adc]
        if len(tris) == rec[3] + 1 and n > 16 and 15 not in adc:   # the 8-block batches' 16th vertex
            adc.add(15)
            tris = [v for v in range(2, n) if v not in adc]
        if len(tris) != rec[3]:
            raise ValueError(f'PS2 mesh: {len(tris)} triangles in a batch, its record says {rec[3]}')
        first = len(verts)
        for i in range(n):
            nx, ny, nz = normals[i] if normals else (0.0, 0.0, 1.0)
            verts.append((*pos[i], *uv[i], nx, ny, nz))
            colours.append((*rgba[i][:3], min(255, rgba[i][3] * 2)) if rgba else (255, 255, 255, 255))
        sub = subs.setdefault(rec[13], [[], rec[14], rec[15], rec[0]])
        sub[3] &= rec[0]   # a submesh's batches agree (the city's 5,328 compared with the Xbox's)
        ids = sub[0]
        start = 0
        for v in range(n):
            if v in adc and (v == 0 or v - 1 not in adc):
                start = v
            if v >= 2 and v not in adc:
                a, b, c = first + v - 2, first + v - 1, first + v
                ids += (a, b, c) if (v - start) % 2 == 0 else (b, a, c)
    return verts, colours, [tuple(subs[k]) for k in sorted(subs)]


def ps2_skinned(leaves):
    """A PS2 skinned model (the NIS people): vertices ((x, y, z), (w0, w1,
    w2), (bone, bone, bone), (u, v), normal) with the skeleton's bone
    numbers, and submeshes (indices, texture index). Positions and UVs are
    f32 whose low byte carries the skinning: x, y, z's the three bones
    (x 4), u's and v's the first two weights (/255, the third the rest).
    The strips and the normals as ps2_mesh's."""
    table = unpad(leaves[0x134a01])
    verts, subs = [], {}
    for bi, ops in enumerate(vif_unpacks(unpad(leaves[0x134a02]))):
        rec = table[bi * 16:bi * 16 + 16]
        by = {}
        for vn, vl, num, addr, data in ops:
            by.setdefault(addr, []).append((vn, vl, num, data))
        n = by[0][0][3][0]
        g = struct.unpack_from('<4I', by[1][0][3])

        def words(lo, hi):
            out = []
            for addr in sorted(a for a in by if lo <= a < hi):
                for vn, vl, num, data in by[addr]:
                    if vl != 0:
                        raise ValueError('PS2 skinned batch: not f32')
                    out.append((vn + 1, struct.unpack_from(f'<{num * (vn + 1)}I', data)))
            return out
        pos = []
        for comps, v in words(5, 29):
            for blk in range(len(v) // (3 * comps)):
                xs, ys, zs = (v[(blk * 3 + k) * comps:(blk * 3 + k + 1) * comps] for k in range(3))
                pos += list(zip(xs, ys, zs))
        uvw = [x for _, v in words(29, 45) for x in v]
        nv = [x for _, _, _, d in (o for a in sorted(by) if a >= 77 for o in by[a]) for x in struct.unpack(f'<{len(d)}b', d)]
        f32 = lambda w: struct.unpack('<f', struct.pack('<I', w & ~0xff))[0]
        first = len(verts)
        for i in range(n):
            b, k = divmod(i, 4)
            blk = uvw[b * 8:b * 8 + 8]
            uw, vw = blk[(0, 3, 4, 7)[k]], blk[(1, 2, 5, 6)[k]]
            w0, w1 = (uw & 0xff) / 255, (vw & 0xff) / 255
            normal = tuple(nv[b * 12 + 4 * a + k] / 127 for a in range(3)) if len(nv) >= b * 12 + 12 else (0.0, 0.0, 1.0)
            verts.append((tuple(f32(c) for c in pos[i]), (w0, w1, max(0.0, 1 - w0 - w1)), tuple((c & 0xff) >> 2 for c in pos[i]),
                          (f32(uw), f32(vw)), normal))
        adc = ps2_adc(n, g)
        tris = [v for v in range(2, n) if v not in adc]
        if len(tris) == rec[3] + 1 and n > 16 and 15 not in adc:
            adc.add(15)
        ids = subs.setdefault(rec[13], [[], rec[14]])[0]
        start = 0
        for v in range(n):
            if v in adc and (v == 0 or v - 1 not in adc):
                start = v
            if v >= 2 and v not in adc:
                a, b_, c = first + v - 2, first + v - 1, first + v
                ids += (a, b_, c) if (v - start) % 2 == 0 else (b_, a, c)
    return verts, [tuple(subs[k]) for k in sorted(subs)]


def ps2_world(leaves):
    """A PS2 world object as the Xbox's prelit layout: vertices (x, y, z,
    0xAARRGGBB, u, v) and submeshes (indices, texture index, flags)."""
    verts, colours, subs = ps2_mesh(leaves)
    out = [(x, y, z, a << 24 | r << 16 | g << 8 | b, u, v) for (x, y, z, u, v, *_), (r, g, b, a) in zip(verts, colours)]
    return out, [(ids, ti, flags) for ids, ti, mi, flags in subs]


class Part:
    """One 0x80134010 object. verts: (x,y,z,u,v,nx,ny,nz) in Xbox car space
    (X forward, Y lateral/left, Z up). subs: (indices, texkey, matkey)."""
    def __init__(self, name, verts, subs, markers):
        self.name, self.verts, self.subs = name, verts, subs
        self.markers = list(markers.items() if isinstance(markers, dict) else markers)   # [(key, matrix)], every one, as the file has them
        self.sockets = dict(self.markers)   # key -> matrix (a key twice: the last)
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
        if not accept(name):
            continue
        if 0x134a02 in leaves:      # the PS2's: VIF strips (ps2_mesh)
            tex = [a for a, b in struct.iter_unpack('<II', leaves[0x134012])]
            mats = [a for a, b in struct.iter_unpack('<II', leaves[0x134013])] if 0x134013 in leaves else []
            verts, _, psubs = ps2_mesh(leaves)
            if not verts:
                skipped.append((name, 'empty'))
                continue
            subs = [(tuple(ids), tex[ti], mats[mi] if mi < len(mats) else 0) for ids, ti, mi, _ in psubs if ids]
            yield Part(name, verts, subs, sockets_of(leaves, name))
            continue
        if 0x134b01 not in leaves:
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
        yield Part(name, verts, subs, sockets_of(leaves, name))


def sockets_of(leaves, name):
    """A part's sockets (0x13401a, the same on both discs): [(key,
    matrix)] in the file's order. A key can come twice: the SUV's two
    headlamps a side under one name, its two tail lamps both
    BRAKELIGHT_RIGHT (eModel::GetPostionMarker walks them all)."""
    sockets = []
    if 0x13401a in leaves:
        s = unpad(leaves[0x13401a])
        for at in range(0, len(s) - 79, 80):
            m = struct.unpack_from('<16f', s, at + 16)
            if abs(m[15] - 1) > 1e-3 or any(abs(m[i]) > 1e-3 for i in (3, 7, 11)):
                raise ValueError(f'invalid socket matrix: {name}')
            sockets.append((u32(s, at), m))
    return sockets


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


def wheel_faces(global_data, car):
    """The car record's wheel faces (+0x1c, +0x20 of each wheel at
    +0x120+0x30*k): how far out, signed as the wheel's y, the game draws
    each wheel's outer face, on the stock body and under a wide body kit
    (CarRenderInfo::UpdateWheelYRenderOffset)."""
    table = [global_data[p:p+n] for t, p, n in chunks(global_data) if t == 0x34600][0][8:]
    for at in range(0, len(table), 0x890):
        if table[at:at+32].split(b'\0')[0] == car.encode():
            faces = [struct.unpack_from('<2f', table, at + 0x13c + 0x30 * k) for k in range(4)]
            if not all(.4 < abs(s) < 1.5 and .4 < abs(w) < 1.5 for s, w in faces):
                raise ValueError('wheel face mismatch')
            return faces
    raise ValueError(car + ' physics record absent')


def tyre_size(global_data, car):
    """The car record's byte +0xdc: its tyres' outer diameter in inches
    (the COROLLA's 24, the 350Z's 26, the HUMMER's 34). The rim shop lists
    the rims of that diameter alone, their names' second number
    (ENKEI_STYLE01_17_24: a 17 in rim in a 24 in tyre), as the parts
    table's OUTER_RADIUS (GetIcePartsList)."""
    table = [global_data[p:p+n] for t, p, n in chunks(global_data) if t == 0x34600][0][8:]
    for at in range(0, len(table), 0x890):
        if table[at:at+32].split(b'\0')[0] == car.encode():
            size = table[at + 0xdc]
            if not 20 <= size <= 40:
                raise ValueError(f'{car}: tyre size {size}')
            return size
    raise ValueError(car + ' physics record absent')


def envmap_faces(data):
    """FRONTEND/ENVMAPS/*.BIN: six BGRA cube faces named XPOS..ZNEG."""
    tpk = FlatTPK(data)
    return {rec[0]: tpk.get(key) for key, rec in tpk.records.items()}
