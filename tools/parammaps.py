#!/usr/bin/env python3
"""The game's parameter maps by region: TRACKS/<track>.BUN chunk 0x8003B600.

Each map ("layer") is one 0x8003B601 chunk:
  0x3B602 (36 bytes): u32 the layer's name hash (name_hash below), f32 x min,
    y min, x max, y max (the game's axes), u32 nodes, u32 sets, u32 bytes a
    set, u32 fields a set;
  0x3B603 / 0x3B604: each field's type (0 float, 1 int) and its offset in a
    set;
  0x3B605: the sets;
  0x3B608: the quadtree, u16 x 4 a node, or 0x3B607 the same in u8 (small
    maps); none: set 0 everywhere.
An inner node's four children are the quarters (x below the middle and y
below it, x below and y above, x above and y below, both above); a leaf has
0 first and its set second (GameCube ParameterMapLayer::Load and
GetParameterSetIndexFromQuadData8/16).

Layers of the city's L4RA: Normal Fog, Night Normal Fog, Night Raining
Fog, Fog Blend Distance, Sunrise Screen Tint, Night Screen Tint, Rain, Wind,
Ambience, Reverb, Volumes and three more not named.

write_layer() encodes a layer as the port reads it (psp-viewer/pmap.c):
f32 x min, y min, x max, y max, u32 nodes, u32 sets, u32 fields, nodes x
u16[4], sets x fields f32 (ints as floats).
"""
import struct

from n2formats import archived


def name_hash(s):
    """The game's bStringHash: h * 33 + c from -1."""
    h = 0xffffffff
    for c in s.encode():
        h = (h * 33 + c) & 0xffffffff
    return h


def maps(game, track='L4RA'):
    """name hash -> (bounds, nodes, sets, fields, tree as u16 list, values as floats)"""
    d = archived(game, f'TRACKS/{track}.BUN')
    at = d.index(struct.pack('<I', 0x8003b600))
    blk = d[at + 8:at + 8 + struct.unpack_from('<I', d, at + 4)[0]]
    out, o = {}, 0
    while o + 8 <= len(blk):
        _, n = struct.unpack_from('<II', blk, o)
        sub, parts, p = blk[o + 8:o + 8 + n], {}, 0
        while p + 8 <= len(sub):
            t, m = struct.unpack_from('<II', sub, p)
            parts[t] = sub[p + 8:p + 8 + m]
            p += 8 + m
        o += 8 + n
        h = parts[0x3b602]
        name = struct.unpack_from('<I', h, 0)[0]
        bounds = struct.unpack_from('<4f', h, 4)
        nodes, sets, stride, fields = struct.unpack_from('<4I', h, 20)
        types = struct.unpack_from(f'<{fields}I', parts[0x3b603])
        offs = struct.unpack_from(f'<{fields}I', parts[0x3b604])
        vals = [float(struct.unpack_from('<f' if types[f] == 0 else '<i', parts[0x3b605], s * stride + offs[f])[0])
                for s in range(sets) for f in range(fields)]
        if 0x3b608 in parts:
            tree = list(struct.unpack_from(f'<{nodes * 4}H', parts[0x3b608]))
        elif 0x3b607 in parts:
            tree = list(parts[0x3b607][:nodes * 4])
        else:
            tree = []
        out[name] = (bounds, nodes if tree else 0, sets, fields, tree, vals)
    return out


def layer(game, name, track='L4RA'):
    return maps(game, track)[name_hash(name)]


def lookup(lay, x, y):
    """The set at a place (the game's x, y): down the quadtree to a leaf;
    set 0 outside the map or without a tree."""
    (x0, y0, x1, y1), nodes, sets, fields, tree, vals = lay
    if not nodes or not (x0 <= x <= x1 and y0 <= y <= y1):
        return 0
    n = 0
    while tree[n * 4]:
        mx, my = (x0 + x1) * .5, (y0 + y1) * .5
        k = 0 if x < mx else 2
        if x < mx:
            x1 = mx
        else:
            x0 = mx
        if y < my:
            y1 = my
        else:
            y0 = my
            k += 1
        n = tree[n * 4 + k]
    return tree[n * 4 + 1] if tree[n * 4 + 1] < sets else 0


def values(lay, s):
    fields = lay[3]
    return lay[5][s * fields:(s + 1) * fields]


def write_layer(lay):
    bounds, nodes, sets, fields, tree, vals = lay
    return (struct.pack('<4f3I', *bounds, nodes, sets, fields) + struct.pack(f'<{nodes * 4}H', *tree[:nodes * 4])
            + struct.pack(f'<{sets * fields}f', *vals))
