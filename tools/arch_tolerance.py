#!/usr/bin/env python3
"""Reference equivalence for the outputs whose bytes depend on the CPU.

ffmpeg's scaling and resampling and numpy's float reductions take their
own SIMD paths on x86-64 and ARM64, so a few outputs round differently:
the movies (a few JPEG bytes, 0.005% of a movie's size), the music and
speech MP3s (re-encoded from PCM a least significant bit apart), the
geometry (.n2cy, a byte or two where a float lands on the other side of a
decision) and the racing lines (a float's last bit, 0.00024 m). Windows'
Pillow (MSVC) also quantizes the vinyls' and magazines' palettes a little
differently (0.006% of a vinyl's pixels on average, most identical), and
its numpy a few bytes of the engine sounds (a byte or two), the mipmaps'
header (their geometry's size) and so the data ID. The line forks' ways
are pulled tight by the racing line's minimum-curvature QP, whose passes
converge a little differently on each CPU's linear algebra: the same forks
(where each leaves and rejoins the line, its signs, its third lane), a
way a point or two longer or shorter (the 4086's back way: 111 points on
ARM, 109 on Windows) or, where a pass finds no solution, left as the
routes draw it. The
reference manifest keeps, for those files, their shape: the structural
fields their readers depend on (frame and sample counts, picture size,
grids, texture keys, line ids and point counts, the forks) and their size. A file
whose bytes differ from the reference passes as equivalent only when it
is of one of these kinds, its shape is the reference's and its size is
within the kind's tolerance. pack.n2pk is checked entry by entry: every
other entry must be byte-identical. Python stdlib only.

    arch_tolerance.py describe GAME MANIFEST   adds the shapes of GAME's
        files (the reference build, its hashes the manifest's) to MANIFEST
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

# The share of the size a file of each kind may differ by (0: exact).
TOLERANCE = {'.n2mv': 0.001, '.n2cy': 0.0005, '.n2rl': 0.0, '.mp3': 0.0, '.big': 0.002,
             '.n2vc': 0.002, '.n2mg': 0.0, '.n2lf': 0.01, '.n2en': 0.0, '.n2mp': 0.0,
             'data-id.txt': 0.0}
PACK = 'pack.n2pk'


def kind(name):
    return next((k for k in TOLERANCE if name.endswith(k)), None)


def _digest(value):
    return hashlib.sha256(json.dumps(value, separators=(',', ':')).encode()).hexdigest()


def shape(name, data):
    """The structural fields of a tolerant kind's file (a digest), None
    for any other file."""
    k = kind(name)
    if k == '.n2mv':    # 'N2MV', version, fps, width, height, frames, rate, samples, index offset
        return _digest(list(struct.unpack_from('<4s8I', data, 0)[1:]))
    if k == '.n2cy':    # header without its floats, then the texture keys
        h = struct.unpack_from('<4sIf2i3I4f2I', data, 0)
        textures, tex_at = h[7], h[13]
        keys = [struct.unpack_from('<I', data, tex_at + 12 * i)[0] for i in range(textures)]
        return _digest([h[1], h[3], h[4], h[5], h[6], textures, h[12], tex_at, keys])
    if k == '.n2rl':    # every line's id, circuit flag and point count
        count = struct.unpack_from('<I', data, 8)[0]
        return _digest([list(struct.unpack_from('<2H2I', data, 12 + 12 * i)[:3]) for i in range(count)])
    if k == '.n2vc':    # the catalogue's rows without their offsets and sizes
        count = struct.unpack_from('<I', data, 8)[0]
        return _digest([data[12 + 60 * i:12 + 60 * i + 52].hex() for i in range(count)])
    if k == '.n2mg':    # header and table (fixed-size images: their offsets too)
        count, at = struct.unpack_from('<I', data, 8)[0], 12
        for _ in range(count):
            at += 12 + 8 * data[at + 8]
        return _digest([data[:at].hex()])
    if k == '.n2lf':    # per event and way its forks: fork and rejoin point, signs, third, enter
        count = struct.unpack_from('<I', data, 8)[0]
        forks = []
        for i in range(count):
            eid, way, nf, at = struct.unpack_from('<H2BI', data, 12 + 8 * i)
            heads = []
            for _ in range(nf):
                ja, jr, *types, third, _, points, enter, _ = struct.unpack_from('<2H6B3H', data, at)
                heads.append([ja, jr, types, third, enter])
                at += 20 + 24 * points
            forks.append([eid, way, heads])
        return _digest(forks)
    if k == '.n2mp':    # 'N2MP', version, texture count (not the geometry's size)
        return _digest([data[:8].hex(), data[12:16].hex()])
    if k == '.n2en':
        return _digest([data[:4].hex()])
    if k == 'data-id.txt':
        return _digest([len(data) == 5 and data[4:] == b'\n' and all(c in b'0123456789abcdef' for c in data[:4])])
    if k in ('.mp3', '.big'):
        return _digest([k])
    return None


def _legacy_n2lf_shape(data):
    """The forks' table (event, way, fork count, offset): the shape 0.2.4's
    manifests recorded for the line forks, with no size tolerance. The
    update checks the previous folder against its own version's manifest."""
    count = struct.unpack_from('<I', data, 8)[0]
    return _digest([data[:12 + 8 * count].hex()])


def pack_entries(data):
    """{name: bytes} of a pack.n2pk (tools/ms_package.py's write_pack)."""
    magic, _, n, size = struct.unpack_from('<4sIII', data, 0)
    if magic != b'N2PK':
        raise ValueError('not a pack.n2pk')
    out, at = {}, 16
    for _ in range(n):
        off, length, name_len = struct.unpack_from('<IIH', data, at)
        out[data[at + 10:at + 10 + name_len].decode()] = data[off:off + length]
        at += 10 + name_len + (-(10 + name_len) % 4)
    return out


def describe(name, data):
    """The manifest's record of a reference file: size and shape, or for
    the pack every entry's hash and its tolerant entries' records."""
    if name == PACK:
        entries = pack_entries(data)
        return {'entries': {n: hashlib.sha256(b).hexdigest() for n, b in sorted(entries.items())},
                'tolerant': {n: describe(n, b) for n, b in sorted(entries.items()) if kind(n)}}
    return {'size': len(data), 'shape': shape(name, data)}


def equivalent(name, data, ref):
    """Whether a file whose hash differs from the reference's is the same
    output rounded on another CPU (ref: its describe() record)."""
    if ref is None:
        return False
    if name == PACK:
        try:
            entries = pack_entries(data)
        except (ValueError, struct.error):
            return False
        if set(entries) != set(ref['entries']):
            return False
        for n, b in entries.items():
            if hashlib.sha256(b).hexdigest() != ref['entries'][n] and not equivalent(n, b, ref['tolerant'].get(n)):
                return False
        return True
    k = kind(name)
    if k is None:
        return False
    try:
        same_shape = shape(name, data) == ref['shape']
        if k == '.n2lf' and not same_shape and _legacy_n2lf_shape(data) == ref['shape']:
            return len(data) == ref['size']
    except struct.error:
        return False
    return same_shape and abs(len(data) - ref['size']) <= TOLERANCE[k] * ref['size']


def main():
    if len(sys.argv) != 4 or sys.argv[1] != 'describe':
        raise SystemExit(__doc__)
    game, manifest_path = Path(sys.argv[2]), Path(sys.argv[3])
    manifest = json.loads(manifest_path.read_text())
    records = {}
    for name, digest in sorted(manifest['files'].items()):
        if name != PACK and not kind(name):
            continue
        data = (game / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f'{name}: not the reference build (hash differs)')
        records[name] = describe(name, data)
    manifest['equivalence'] = records
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
    print(f'{manifest_path.name}: {len(records)} records')


if __name__ == '__main__':
    main()
