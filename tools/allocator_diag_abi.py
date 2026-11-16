#!/usr/bin/env python3
"""Fail closed on the private newlib ABI used by the diagnostic-only helper.

ELF32 little-MIPS with retained relocations only. Code fingerprints mask the
relocated immediate but retain instruction bits, relocation type, symbol and
offset. A changed libc/layout requires a new review, not an automatic rebase.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

FUNCTIONS = ('_malloc_r', '_mallinfo_r', '__malloc_update_mallinfo',
             '__malloc_lock', '__malloc_unlock', '_sbrk', '_sbrk_r',
             '__retarget_lock_acquire_recursive', '__retarget_lock_release_recursive', '__locks_init')
OFFSETS = ('heap_top.0', 'heap_ptr.1', 'heap_bottom.2')


class Elf:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if len(self.data) < 52 or self.data[:7] != b'\x7fELF\x01\x01\x01':
            raise ValueError('requires ELF32 little-endian version1')
        h = struct.unpack_from('<16sHHIIIIIHHHHHH', self.data)
        if h[2] != 8 or h[11] != 40 or not h[12]:
            raise ValueError('requires MIPS and nonempty standard section table')
        self.sections = [struct.unpack_from('<IIIIIIIIII', self.data, h[6]+40*i)
                         for i in range(h[12])]
        self.symbols = {}
        self.symtabs = {}
        for i, s in enumerate(self.sections):
            if s[1] != 2:
                continue
            if s[9] != 16:
                raise ValueError('unsupported symbol size')
            strings = self.section_bytes(self.sections[s[6]])
            entries = []
            for off in range(s[4], s[4]+s[5], 16):
                name, value, size, info, other, sec = struct.unpack_from('<IIIBBH', self.data, off)
                end = strings.find(b'\0', name)
                if end < 0:
                    raise ValueError('unterminated symbol')
                label = strings[name:end].decode('utf-8')
                entries.append((label, value, size, sec))
                if label in (*FUNCTIONS, *OFFSETS, '__malloc_av_', '__psp_heap_blockid', '__malloc_top_pad', 'allocator_diag_abi_offsets') and sec not in (0, 0xfff1):
                    if label in self.symbols and self.symbols[label] != entries[-1]:
                        raise ValueError('ambiguous symbol '+label)
                    self.symbols[label] = entries[-1]
            self.symtabs[i] = entries

    def section_bytes(self, s):
        if s[4]+s[5] > len(self.data):
            raise ValueError('truncated section')
        return self.data[s[4]:s[4]+s[5]]

    def symbol_bytes(self, name):
        _, value, size, sec = self.symbols[name]
        s = self.sections[sec]
        offset = value-s[3]
        if offset < 0 or offset+size > s[5]:
            raise ValueError('symbol outside section '+name)
        return self.section_bytes(s)[offset:offset+size]

    def fingerprint(self, name):
        _, value, size, sec = self.symbols[name]
        code = bytearray(self.symbol_bytes(name))
        rels = []
        for r in self.sections:
            if r[1] != 9 or r[7] != sec:
                continue
            if r[9] != 8:
                raise ValueError('unsupported relocation entry')
            symbols = self.symtabs[r[6]]
            target = self.sections[sec]
            entries = [struct.unpack_from('<II', self.data, pos)
                       for pos in range(r[4], r[4]+r[5], 8)]
            for index, (offset, info) in enumerate(entries):
                # This PSP link retains section-relative REL offsets.
                address = target[3]+offset
                if not value <= address < value+size:
                    continue
                kind, sym = info & 255, info >> 8
                within = address-value
                if within & 3 or within+4 > len(code):
                    raise ValueError('bad relocation address')
                mask = {2: 0xffffffff, 4: 0x03ffffff, 5: 0xffff, 6: 0xffff}.get(kind)
                if mask is None:
                    raise ValueError('unsupported allocator relocation '+str(kind))
                word = struct.unpack_from('<I', code, within)[0]
                symbol_value = symbols[sym][1]
                if kind == 2:
                    addend = (word-symbol_value) & 0xffffffff
                elif kind == 4:
                    linked = ((address+4) & 0xf0000000) | ((word & 0x03ffffff) << 2)
                    addend = (linked-symbol_value) & 0xffffffff
                elif kind == 6:
                    # Standalone/reused LO relocations still preserve their
                    # offset modulo64KiB, rather than erasing field selection.
                    addend = ((word & 0xffff)-(symbol_value & 0xffff)) & 0xffff
                else:
                    # A HI relocation is paired with the next LO for this
                    # symbol in retained relocation order, as the linker does.
                    low = next((o for o, inf in entries[index+1:]
                                if inf & 255 == 6 and inf >> 8 == sym), None)
                    if low is None or low+4 > target[5]:
                        raise ValueError('unpaired HI16 allocator relocation')
                    low_word = struct.unpack_from('<I', self.data, target[4]+low)[0]
                    signed_low = struct.unpack('<h', struct.pack('<H', low_word & 0xffff))[0]
                    addend = (((word & 0xffff) << 16)+signed_low-symbol_value) & 0xffffffff
                struct.pack_into('<I', code, within, word & ~mask)
                rels.append((within, kind, symbols[sym][0], addend))
        if not rels:
            raise ValueError('missing retained relocations for '+name)
        payload = bytes(code)+json.dumps(sorted(rels), separators=(',', ':')).encode()
        return {'size': size, 'sha256_normalized': hashlib.sha256(payload).hexdigest()}

    def abi(self):
        av = self.symbols['__malloc_av_']
        if av[2] != 1032:
            raise ValueError('__malloc_av_ must be258 32-bit words')
        for name in ('__psp_heap_blockid', '__malloc_top_pad', *OFFSETS):
            if self.symbols[name][2] != 4:
                raise ValueError('expected 32-bit field '+name)
        return {'schema': 1, 'av_bytes': av[2],
                'private_offsets': [self.symbols[n][1]-av[1] for n in OFFSETS],
                'functions': {n: self.fingerprint(n) for n in FUNCTIONS}}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf', required=True)
    p.add_argument('--reference', required=True)
    p.add_argument('--make-reference', action='store_true')
    p.add_argument('--baseline-only', action='store_true',
                   help='audit original395 without helper; NEVER a packaging gate')
    p.add_argument('--output')
    args = p.parse_args()
    try:
        elf = Elf(args.elf)
        abi = elf.abi()
        if args.make_reference:
            Path(args.reference).write_text(json.dumps(abi, indent=2)+'\n')
            print('reference written; original ELF reviewed separately')
            return
        if abi != json.loads(Path(args.reference).read_text()):
            raise ValueError('allocator ABI/code changed; review required')
        if not args.baseline_only:
            if elf.symbols['allocator_diag_abi_offsets'][2] != 12:
                raise ValueError('wrong diagnostic offsets array')
            compiled = list(struct.unpack('<III', elf.symbol_bytes('allocator_diag_abi_offsets')))
            if compiled != abi['private_offsets']:
                raise ValueError('compiled offsets differ from this ELF')
        result = {'verified': True, 'baseline_only': args.baseline_only,
                  'elf_sha256': hashlib.sha256(elf.data).hexdigest(),
                  'private_offsets': abi['private_offsets'], 'functions_checked': list(FUNCTIONS)}
        if args.output:
            Path(args.output).write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(result))
    except (ValueError, KeyError, IndexError, struct.error, OSError) as exc:
        p.exit(1, 'allocator-diag ABI REFUSED: '+str(exc)+'\n')


if __name__ == '__main__':
    main()
