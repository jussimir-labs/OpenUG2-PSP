"""The game's executable read by virtual address: the Xbox default.xbe or
the PS2 SLUS_210.65 (an ELF). Both are 32-bit little-endian builds of the
same code, so the tables the converters read from it (the sound tables,
the help screens, the racers' names...) have the same layout and are
found the same way: by a string a table points at, or by its values.
Stdlib only.
"""
import struct

import n2source


class Exe:
    def __init__(self, path):
        self.path = path
        self.b = b = path.read_bytes()
        if b[:4] == b'XBEH':
            base, count, at = struct.unpack_from('<I', b, 0x104)[0], *struct.unpack_from('<2I', b, 0x11c)
            # (vaddr, vsize, raw offset, raw size)
            self.secs = [struct.unpack_from('<4I', b, at - base + 56 * k + 4) for k in range(count)]
            self.code = self.gp = None
        elif b[:4] == b'\x7fELF':
            phoff, shoff = struct.unpack_from('<I', b, 0x1c)[0], struct.unpack_from('<I', b, 0x20)[0]
            phsize, phnum, shsize, shnum, shstr = struct.unpack_from('<5H', b, 0x2a)
            self.secs = []
            for k in range(phnum):
                kind, off, vaddr, _, filesz, memsz = struct.unpack_from('<6I', b, phoff + phsize * k)
                if kind == 1 and filesz:     # PT_LOAD
                    self.secs.append((vaddr, memsz, off, filesz))
            # .text's range (the initialisers n2mips runs live there)
            sh = [struct.unpack_from('<10I', b, shoff + shsize * k) for k in range(shnum)]
            names = sh[shstr][4] if shnum else 0
            named = {b[names + h[0]:b.index(b'\0', names + h[0])]: h for h in sh}
            self.code = (named[b'.text'][3], named[b'.text'][3] + named[b'.text'][5]) if b'.text' in named else None
            # $gp as the linker set it (.reginfo's ri_gp_value)
            self.gp = struct.unpack_from('<I', b, named[b'.reginfo'][4] + 20)[0] if b'.reginfo' in named else None
        else:
            raise ValueError(f'{path}: neither an XBE nor an ELF')

    def overlay(self, mem):
        """Read the stores n2mips ran ({address: byte}) as part of the
        image: a section over each run of them inside the program's
        memory (the stack's left out), the file's bytes under them."""
        top = max(a + vs for a, vs, r, n in self.secs)
        addrs = sorted(a for a in mem if a < top)
        runs, start = [], None
        for k, a in enumerate(addrs):
            if start is None:
                start = a
            if k + 1 == len(addrs) or addrs[k + 1] - a > 4096:
                runs.append((start, a + 1))
                start = None
        for lo, hi in runs:
            blob = bytearray(hi - lo)
            for a in range(lo, hi):
                o = self.raw(a)
                if o is not None:
                    blob[a - lo] = self.b[o]
                if a in mem:
                    blob[a - lo] = mem[a]
            self.secs.insert(0, (lo, hi - lo, len(self.b), hi - lo))
            self.b = self.b + bytes(blob)
        return self

    def raw(self, v):
        """The file offset of virtual address v (None outside the file)."""
        return next((r + v - a for a, vs, r, n in self.secs if a <= v < a + n), None)

    def va(self, off):
        return next(a + off - r for a, vs, r, n in self.secs if r <= off < r + n)

    def cstr(self, v):
        o = self.raw(v)
        return self.b[o:self.b.index(b'\0', o)].decode('latin-1') if o is not None else ''

    def refs(self, text):
        """Every aligned word pointing at the string text (exact, NUL-ended)."""
        out, s = [], text.encode('latin-1') + b'\0'
        at = self.b.find(s)
        while at >= 0:
            if at == 0 or self.b[at - 1] == 0 or text[:1] == '\0':
                v = struct.pack('<I', self.va(at))
                p = self.b.find(v)
                while p >= 0:
                    if p % 4 == 0:
                        out.append(p)
                    p = self.b.find(v, p + 1)
            at = self.b.find(s, at + 1)
        return sorted(out)

    def ref(self, text):
        """The first word pointing at the string text (a table's first row)."""
        r = self.refs(text)
        if not r:
            raise ValueError(f'{self.path.name}: nothing points at {text}')
        return r[0]

    def find(self, values, fmt):
        at = self.b.find(struct.pack(f'<{len(values)}{fmt}', *values))
        if at < 0:
            raise ValueError(f'{self.path.name}: {values} not found')
        return at


def of(game, anchors=()):
    """The executable beside the disc's NFSUNDER folder. anchors: strings
    whose tables the PS2's executable fills in code (n2mips runs the
    functions that use them, and their stores read as data)."""
    exe = Exe(n2source.executable(game))
    if anchors and exe.b[:4] == b'\x7fELF':
        import n2mips
        exe.overlay(n2mips.initialise(exe, anchors))
    return exe
