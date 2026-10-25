"""Original scenery ownership until the city's final geometry pack boundary.

The numeric geometry producers are unchanged. Shop-door static twins remain
in the classification context, then leave only the static archive; the
original animated door is emitted separately by convert_worldanim.py.
"""
import math
import struct

_INSTANCE_KEY = struct.Struct('<3I12d')

DOOR_PREFIXES = ('XO_DOORPIECE', 'ANM_DOORANIM')
DOOR_MODELS = frozenset((0x10246183, 0x142ef2a4, 0x183983c5,
                         0x1c4414e6, 0xc1035785))


class OwnedTriangle:
    __slots__ = ('_points', 'owner')

    def __init__(self, points, owner):
        if not isinstance(points, tuple) or len(points) != 3:
            raise ValueError('triangle producer must retain its three-point tuple')
        self._points, self.owner = points, owner

    def __len__(self): return 3
    def __iter__(self): return iter(self._points)
    def __getitem__(self, key): return self._points[key]
    def __hash__(self): return hash(self._points)

    def __eq__(self, other):
        if isinstance(other, OwnedTriangle):
            return self._points == other._points
        if isinstance(other, tuple):
            return self._points == other
        return NotImplemented


def instance_key(keys, pos, rot):
    """Exact internal identity without rebuilding a float tree in each worker.

    Only signed zero is canonicalized: Python numeric tuple keys already
    compare +0.0 and -0.0 equal. Geometry inputs themselves remain untouched.
    """
    if len(keys) != 3 or len(pos) != 3 or len(rot) != 9:
        raise ValueError('invalid original instance identity layout')
    if any(type(key) is not int or not 0 <= key <= 0xffffffff for key in keys):
        raise ValueError('instance model IDs must remain original uint32')
    values = []
    for raw in (*pos, *rot):
        value = float(raw)
        if not math.isfinite(value) or value != raw:
            raise ValueError('instance identity requires exact finite binary64 values')
        values.append(0.0 if value == 0.0 else value)
    return _INSTANCE_KEY.pack(*keys, *values)


def door_context(base, track):
    """Keep the original city door context, excluding other animated twins.

    Match the original flag, name and model together; do not delete by texture
    or spatial bounds. Closed tracks retain their existing exclusion rule.
    """
    kept, removed = [], set()
    for inst in base:
        animated = bool(inst.flags & 0x10)
        door = animated and track == 'L4RA' and inst.name.upper().startswith(DOOR_PREFIXES)
        if door and inst.keys[0] not in DOOR_MODELS:
            raise ValueError('unknown static door model; original ownership needs review')
        if not animated or door:
            kept.append(inst)
        if door:
            removed.add((inst.section, inst.index))
    owners = {}
    if removed:
        for inst in kept:
            key, owner = instance_key(inst.keys, inst.pos, inst.rot), (inst.section, inst.index)
            if key in owners and owners[key] != owner:
                raise ValueError('ambiguous scenery instance ownership')
            owners[key] = owner
    return kept, owners, frozenset(removed)


def owned_triangles(triangles, owner):
    for tri in triangles:
        yield OwnedTriangle(tri, owner)


def inherit_fan(fan, parent):
    return list(owned_triangles(fan, parent.owner)) if isinstance(parent, OwnedTriangle) else fan


def remove_owned(batches, removed, triangle_type):
    """Stable in-place filter, called only after stitch/mark_overlays.

    Bind the exact producer class explicitly. Raw tuples or a class loaded
    from a second module are refused rather than silently losing ownership.
    """
    count = 0
    for key in tuple(batches):
        tris, keep = batches[key], 0
        for tri in tris:
            if not isinstance(tri, triangle_type):
                raise ValueError('static filter lost original triangle ownership')
            if tri.owner in removed:
                count += 1
            else:
                tris[keep] = tri
                keep += 1
        while len(tris) > keep:
            tris.pop()
        if not tris:
            del batches[key]
    return count
