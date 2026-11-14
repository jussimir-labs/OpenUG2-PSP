"""Original car-part hinges and raw keys for the trunk and Scissor doors.

GameCube GUGE69 GetMarkerPosition selects +0x310 in chunk 0x37100 for
the trunk. GetCarPartAnimMatrix evaluates <CAR>_ZAM_TRUNK_REG_q/t.
Keep the supported raw, single-axis keys; other encodings remain explicit
unsupported entries, rather than interpreting compressed bytes as floats.
"""
import json
import math
import struct
from n2formats import archived, chunks


def raw_trunk(data, symbols, model):
    bank = struct.unpack_from('<8I', data, symbols['__AnimationBank:::bank'])
    ptrs = struct.unpack_from(f'<{bank[1]}I', data, bank[4])
    names = struct.unpack_from(f'<{bank[1]}I', data, bank[5])
    tracks = {data[n:data.index(b'\0', n)].decode(): p for n, p in zip(names, ptrs)}
    qptr, tptr = (tracks[f'{model}_ZAM_TRUNK_REG_{suffix}'] for suffix in ('q', 't'))
    top = struct.unpack_from('<4I', data, qptr)
    frames = top[2] >> 16
    if top[0] & 0xffff != 15 or top[2] & 0xffff != 1 or not 2 <= frames <= 64:
        raise ValueError('unsupported trunk animation container')
    channel = top[3]
    header = struct.unpack_from('<8I', data, channel)
    if header != (0x14000000, 2, 4, frames, 1, 0, 1, 0):
        raise ValueError('unsupported trunk quaternion encoding')
    ttop = struct.unpack_from('<4I', data, tptr)
    th = struct.unpack_from('<6I', data, ttop[3])
    if ttop[0] & 0xffff != 15 or ttop[2] != top[2] or th != (0x14000000, 1, 0, frames, 0, 0):
        raise ValueError('unsupported moving trunk translation')
    keys = [struct.unpack_from('<4f', data, channel + 32 + 16 * i) for i in range(frames)]
    for x, y, z, w in keys:
        if not all(math.isfinite(v) for v in (x, y, z, w)) or abs(x) > 1e-6 or abs(z) > 1e-6 or abs(y*y+w*w-1) > 1e-4:
            raise ValueError('invalid or non-lateral-axis trunk quaternion')
    if keys[0] != (0, 0, 0, 1):
        raise ValueError('trunk rest orientation is not identity')
    return [(q[1], q[3]) for q in keys]


def trunk(game, model):
    # Reuse the existing EAGL ELF reader, not its character animation decoder.
    from convert_nis import elf_data
    blob = archived(game, f'CARS/{model}/PARTS_ANIMATIONS.BIN')
    marker = next(p for tag, p, _ in chunks(blob) if tag == 0x37100)
    marker = (marker + 15) & ~15
    matrix = struct.unpack_from('<16f', blob, marker + 0x310)
    if matrix[:12] != (1,0,0,0,0,1,0,0,0,0,1,0) or matrix[15] != 1:
        raise ValueError('unsupported trunk rest basis')
    pivot = (matrix[13], matrix[14], matrix[12])  # game Y,Z,X -> GU X,Y,Z
    if not all(math.isfinite(v) and abs(v) < 10 for v in pivot):
        raise ValueError('invalid trunk pivot')
    needle = f'{model}_ZAM_TRUNK_REG_q'.encode() + b'\0'
    bank = next(blob[p:p+n] for tag, p, n in chunks(blob) if tag == 0xe34010 and needle in blob[p:p+n])
    data, symbols = elf_data(bank)
    return pivot, raw_trunk(data, symbols, model)


def scissor(game, model):
    from convert_nis import elf_data
    blob = archived(game, f'CARS/{model}/PARTS_ANIMATIONS.BIN')
    marker = next(p for tag, p, _ in chunks(blob) if tag == 0x37100)
    marker = (marker + 15) & ~15
    sides = []
    for side, offset in (('LEFT', 0x1d0), ('RIGHT', 0x290)):
        prefix = f'{model}_ZAM_{side}_DOOR_SCISSOR_'
        needle = (prefix + 'q').encode() + b'\0'
        matrix = struct.unpack_from('<16f', blob, marker + offset)
        bank = next(blob[p:p+n] for tag, p, n in chunks(blob) if tag == 0xe34010 and needle in blob[p:p+n])
        data, symbols = elf_data(bank)
        header = struct.unpack_from('<8I', data, symbols['__AnimationBank:::bank'])
        ptrs = struct.unpack_from(f'<{header[1]}I', data, header[4])
        names = struct.unpack_from(f'<{header[1]}I', data, header[5])
        tracks = {data[n:data.index(b'\0', n)].decode(): p for n, p in zip(names, ptrs)}
        q = struct.unpack_from('<4I', data, tracks[prefix + 'q'])
        t = struct.unpack_from('<4I', data, tracks[prefix + 't'])
        frames = q[2] >> 16
        if q[0] != 0x1400000f or t[0] != q[0] or (q[2] & 0xffff) != 1 or t[2] != q[2] or not 2 <= frames <= 64:
            raise ValueError('unsupported scissor animation container')
        if struct.unpack_from('<8I', data, q[3]) != (0x14000000, 2, 4, frames, 1, 0, 1, 0):
            raise ValueError('unsupported scissor quaternion encoding')
        if struct.unpack_from('<8I', data, t[3]) != (0x14000000, 2, 3, frames, 1, 2, 1, 2):
            raise ValueError('unsupported scissor translation encoding')
        if matrix[:12] != (1,0,0,0,0,1,0,0,0,0,1,0) or matrix[15] != 1:
            raise ValueError('unsupported scissor rest basis')
        pivot = (matrix[13], matrix[14], matrix[12])  # game Y,Z,X -> GU X,Y,Z
        if not all(math.isfinite(v) and abs(v) < 10 for v in pivot):
            raise ValueError('invalid scissor pivot')
        samples = []
        for i in range(frames):
            x, y, z, w = struct.unpack_from('<4f', data, q[3] + 32 + 16*i)
            tx, ty, tz = struct.unpack_from('<3f', data, t[3] + 32 + 12*i)
            if not all(math.isfinite(v) for v in (x,y,z,w,tx,ty,tz)) or abs(x)>1e-6 or abs(z)>1e-6 or abs(y*y+w*w-1)>1e-4:
                raise ValueError('unsupported scissor rotation axis')
            if abs(tx-pivot[2])>1e-5 or abs(tz-pivot[1])>1e-5 or abs(ty-pivot[0])>1:
                raise ValueError('unsupported scissor translation axis')
            samples.append((y,w,ty-pivot[0]))
        if samples[0][:2] != (0,1) or abs(samples[0][2])>1e-5:
            raise ValueError('scissor rest pose disagrees with marker')
        sides.append(struct.pack('<3fI', *pivot, frames) + b''.join(struct.pack('<3f', *key) for key in samples))
    return struct.pack('<16s', model.encode()) + b''.join(sides)


def write(game, out, models):
    """Missing ZDIR entries/unsupported encodings are per-model fallbacks.

    Missing archives, permissions and other I/O failures abort conversion:
    they indicate incomplete/unreadable source data, not an unsupported car.
    Do not catch OSError and silently publish an incomplete animation table.
    """
    scissor_records = []
    records, unsupported = [], {}
    for model in models:
        try:
            scissor_records.append(scissor(game, model))
        except (ValueError, KeyError, StopIteration, struct.error):
            pass  # absent or unsupported source uses the existing door pose
        try:
            pivot, keys = trunk(game, model)
        except (ValueError, KeyError, StopIteration, struct.error) as exc:
            unsupported[model] = str(exc) or type(exc).__name__
            continue
        records.append(struct.pack('<16s3fI', model.encode(), *pivot, len(keys)) +
                       b''.join(struct.pack('<2f', *q) for q in keys))
    # N2TA v1: header 12B, each name[16], GU pivot[3], u32 key count,
    # then count x (q_y, q_w). The game caches one car, without allocations.
    (out / 'trunk-anim.n2ta').write_bytes(struct.pack('<4s2I', b'N2TA', 1, len(records)) + b''.join(records))
    (out / 'trunk-anim.json').write_text(json.dumps({'supported':len(records), 'unsupported':unsupported}, indent=2)+'\n')
    # N2DA v1: name, then pivot/count and (q_y, q_w, lateral delta) keys for each side.
    (out / 'scissor-anim.n2da').write_bytes(struct.pack('<4s2I', b'N2DA', 1, len(scissor_records)) + b''.join(scissor_records))
    return len(records)
