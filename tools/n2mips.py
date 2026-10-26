"""Run a PS2 static initialiser to read the tables it fills.

The PS2 build of the game (SLUS_210.65) keeps some tables the Xbox has
as initialised data (the car sound tables, the rev patterns) as code: a
static initialiser that stores each field. initialise() runs such a
function on a tiny R5900 interpreter (integer, load/store and the FPU's
moves; calls are skipped) over the executable's image and returns the
words it stored, which n2exe.Exe can then read as data. Stdlib only.
"""
import struct


class Stop(Exception):
    pass


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


class Cpu:
    def __init__(self, exe, gp):
        self.exe, self.mem = exe, {}        # mem: byte address -> byte, the stores
        self.r = [0] * 32
        self.f = [0] * 32                   # FPU registers as raw words
        self.r[28], self.r[29] = gp, 0x7fff0000

    def load(self, a, n):
        out = bytearray()
        for k in range(n):
            if a + k in self.mem:
                out.append(self.mem[a + k])
            else:
                o = self.exe.raw(a + k)
                out.append(self.exe.b[o] if o is not None else 0)
        return bytes(out)

    def store(self, a, data):
        for k, c in enumerate(data):
            self.mem[a + k] = c

    def run(self, pc, steps=200000):
        r = self.r
        delay = None
        for _ in range(steps):
            o = self.exe.raw(pc)
            if o is None:
                raise Stop(f'pc {pc:#x} outside the image')
            w, = struct.unpack_from('<I', self.exe.b, o)
            nxt = pc + 4
            op, rs, rt, rd, sa, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
            imm, target = w & 0xffff, None
            u = lambda v: v & 0xffffffff
            if w == 0:
                pass
            elif op == 0:
                if fn in (0x21, 0x2d, 0x20, 0x2c):          # addu daddu add dadd
                    r[rd] = u(r[rs] + r[rt])
                elif fn in (0x23, 0x2f, 0x22):              # subu dsubu sub
                    r[rd] = u(r[rs] - r[rt])
                elif fn == 0x25:
                    r[rd] = r[rs] | r[rt]
                elif fn == 0x24:
                    r[rd] = r[rs] & r[rt]
                elif fn == 0x26:
                    r[rd] = r[rs] ^ r[rt]
                elif fn == 0x27:
                    r[rd] = u(~(r[rs] | r[rt]))
                elif fn == 0x00:
                    r[rd] = u(r[rt] << sa)
                elif fn == 0x02:
                    r[rd] = r[rt] >> sa
                elif fn == 0x03:
                    r[rd] = u((r[rt] - (1 << 32) if r[rt] >> 31 else r[rt]) >> sa)
                elif fn == 0x2a:
                    s = lambda v: v - (1 << 32) if v >> 31 else v
                    r[rd] = int(s(r[rs]) < s(r[rt]))
                elif fn == 0x2b:
                    r[rd] = int(r[rs] < r[rt])
                elif fn == 0x08:                            # jr
                    if rs == 31:
                        target = 'ret'
                    else:
                        raise Stop('jr through a register')
                elif fn == 0x09:                            # jalr: skipped
                    r[2] = 0
                elif fn in (0x0f,):                         # sync
                    pass
                else:
                    raise Stop(f'special {fn:#x} at {pc:#x}')
            elif op == 0x0f:
                r[rt] = u(imm << 16)
            elif op in (0x09, 0x19, 0x08, 0x18):            # addiu daddiu addi daddi
                r[rt] = u(r[rs] + sx16(imm))
            elif op == 0x0d:
                r[rt] = r[rs] | imm
            elif op == 0x0c:
                r[rt] = r[rs] & imm
            elif op == 0x0e:
                r[rt] = r[rs] ^ imm
            elif op in (0x0a, 0x0b):                        # slti sltiu
                s = lambda v: v - (1 << 32) if v >> 31 else v
                r[rt] = int(s(r[rs]) < sx16(imm)) if op == 0x0a else int(r[rs] < u(sx16(imm)))
            elif op in (0x2b, 0x29, 0x28, 0x3f, 0x1f):      # sw sh sb sd sq
                a = u(r[rs] + sx16(imm))
                n = {0x2b: 4, 0x29: 2, 0x28: 1, 0x3f: 8, 0x1f: 16}[op]
                self.store(a, (r[rt] & ((1 << (8 * min(n, 4))) - 1)).to_bytes(min(n, 4), 'little') + bytes(max(0, n - 4)))
            elif op in (0x23, 0x21, 0x25, 0x20, 0x24, 0x37):  # lw lh lhu lb lbu ld
                a = u(r[rs] + sx16(imm))
                n = {0x23: 4, 0x21: 2, 0x25: 2, 0x20: 1, 0x24: 1, 0x37: 4}[op]
                v = int.from_bytes(self.load(a, n), 'little')
                if op in (0x21, 0x20) and v >> (8 * n - 1):
                    v = u(v - (1 << (8 * n)))
                r[rt] = v
            elif op == 0x39:                                # swc1
                self.store(u(r[rs] + sx16(imm)), self.f[rt].to_bytes(4, 'little'))
            elif op == 0x31:                                # lwc1
                self.f[rt] = int.from_bytes(self.load(u(r[rs] + sx16(imm)), 4), 'little')
            elif op == 0x11:                                # COP1
                if rs == 0x04:                              # mtc1
                    self.f[rd] = r[rt]
                elif rs == 0x00:                            # mfc1
                    r[rt] = self.f[rd]
                elif rs == 0x10 and fn == 0x06:             # mov.s
                    self.f[sa] = self.f[rd]
                elif rs == 0x10 and fn == 0x07:             # neg.s
                    self.f[sa] = self.f[rd] ^ 0x80000000
                elif rs == 0x10 and fn in (0, 1, 2, 3):     # add sub mul div
                    fa = struct.unpack('<f', struct.pack('<I', self.f[rd]))[0]
                    fb = struct.unpack('<f', struct.pack('<I', self.f[rt]))[0]
                    res = (fa + fb, fa - fb, fa * fb, fa / fb if fb else 0.0)[fn]
                    self.f[sa] = struct.unpack('<I', struct.pack('<f', res))[0]
                elif rs == 0x14 and fn == 0x20:             # cvt.s.w
                    v = self.f[rd]
                    self.f[sa] = struct.unpack('<I', struct.pack('<f', float(v - (1 << 32) if v >> 31 else v)))[0]
                else:
                    raise Stop(f'cop1 {w:#010x} at {pc:#x}')
            elif op == 0x03:                                # jal: skipped
                r[2] = 0
            elif op == 0x02:                                # j
                target = (pc & 0xf0000000) | ((w & 0x3ffffff) << 2)
            elif op in (0x04, 0x05, 0x14, 0x15):            # beq bne beql bnel
                take = (r[rs] == r[rt]) == (op in (0x04, 0x14))
                if take:
                    target = u(pc + 4 + (sx16(imm) << 2))
                elif op in (0x14, 0x15):
                    nxt = pc + 8                            # likely: the delay slot is skipped
            elif op in (0x06, 0x07, 0x16, 0x17):            # blez bgtz (l)
                v = r[rs] - (1 << 32) if r[rs] >> 31 else r[rs]
                take = v <= 0 if op in (0x06, 0x16) else v > 0
                if take:
                    target = u(pc + 4 + (sx16(imm) << 2))
                elif op in (0x16, 0x17):
                    nxt = pc + 8
            elif op == 0x01 and rt in (0, 1, 2, 3):         # bltz bgez (l)
                v = r[rs] - (1 << 32) if r[rs] >> 31 else r[rs]
                take = v < 0 if rt in (0, 2) else v >= 0
                if take:
                    target = u(pc + 4 + (sx16(imm) << 2))
                elif rt in (2, 3):
                    nxt = pc + 8
            elif op in (0x1e, 0x12, 0x1c, 0x2f):            # lq, COP2, MMI, cache: not in these initialisers
                raise Stop(f'op {op:#x} at {pc:#x}')
            else:
                raise Stop(f'op {op:#x} at {pc:#x}')
            r[0] = 0
            if delay is not None:
                if delay == 'ret':
                    return
                pc, delay = delay, None
                continue
            if target is not None:
                delay = target
            pc = nxt
        raise Stop('step limit')


def gp_of(exe):
    """$gp: the ELF's .reginfo (n2exe.Exe.gp)."""
    if not exe.gp:
        raise ValueError('no $gp (.reginfo) in the executable')
    return exe.gp


def function_start(exe, va):
    """The prologue (addiu sp, sp, -n) before va."""
    for a in range(va, va - 0x20000, -4):
        o = exe.raw(a)
        w, = struct.unpack_from('<I', exe.b, o)
        if w >> 16 == 0x27bd and w & 0x8000:
            return a
    raise ValueError(f'no prologue before {va:#x}')


def code_refs(exe, va):
    """The code addresses that build va (lui hi / addiu lo, the addiu's)."""
    lo, out = va & 0xffff, []
    hi = ((va + 0x8000) >> 16) & 0xffff
    text_lo, text_hi = exe.code
    for a in range(text_lo, text_hi, 4):
        w, = struct.unpack_from('<I', exe.b, exe.raw(a))
        if w >> 26 == 9 and w & 0xffff == lo:
            # the matching lui within a few instructions before
            for b in range(a - 4, a - 4 * 64, -4):
                w2, = struct.unpack_from('<I', exe.b, exe.raw(b))
                if w2 >> 26 == 0x0f and w2 & 0xffff == hi and (w2 >> 16) & 31 == (w >> 21) & 31:
                    out.append(a)
                    break
    return out


def initialise(exe, strings):
    """Run the functions whose code builds the address of any of strings
    and return their stores: {address: byte}."""
    gp, mem, done = gp_of(exe), {}, set()
    for s in strings:
        at = exe.b.find(s.encode('latin-1') + b'\0')
        while at >= 0 and exe.b[at - 1] != 0:
            at = exe.b.find(s.encode('latin-1') + b'\0', at + 1)
        if at < 0:
            continue
        for site in code_refs(exe, exe.va(at)):
            start = function_start(exe, site)
            if start in done:
                continue
            done.add(start)
            cpu = Cpu(exe, gp)
            try:
                cpu.run(start)
            except Stop:
                pass
            mem.update(cpu.mem)
    return mem
