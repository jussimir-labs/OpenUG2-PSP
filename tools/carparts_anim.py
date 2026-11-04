"""Original car-part hinge and raw quaternion keys -> trunk-anim.n2ta.

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


def write(game, out, models):
    """Missing ZDIR entries/unsupported encodings are per-model fallbacks.

    Missing archives, permissions and other I/O failures abort conversion:
    they indicate incomplete/unreadable source data, not an unsupported car.
    Do not catch OSError and silently publish an incomplete animation table.
    """
    records, unsupported = [], {}
    for model in models:
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
    return len(records)
