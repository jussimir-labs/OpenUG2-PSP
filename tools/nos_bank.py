#!/usr/bin/env python3
"""Strict native NOS/PURGE banks. N2NS v1 is LE PCM16, not a C ABI dump.

Only the four qualified US class graphs are accepted. No pitch/gain/loop
fitting, resampling, seam processing, game execution or music conversion.
"""
import argparse
import binascii
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

MAX_BYTES = 1 << 20
HEADER = struct.Struct('<4s7I')
DESC = struct.Struct('<8I')
DATA_OFFSET = 224
LOOP = 1
# Normalized call/store programs (relative state offsets), then exact immutable
# state templates. Retail Xbox and PS2 share these templates; accepting relocated
# or edited templates without qualification is deliberately unsupported.
SHAPES = {
    'nos': '1e24abdbfdefd79add21f90243325ecae49b95655978a399d9ee4839a50caf1a',
    'purge0': '14d727ac0978bbc95201ca1b3695fb5a98efdb601cc3a2074c8426ffae016ca9',
    'purge1': '9f3852e74215be7eea480f72c44fc2023175559c32680030f3576e5d9e01e2be',
}
PARAMETERS = {
    0: '24b4ae9f090476da9dc201eb6b5d84c19fe7607d30eac5f2494f69a63785cacb',
    1: '50484be91160ed0837800257a492e5149721a19ba64899a6a618f50e1300848f',
}
CLASS_HEADERS = {
    (0, 'FX_NITROUS'): '4628c21c341462d6bac43ea35dab906f8b13c78b6f07a57b1f2dc765a7ac11d6',
    (0, 'FX_PURGE'): '1f1a47d81fff4e8bfe1cb325d40b0b2b59a66fb66913832e55a901596d5c5535',
    (1, 'FX_NITROUS'): 'b68fd0f85fadbdbfe48744c3ea8837bd1757df06024ee8f00c13bdcd84a0a76d',
    (1, 'FX_PURGE'): '9007b41e20538290c0227022558bf342359e44a740d9e7dac4e39bfc40e873a7',
}
TEMPLATES = {
    (0, 'FX_NITROUS'): ('nos', 808, 792, [172, 312, 468, 728], 4,
        '7902f5346403be6a520ea3e571863bff7b372292a36a61e9d9f233d64d0cc85d'),
    (0, 'FX_PURGE'): ('purge0', 812, 796, [172, 540, 604, 668], 4,
        'e893d0765760aa115e6a649e993f93bb681684b401cbb926b13bea2d423b30c8'),
    (1, 'FX_NITROUS'): ('nos', 808, 792, [172, 312, 468, 728], 4,
        '97a3bc4d2e96bcc1a2cc626eb2e7531789c287a18fa563be115463e7af1af16f'),
    (1, 'FX_PURGE'): ('purge1', 812, 796, [412, 484, 564, 668], 2,
        'c4debd92178b3ecce4d12d37a3a99b45330f3202f93f988c82dc6440e3868b4d'),
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(ok, reason):
    if not ok:
        raise ValueError('NOS bank: ' + reason)

def span(b, at, n):
    require(isinstance(at, int) and at >= 0 and n >= 0 and at <= len(b) - n, 'out-of-bounds data')
    return b[at:at + n]

def u32(b, at):
    return struct.unpack('<I', span(b, at, 4))[0]

def u16(b, at):
    return struct.unpack('<H', span(b, at, 2))[0]

def normalized_hash(operations):
    return sha(json.dumps(operations, separators=(',', ':')).encode('ascii'))

def xbox_program(b, code, limit, size):
    """Bounded source-bytecode parser, translated into relative reads/writes."""
    at, state, out = code, 24, []
    for _ in range(256):
        require(at + 4 <= limit, 'unterminated Xbox program')
        word = u32(b, at); at += 4
        if word == 255:
            require(state == size, 'Xbox class-state size')
            return out
        fn, count = word & 255, word >> 8
        require(fn < 40 and count < 20 and at + count * 8 + 4 <= limit, 'Xbox opcode')
        pairs = [struct.unpack('<2I', span(b, at + k * 8, 8)) for k in range(count)]
        at += count * 8
        advance = u32(b, at); at += 4
        require(advance > 0 and state + advance <= size, 'Xbox state advance')
        if fn == 3:
            ret = ('mem', state); out.append(('store', state, ('const', 0)))
        elif fn == 36:
            ret = ('add', ('mem', state), ('mem', state + 4))
        else:
            out.append(('call', fn, state)); ret = ('return', fn, state)
        for source, dest in pairs:
            require(state + dest < size and (source == 0xffffffff or state + source < size), 'Xbox state reference')
            out.append(('store', state + dest, ret if source == 0xffffffff else ('mem', state + source)))
        state += advance
    raise ValueError('NOS bank: Xbox program exceeds operation bound')

def ps2_program(b, code, limit):
    """The qualified PS2 JAL-index sequence, including relevant delay slots.

    This is a restricted original-bank parser, not a general MIPS emulator.
    Unknown instructions/expressions are refused, never approximated.
    """
    regs, out = {0: ('const', 0), 4: ('ptr', 24)}, []
    for at in range(code, min(limit, code + 4096), 4):
        w = u32(b, at); op = w >> 26; rs = w >> 21 & 31; rt = w >> 16 & 31; rd = w >> 11 & 31; fn = w & 63
        imm = (w & 65535) - (65536 if w & 32768 else 0)
        try:
            if op == 3:
                require(u32(b, at + 4) == 0x0200202d, 'PS2 call delay slot')
                require(regs[16][0] == 'ptr', 'PS2 state pointer')
                idx, state = w & 0x3ffffff, regs[16][1]
                require(idx < 40, 'PS2 call index')
                out.append(('call', idx, state)); regs[2] = ('return', idx, state)
            elif op == 9 and rt == 16:
                require(rs == 16 and regs[16][0] == 'ptr', 'PS2 state increment')
                regs[16] = ('ptr', regs[16][1] + imm)
            elif op == 35 and rs == 16:
                regs[rt] = ('mem', regs[16][1] + imm)
            elif op == 43 and rs == 16:
                out.append(('store', regs[16][1] + imm, regs[rt]))
            elif op == 0 and fn in (33, 45):
                regs[rd] = regs[rs] if rt == 0 else ('add', regs[rs], regs[rt])
            elif op == 0 and fn == 8:
                require(rs == 31 and u32(b, at + 4) == 0x27bd0020, 'PS2 return/delay slot')
                return out
            elif op in (9, 30, 31):
                require(w in (0x27bdffe0, 0x7fb00000, 0x7fbf0010, 0x7bbf0010, 0x7bb00000),
                        'PS2 unsupported stack-frame instruction')
            else:
                raise ValueError('NOS bank: unsupported PS2 instruction')
        except KeyError as e:
            raise ValueError('NOS bank: unsupported PS2 register use') from e
    raise ValueError('NOS bank: unterminated PS2 program')

def validate_graph(b, bank_id):
    require(bank_id in (0, 1) and 92 <= len(b) <= MAX_BYTES, 'ABKC size/bank ID')
    require(b[:4] == b'ABKC' and b[4] in (1, 2), 'unsupported ABKC platform/version')
    require(u16(b, 10) == 2, 'expected two effect classes')
    platform = 'xbox' if b[4] == 2 else 'ps2'
    fixups = u32(b, 0x38); require(u32(b, fixups) == 2, 'class-name fixups')
    names = {}
    for i in range(2):
        handle, ident, kind = struct.unpack('<3I', span(b, fixups + 4 + i * 12, 12))
        require(kind & 255 == 1 and handle >= 4, 'class handle kind')
        span(b, ident, 4); end = b.find(b'\0', ident + 4, min(len(b), ident + 68))
        require(end >= 0, 'unterminated class name')
        try: name = b[ident + 4:end].decode('ascii')
        except UnicodeError as e: raise ValueError('NOS bank: class name encoding') from e
        require(handle - 4 not in names and name not in names.values(), 'duplicate class name')
        names[handle - 4] = name
    at = u32(b, 0x1c); modules = []
    for i in range(2):
        span(b, at, 60); require(at in names, 'unresolved class')
        name = names[at]; require((bank_id, name) in TEMPLATES, 'unsupported effect class')
        shape, size, destructor, refs, instances, template_hash = TEMPLATES[bank_id, name]
        code, data, actual_size, actual_destructor = struct.unpack('<4I', span(b, at + 40, 16))
        count = b[at + 36]; require(count == 4 and b[at + 39] == 0, 'player/reference count')
        players = [u32(b, at + 60 + j * 4) for j in range(count)]
        require((actual_size, actual_destructor, players) == (size, destructor, refs), 'class layout')
        require(u16(b, at + 30) == instances, 'class max_instances')
        header = bytearray(span(b, at, 76))
        struct.pack_into('<2I', header, 40, 0, 0)  # code/data file locations, checked separately
        require(sha(header) == CLASS_HEADERS[bank_id, name], 'unsupported class-header parameters')
        require(sha(span(b, data, size)) == template_hash, 'unsupported class parameters/template')
        modules.append(dict(name=name, header=at, code=code, data=data, size=size, players=players,
                            max_instances=instances, class_header_sha256=CLASS_HEADERS[bank_id, name],
                            template_sha256=template_hash, shape_key=shape))
        at += 60 + 4 * count
    require({x['name'] for x in modules} == {'FX_NITROUS', 'FX_PURGE'}, 'class set')
    # Include external sample-list/random/envelope tables, not only class state.
    require(min(x['data'] for x in modules) == 1332 and u32(b, 24) == 3456,
            'unsupported relocated parameter region')
    require(sha(span(b, 1332, 3456 - 1332)) == PARAMETERS[bank_id],
            'unsupported shared parameters/sample bindings')
    for i, m in enumerate(modules):
        limit = modules[i + 1]['code'] if i + 1 < len(modules) else min(x['data'] for x in modules)
        require(m['code'] % 4 == 0 and m['code'] < limit <= len(b), 'code range')
        ops = xbox_program(b, m['code'], limit, m['size']) if platform == 'xbox' else ps2_program(b, m['code'], limit)
        digest = normalized_hash(ops)
        require(digest == SHAPES[m['shape_key']], 'unsupported normalized graph shape')
        m['shape_sha256'], m['normalized_operations'] = digest, len(ops)
        m['parameter_region_sha256'] = PARAMETERS[bank_id]
    return platform, modules

def read_pt(b, at):
    """Bounds/duplicate-tag checks before invoking the shared PT decoder."""
    from convert_audio import pt_header
    require(span(b, at, 2) == b'PT', 'PT header')
    span(b, at, 4); p = at + 4; seen = set()
    for _ in range(128):
        tag = span(b, p, 1)[0]; p += 1
        if tag == 255:
            return pt_header(b[:p], at)
        if tag in (252, 253, 254): continue
        require(tag not in seen, 'duplicate PT tag'); seen.add(tag)
        n = span(b, p, 1)[0]; p += 1
        require(0 <= n <= 4, 'PT field size'); span(b, p, n); p += n
    raise ValueError('NOS bank: unterminated PT header')

def source_samples(b, platform):
    from convert_audio import stream_pcm
    bnk = u32(b, 24); require(span(b, bnk, 4) == b'BNKl' and u16(b, bnk + 6) == 7, 'BNKl seven slots')
    require(u32(b, bnk + 20) == 0, 'slot0 must be empty')
    out, proof, regions = [], [], []
    for slot in range(1, 7):
        loc = bnk + 20 + slot * 4; rel = u32(b, loc); require(rel > 0, 'missing native slot')
        f = read_pt(b, loc + rel); ns, rate = f.get(0x85, 0), f.get(0x84, 0)
        require(f.get('platform') == (7 if platform == 'xbox' else 5), 'PT platform mismatch')
        require(f.get(0x82, 1) == 1 and 4000 <= rate <= 192000 and 0 < ns <= (MAX_BYTES - DATA_OFFSET) // 2, 'native sample shape')
        require(f.get(0xa0, 0) == (20 if platform == 'xbox' else 0), 'native sample codec')
        require(0x88 in f and f[0x88] > 0, 'missing native sample offset')
        start = bnk + f[0x88]
        stored = ((ns + 63) // 64 * 36) if platform == 'xbox' else ((ns + 27) // 28 * 16)
        span(b, start, stored); regions.append((start, start + stored))
        loop = slot in (1, 4)
        if loop:
            require(0x86 in f and 0x87 in f, 'native hold loop bounds missing')
            lo, hi = f[0x86], f[0x87] + 1
            require(0 <= lo < hi <= ns, 'native loop bounds')
        else:
            require(0x86 not in f and 0x87 not in f, 'unexpected native one-shot loop')
            lo = hi = 0
        pcm = stream_pcm(b, bnk, f, 'NOS slot%d' % slot)
        require(len(pcm) == ns, 'decoder did not preserve all native samples')
        raw = struct.pack('<%dh' % ns, *pcm)
        out.append(dict(slot=slot, rate=rate, samples=ns, loop_start=lo, loop_end=hi, flags=int(loop), pcm=raw))
        proof.append(dict(slot=slot, rate=rate, samples=ns, loop_start=lo, loop_end_exclusive=hi,
                          flags=int(loop), native_pt={str(k): v for k, v in f.items()}, pcm_sha256=sha(raw)))
    regions.sort(); require(all(a[1] <= b[0] for a, b in zip(regions, regions[1:])), 'native sample data overlap')
    return out, proof

def build_bank(bank_id, samples):
    require(bank_id in (0, 1) and len(samples) == 6, 'six samples/bank ID')
    blob = bytearray(DATA_OFFSET); seen = set()
    for i, x in enumerate(samples):
        slot, rate, ns, lo, hi, flags, pcm = (x[k] for k in ('slot', 'rate', 'samples', 'loop_start', 'loop_end', 'flags', 'pcm'))
        require(slot in range(1, 7) and slot not in seen, 'sample slots'); seen.add(slot)
        require(isinstance(pcm, bytes) and len(pcm) == ns * 2, 'PCM length')
        at = (len(blob) + 15) & ~15
        require(at + len(pcm) <= MAX_BYTES, 'bank exceeds1MiB')
        blob += b'\0' * (at - len(blob)); blob += pcm
        DESC.pack_into(blob, HEADER.size + i * DESC.size, slot, rate, ns, lo, hi, flags, at, len(pcm))
    HEADER.pack_into(blob, 0, b'N2NS', 1, bank_id, 6, 32, DATA_OFFSET, len(blob), binascii.crc32(blob[32:]) & 0xffffffff)
    parse_bank(blob)
    return bytes(blob)

def parse_bank(data):
    require(DATA_OFFSET <= len(data) <= MAX_BYTES, 'N2NS bounded size')
    b = bytes(data)
    magic, ver, bank, count, table, payload, total, crc = HEADER.unpack_from(b)
    require((magic, ver, count, table, payload) == (b'N2NS', 1, 6, 32, DATA_OFFSET) and bank in (0, 1), 'N2NS header')
    require(total == len(b) and crc == binascii.crc32(b[32:]) & 0xffffffff, 'N2NS length/CRC')
    seen, rows, regions = set(), [], []
    for i in range(6):
        slot, rate, ns, lo, hi, flags, at, size = DESC.unpack_from(b, table + i * DESC.size)
        require(slot in range(1, 7) and slot not in seen, 'N2NS unique slots'); seen.add(slot)
        require(4000 <= rate <= 192000 and ns > 0 and size == ns * 2 and flags in (0, LOOP), 'N2NS sample descriptor')
        require((0 <= lo < hi <= ns) if flags else (lo == hi == 0), 'N2NS loop range')
        require(at >= payload and at % 16 == 0, 'N2NS aligned payload offset'); raw = span(b, at, size)
        regions.append((at, at + size)); rows.append(dict(slot=slot, rate=rate, samples=ns, loop_start=lo, loop_end=hi, flags=flags, data_offset=at, stored_bytes=size, pcm=raw))
    regions.sort(); last = payload
    for a, z in regions:
        require(a >= last and not any(b[last:a]), 'N2NS overlap/nonzero padding'); last = z
    require(last == len(b), 'N2NS unexpected trailing payload')
    return bank, rows

def strip_legacy_nos(existing_sfx, out=None):
    """Repack N2SF removing only nitrous/nitrous_2; other samples exact.

    Returns bytes, optionally writes them to out. No decode or processing.
    """
    b = bytes(existing_sfx) if isinstance(existing_sfx, (bytes, bytearray, memoryview)) else Path(existing_sfx).read_bytes()
    require(len(b) >= 8 and b[:4] == b'N2SF', 'N2SF header')
    n = u32(b, 4); require(n <= 4096 and 8 + n * 32 <= len(b), 'N2SF table')
    payload = 8 + n * 32; kept, blob, names, regions = [], bytearray(), set(), []
    for i in range(n):
        name, rate, ns, flags, off = struct.unpack('<16s4I', span(b, 8 + i * 32, 32))
        require(name not in names, 'duplicate N2SF name'); names.add(name)
        require(rate > 0 and flags in (0, 1), 'N2SF native PCM descriptor')
        pcm = span(b, payload + off, ns * 2); regions.append((off, off + ns * 2))
        if name.rstrip(b'\0') in (b'nitrous', b'nitrous_2'): continue
        kept.append((name, rate, ns, flags, len(blob))); blob += pcm
    regions.sort(); require(all(a[1] <= b[0] for a, b in zip(regions, regions[1:])), 'N2SF overlapping PCM')
    result = struct.pack('<4sI', b'N2SF', len(kept)) + b''.join(struct.pack('<16s4I', *e) for e in kept) + blob
    if out is not None: Path(out).write_bytes(result)
    return bytes(result)

def export(game, out):
    from n2formats import archived
    products, receipt = {}, {'schema': 1, 'format': 'N2NSv1', 'banks': {}}
    platforms = set()
    for bank in (0, 1):
        path = 'SOUND/NOS/Nitrous_%02d_MB.abk' % bank; raw = archived(game, path)
        platform, modules = validate_graph(raw, bank); platforms.add(platform)
        samples, proof = source_samples(raw, platform); payload = build_bank(bank, samples)
        products['nos-%d.n2ns' % bank] = payload
        receipt['banks'][str(bank)] = dict(source_path=path, source_sha256=sha(raw), platform=platform,
                                           classes=modules, samples=proof, output_sha256=sha(payload), output_bytes=len(payload))
    require(len(platforms) == 1, 'mixed original source platforms')
    receipt['source'] = next(iter(platforms))
    products['nos-banks.json'] = (json.dumps(receipt, indent=2) + '\n').encode()
    out = Path(out); out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nos-stage-', dir=out) as staging:
        for n, b in products.items(): (Path(staging) / n).write_bytes(b)
        for n in products: os.replace(Path(staging) / n, out / n)
    return receipt

def main():
    import n2source
    ap = argparse.ArgumentParser(description=__doc__); n2source.game_args(ap)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args(); print(json.dumps(export(args.game, args.out), indent=2))

if __name__ == '__main__':
    main()
