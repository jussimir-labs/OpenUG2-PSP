#!/usr/bin/env python3
"""Generate local build headers from the US PS2 disc (SLUS-21065)."""

import argparse
from contextlib import redirect_stdout
import hashlib
from io import StringIO
from pathlib import Path
import struct
import tempfile

import convert_ending_nis
import convert_nis
import n2formats
import partsdb

SOURCE = Path(__file__).resolve().parents[1] / 'psp-viewer'
EXECUTABLE_HASH = 'a666f3e8869e63ad2f5eff90fc30c4953fcce41166ff006aaf1ed327d180d8ff'
BANNER = '/* Generated locally from your game disc. Keep this file out of Git. */\n'

# File offsets in the above executable. All tables are little-endian.
STEERING = (
    ('LOCK', 0x3a3f08, (10,)), ('RATE', 0x3a3f48, (10,)),
    ('HOLD_LOCK', 0x3a3f30, (6,)), ('HOLD_RATE', 0x3a3fe0, (6,)),
    ('FLICK_RATE', 0x3a3fc8, (6,)), ('COUNTER', 0x3a3f98, (5,)),
    ('COUNTER_DRIFT', 0x3a3fb0, (5,)), ('CURVE', 0x3a3ff8, (21,)),
    ('CURVE_DRIFT', 0x3a4100, (21,)), ('DRIFT_CONTROL', 0x3a4158, (6, 2)),
)


def floats(data, offset, count):
    return struct.unpack_from(f'<{count}f', data, offset)


def c_value(value):
    if isinstance(value, float):
        return value.hex() + 'f'
    if isinstance(value, int):
        return str(value)
    return '{' + ', '.join(map(c_value, value)) + '}'


def declaration(ctype, name, shape, values):
    dimensions = ''.join(f'[{n}]' for n in shape)
    if len(shape) > 1:
        body = '{\n    ' + ',\n    '.join(map(c_value, values)) + '\n}'
    else:
        body = c_value(values)
    return f'static const {ctype} {name}{dimensions} = {body};\n'


def chunks_of(values, size):
    return [values[i:i + size] for i in range(0, len(values), size)]


def tuning(data):
    rows = [floats(data, 0x428cf0 + 0x144 * i, 81) for i in range(3)]
    # Two 11-field endpoints per drivetrain: stock and fully upgraded.
    drift = [list(zip(row[15:26], row[26:37])) for row in rows]
    misc = [
        ('float', 'og_drift_gains', (9,), [v for row in rows for v in row[10:13]]),
        ('float', 'og_rear_blends', (3,), [row[13] for row in rows]),
        ('float', 'og_rear_scales', (3,), [row[14] for row in rows]),
        ('float', 'og_neutral_scales', (3,), [row[4] for row in rows]),
        ('float', 'og_burnout_scales', (3, 4), [row[:4] for row in rows]),
        ('float', 'og_burnout_points', (6, 2), chunks_of(floats(data, 0x428ac8, 12), 2)),
    ]
    for i, suffix in enumerate((
        'left_right', 'all_wheel', 'cancel_slip', 'yaw_cancel', 'allow_time',
        'max_load', 'max_speed_mph', 'fishtail_time',
    )):
        misc.append(('float', f'og_burnout_{suffix}', (), floats(data, 0x473e18 + i * 4, 1)[0]))
    misc.append(('unsigned', 'og_burnout_fishtails', (), struct.unpack_from('<I', data, 0x473e38)[0]))
    return {
        'og_drift_values.h': declaration('float', 'drift_mode', (3, 11, 2), drift),
        'original_misc.h': ''.join(declaration(*row) for row in misc),
        'original_tables.h': ''.join((
            declaration('unsigned char', 'atan_lut', (257,), data[0x3b0398:0x3b0499]),
            declaration('float', 'curves', (42,), floats(data, 0x4291c0, 42)),
            declaration('float', 'sine_coeff', (10,), floats(data, 0x46d124, 10)),
        )),
    }


def hydraulics(data):
    levels = []
    for i in range(3):
        start = 0x3ee880 + i * 0x114
        fields = list(struct.unpack_from('<8fI', data, start))
        waves = [struct.unpack_from('<I4f', data, start + 0x24 + j * 20) for j in range(12)]
        levels.append(fields + [chunks_of(waves, 3)])
    return declaration('FeHydroLevel', 'FE_HYDRO_LEVELS', (3,), levels)


def camera_keys(bundle):
    tag = convert_nis.h33('CareerNis1')
    for kind, at, size in n2formats.chunks(bundle):
        if kind != 0x8003b201:
            continue
        limit = at + size
        while at + 16 <= limit:
            _, length, name = struct.unpack_from('<3I', bundle, at)
            end = at + 8 + length
            if length < 8 or end > limit:
                raise ValueError('Invalid career camera chunk')
            if name == tag:
                start = at + 16
                first = start + 0x20
                if first > end:
                    raise ValueError('Truncated career camera header')
                count = bundle[start + 0x10]
                if count != 6 or first + count * 0x88 > end:
                    raise ValueError('Unexpected career camera layout')
                return convert_ending_nis.camera_track(
                    [bundle[i:i + 0x88] for i in range(first, first + count * 0x88, 0x88)]
                )
            at = end
    return None


def camera(game):
    for name in ('GLOBAL/INGAMEFREEROAM.BUN', 'GLOBAL/INGAMERACE.BUN'):
        keys = camera_keys(n2formats.archived(game, name))
        if keys:
            break
    else:
        raise ValueError('CareerNis1 camera data is missing')

    scene = n2formats.archived(game, 'NIS/Scene_CareerNis1_BundleB.bun')
    span, _, _ = convert_ending_nis.scene_times(scene)
    lines = [f'#define CAREER_NIS_SPAN {span:.2f}f',
             'static const NisCamKey career_nis_keys[] = {']
    for t, eye_cubic, look_cubic, (eye_space, look_space), slopes, points, fov in keys:
        values = [','.join(f'{x:.4f}f'.replace('-0.0000f', '0.0000f') for x in row)
                  for row in (slopes, points, fov)]
        lines.append(f'    {{{t:.4f}f,{eye_cubic},{look_cubic},{eye_space},{look_space},'
                     f'{{{values[0]}}},{{{values[1]}}},{{{values[2]}}}}},')
    return '\n'.join(lines + ['};', ''])


def garage(game):
    parts = partsdb.Parts(game)
    by_key = {part['key']: part for part in parts.of_kind(48)}
    paint = [parts.rgb(part) for family in ('GLOSS', 'METAL') for part in partsdb.palette(parts, family)]
    for i in range(1, 31):
        part = by_key[partsdb.fe.bin_hash(f'PEARL{i}_PAINT')]
        rgb = parts.rgb(part)
        if len(set(rgb)) == 1 and parts.get(part, 'DISPRED') is not None:
            rgb = tuple(parts.get(part, 'DISP' + c) for c in ('RED', 'GREEN', 'BLUE'))
        paint.append(rgb)
    palettes = [('paints', paint, 150)]
    for name, family in (('vinyl_cols', 'VINYL'), ('rim_cols', 'RIMS'), ('caliper_cols', 'CALIPERS'),
                         ('hose_cols', 'HOSES'), ('muffler_cols', 'MUFFLERS')):
        palettes.append((name, [parts.rgb(part) for part in partsdb.palette(parts, family)], 60))
    output = StringIO()
    for name, colours, expected in palettes:
        if len(colours) != expected:
            raise ValueError(f'Unexpected {name} palette size')
        output.write(partsdb.c_table(name, colours) + '\n')
    with redirect_stdout(output):
        partsdb.specialties(parts)
    return output.getvalue()


def prepare(disc, output):
    data = (disc / 'SLUS_210.65').read_bytes()
    if hashlib.sha256(data).hexdigest() != EXECUTABLE_HASH:
        raise ValueError('Expected the US PS2 SLUS-21065 executable')
    game = disc / 'NFSUNDER'
    n2formats.PLATFORM = 'ps2'
    steering = []
    for name, offset, shape in STEERING:
        values = floats(data, offset, shape[0] * (shape[1] if len(shape) == 2 else 1))
        if len(shape) == 2:
            values = chunks_of(values, shape[1])
        steering.append(declaration('float', name, shape, values))
    headers = tuning(data)
    headers.update({
        'fe_hydraulics_table.h': hydraulics(data),
        'steering_tables.h': ''.join(steering),
        'career_camera.h': camera(game),
        'garage_tables.h': garage(game),
        'terrain_noise.h': declaration('float', 'bumps', (16,), floats(data, 0x42f000, 16)),
        'impact_tables.h': declaration('float', 'SENS', (9,), floats(data, 0x36bef8, 9)) +
                         declaration('float', 'SHARE', (20,), floats(data, 0x3b41c0, 20)),
    })
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as staging:
        staging = Path(staging)
        for name, text in headers.items():
            (staging / name).write_text(BANNER + text, encoding='utf-8')
        for name in headers:
            (staging / name).replace(output / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disc', type=Path, help='extracted disc root with SLUS_210.65 and NFSUNDER')
    args = parser.parse_args()
    try:
        prepare(args.disc, SOURCE / 'local-data')
    except (OSError, ValueError, struct.error, KeyError, StopIteration) as error:
        parser.exit(1, f'Cannot prepare build data: {error}\n')
    print('Prepared local game build data.')


if __name__ == '__main__':
    main()
