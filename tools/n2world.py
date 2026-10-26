"""NFSU2 Xbox world (TRACKS/L4RA + STREAML4RA) reader. Python stdlib only.

Layouts verified on the USA Xbox disc:
- L4RA.BUN 0x34110: 80-byte streaming records; name[8], u16 number@+8,
  u32 type@+0x10, offset@+0x14, size@+0x18 inside STREAML4RA.BUN.
- Section objects reuse the car chunk 0x80134010; header 0x134011 has flags
  @+0x0c (0x400000 = drivable surface), key@+0x10 = game_hash(full name),
  bbox @+0x20/+0x30, matrix @+0x40, name @+0xa4 (truncated to 27 chars).
  World vertices are 24 bytes (position3 f32, BGRA prelit colour, UV2 f32)
  or 32 bytes for dynamic props (car layout); stride u16 @+10 of 0x134b00.
- 0x34102 scenery info, 68 bytes: name[32], model keys for 3 detail
  levels @+0x20, radius f32 @+0x3c.
- 0x34103 instances after 12 bytes of 0x11 filler, 64 bytes: bbox min/max,
  u16 info index @+0x18, u16 flags @+0x1a, position @+0x20, 3x3
  rotation as int16/8192 @+0x2c.
"""
import math
import struct

from n2formats import archived, chunks, game_hash, ps2_world, u32, unpad

DRIVABLE = 0x400000


class Stream:
    def __init__(self, game, track='L4RA'):
        self.name = track
        self.track = archived(game, f'TRACKS/{track}.BUN')
        table = [self.track[p:p+n] for t, p, n in chunks(self.track) if t == 0x34110]
        if len(table) != 1 or len(table[0]) % 80:
            raise ValueError('streaming section table mismatch')
        self.sections = {}
        for at in range(0, len(table[0]), 80):
            r = table[0][at:at+80]
            name = r[:8].split(b'\0')[0].decode('ascii')
            kind, offset, size = struct.unpack_from('<3I', r, 0x10)
            if kind == 1:
                self.sections[name] = (offset, size)
        z = (game / 'ZDIR.BIN').read_bytes()
        entries = [e for e in struct.iter_unpack('<6I', z) if e[0] == game_hash(f'TRACKS/STREAM{track}.BUN')]
        if len(entries) < 1:
            raise ValueError('stream file missing')
        _, archive, sector, _, size, _ = entries[0]
        self.base, self.size = sector * 2048, size
        self.path = game / f'ZZDATA{archive}.BIN'
        if self.base + size > self.path.stat().st_size:
            raise ValueError('stream outside archive')
        self.cache = {}

    def section(self, name):
        if name not in self.cache:
            offset, size = self.sections[name]
            if offset + size > self.size:
                raise ValueError(f'section {name} outside stream')
            with self.path.open('rb') as f:
                f.seek(self.base + offset)
                self.cache[name] = f.read(size)
        return self.cache[name]


class WorldObject:
    """verts: (x,y,z,colour BGRA u32,u,v); subs: (indices, texkey)."""
    def __init__(self, name, key, flags, verts, subs):
        self.name, self.key, self.flags, self.verts, self.subs = name, key, flags, verts, subs


def section_objects(data):
    out = {}
    for tag, p, n in chunks(data):
        if tag != 0x80134010:
            continue
        leaves = {t: data[o:o+s] for t, o, s in chunks(data, p, p+n) if not t & 0x80000000}
        h = unpad(leaves[0x134011])
        flags, key = struct.unpack_from('<2I', h, 0x0c)
        name = h[164:].split(b'\0')[0].decode('ascii', 'replace')
        if 0x134a02 in leaves:      # the PS2's VIF strips, prelit throughout
            verts, psubs = ps2_world(leaves)
            tex = [a for a, b in struct.iter_unpack('<II', leaves[0x134012])] if 0x134012 in leaves else []
            subs = [(tuple(ids), tex[ti] if tex else 0) for ids, ti in psubs if ids]
            out[key] = WorldObject(name, key, flags, verts, subs)
            continue
        if 0x134b01 not in leaves:
            continue
        vb, ib, sb = [unpad(leaves[t]) for t in (0x134b01, 0x134b03, 0x134b02)]
        stride = struct.unpack_from('<H', unpad(leaves[0x134b00]), 10)[0] if 0x134b00 in leaves else 24
        if stride not in (24, 32) or len(vb) % stride or len(ib) % 2 or len(sb) % 64:
            raise ValueError(f'world layout mismatch: {name}')
        if stride == 24:      # prelit: position, BGRA colour, UV
            verts = list(struct.iter_unpack('<3fI2f', vb))
        else:                 # dynamic props: position, UV, normal (car layout)
            verts = []
            for x, y, z, u, v, nx, ny, nz in struct.iter_unpack('<8f', vb):
                lum = int(150 + 90 * max(-1.0, min(1.0, nz * .6 + nx * .3)))
                verts.append((x, y, z, 0xff000000 | lum * 0x010101, u, v))
        ids = struct.unpack('<' + str(len(ib)//2) + 'H', ib)
        tex = [a for a, b in struct.iter_unpack('<II', leaves[0x134012])] if 0x134012 in leaves else []
        subs, expected = [], 0
        for at in range(0, len(sb), 64):
            w = struct.unpack_from('<16I', sb, at)
            count, ti, first = w[6], w[7], w[9]
            if first != expected or count % 3 or first + count > len(ids) or (tex and ti >= len(tex)):
                raise ValueError(f'invalid world submesh: {name}')
            expected += count
            # 31 shipped objects index past their vertices; drop those ranges.
            if max(ids[first:first+count], default=0) < len(verts):
                subs.append((ids[first:first+count], tex[ti] if tex else 0))
        out[key] = WorldObject(name, key, flags, verts, subs)
    return out


class Instance:
    def __init__(self, name, keys, radius, bmin, bmax, flags, pos, rot, section=0, index=0):
        self.name, self.keys, self.radius = name, keys, radius
        self.bmin, self.bmax, self.flags, self.pos, self.rot = bmin, bmax, flags, pos, rot
        self.section, self.index = section, index     # scenery section number (0x34101 +12), place in its list
        self.precull = None   # its preculler row (section_instances; tools/convert_city.py PRECULL)


def section_instances(data):
    out = []
    for tag, p, n in chunks(data):
        if tag != 0x80034100:
            continue
        leaves = {t: data[o:o+s] for t, o, s in chunks(data, p, p+n) if not t & 0x80000000}
        info, inst = leaves.get(0x34102, b''), unpad(leaves.get(0x34103, b''))
        precull = unpad(leaves.get(0x34106, b''))   # 128-byte rows: a bit per camera cell of 32 m the instance is hidden from
        head = leaves.get(0x34101, b'')
        number = struct.unpack_from('<I', head, 12)[0] if len(head) >= 16 else 0
        if len(info) % 68 or len(inst) % 64:
            raise ValueError('scenery chunk layout mismatch')
        infos = []
        for at in range(0, len(info), 68):
            name = info[at:at+32].split(b'\0')[0].decode('ascii', 'replace')
            keys = struct.unpack_from('<3I', info, at + 0x20)
            radius, = struct.unpack_from('<f', info, at + 0x3c)
            infos.append((name, keys, radius))
        for at in range(0, len(inst), 64):
            r = inst[at:at+64]
            bmin, bmax = struct.unpack_from('<3f', r, 0), struct.unpack_from('<3f', r, 12)
            index, flags = struct.unpack_from('<HH', r, 0x18)
            pos = struct.unpack_from('<3f', r, 0x20)
            rot = [v / 8192 for v in struct.unpack_from('<9h', r, 0x2c)]
            if index >= len(infos):
                raise ValueError('instance references missing scenery info')
            name, keys, radius = infos[index]
            row, = struct.unpack_from('<i', r, 0x1c)
            out.append(Instance(name, keys, radius, bmin, bmax, flags, pos, rot, number, at // 64))
            out[-1].precull = precull[row * 128:row * 128 + 128] if 0 <= row and row * 128 + 128 <= len(precull) else None
    return out


def scenery_groups(track_bun):
    """A level's scenery groups (chunk 0x34108: per group 8 bytes, name[32],
    hash, u32, u16 count, u16, count x u16 override index, 4-aligned) over
    its override table (0x34107: u16 scenery section, u16 instance index,
    u16 flags, u16): {name: {(section, instance), ...}}. The closed tracks'
    BARRIERS_<event> groups are the barriers each event puts up."""
    leaves = {t: track_bun[p:p+n] for t, p, n in chunks(track_bun) if not t & 0x80000000}
    table = [struct.unpack_from('<2H', leaves.get(0x34107, b''), at) for at in range(0, len(leaves.get(0x34107, b'')) - 7, 8)]
    data, out, at = leaves.get(0x34108, b''), {}, 0
    while at + 48 <= len(data):
        name = data[at + 8:at + 40].split(b'\0')[0].decode('ascii', 'replace')
        count, = struct.unpack_from('<H', data, at + 48)
        refs = struct.unpack_from(f'<{count}H', data, at + 52)
        out[name] = {table[r] for r in refs if r < len(table)}
        at += 52 + 2 * count
        at += (-at) % 4
    return out


def smokeable_types(track_bun):
    """The knock-over props' types (SmokeableInfo, chunk 0x34026: 256 bytes
    each from the first 16-aligned byte): {key: type}. Of each (offsets in
    the record): name[24] @+8; RigidBodyInfo @+0x20: mass (t), box sizes,
    the inertia's diagonal @+0x30/+0x44/+0x58; its key @+0x80 (also its
    GJK polytope's); emitter groups @+0x84 (lit when its anchor gives way)
    and @+0x88 (lit while awake); pieces it breaks into @+0x8c (count) and
    @+0xa0 (their types); centre of gravity @+0xb0; seconds awake before it
    goes @+0xc0 (0: it stays); anchor @+0xc4 (the impulse, t m/s, it takes
    to move: Smokeable::ConsiderImpulse); bounce @+0xcc (the game keeps it
    at 0.005 at most: Smokeable::Init); terrain hash @+0xd4; category hash
    @+0xdc (SmokeableCategoryNameTable: the impact's sound); friction @+0xe0
    (at least 0.3); a flag @+0xe4 that keeps it from being deleted far away.
    Axes are the model's, game space (x forward, y left, z up)."""
    from n2formats import game_hash
    cats = ['NONE', 'GARBAGE_BAG', 'SHOPPING_CART', 'METAL_FENCE', 'TRAFFIC_CONE', 'WATER_DRUM', 'OIL_DRUM',
            'PARKING_METER', 'ROAD_SIGN_METAL', 'ROAD_SIGN_WOOD', 'MARKET_STALL', 'SANDWICH_BOARD', 'SAW_HORSE',
            'METAL_SMALL', 'METAL_BIG', 'WOODBOX', 'TRASHCAN', 'GLASS', 'CHAIR']
    by_hash = {game_hash(c): c for c in cats}
    out = {}
    for t, p, n in chunks(track_bun):
        if t != 0x34026:
            continue
        at = (p + 15) & ~15
        for k in range((p + n - at) // 256):
            r = track_bun[at + 256 * k:at + 256 * (k + 1)]
            mass, = struct.unpack_from('<f', r, 0x20)
            key, em1, em2, pieces = struct.unpack_from('<4I', r, 0x80)
            life, anchor, _, bounce = struct.unpack_from('<4f', r, 0xc0)
            friction, keep = struct.unpack_from('<fI', r, 0xe0)
            out[key] = dict(name=r[8:0x20].split(b'\0')[0].decode('ascii', 'replace'), mass=mass,
                            inertia=struct.unpack_from('<f', r, 0x30) + struct.unpack_from('<f', r, 0x44) + struct.unpack_from('<f', r, 0x58),
                            cg=struct.unpack_from('<3f', r, 0xb0), emitters=(em1, em2),
                            pieces=struct.unpack_from(f'<{min(pieces, 4)}I', r, 0xa0), life=life, anchor=anchor,
                            bounce=min(bounce, .005), friction=max(friction, .3), keep=keep,
                            category=by_hash.get(struct.unpack_from('<I', r, 0xdc)[0], 'NONE'))
    return out


def smokeable_spawners(stream):
    """The city's knock-over props (chunk 0x34027 of each scenery section,
    SmokeableSpawnerPack: 16-byte header with s16 section @+8, s16 first id
    @+10, s16 count @+12, then 112 bytes a spawner): [spawner] with its id,
    section, type key @+0, the scenery instance it stands for (u16 override
    @+4, in the level's table 0x34107: section, instance), that instance's
    detail keys, the moving model's key @+8, its box (@+0x10, +0x20) and its
    4x4 matrix @+0x30 (rows: local x, y, z axes, then the place; game
    space). A spawner a few sections repeat counts once."""
    leaves = {t: stream.track[p:p+n] for t, p, n in chunks(stream.track) if not t & 0x80000000}
    ov = leaves.get(0x34107, b'')
    table = [struct.unpack_from('<2H', ov, a) for a in range(0, len(ov) - 7, 8)]
    found, wanted = {}, set()
    for name in stream.sections:
        d = stream.section(name)
        for t, p, n in chunks(d):
            if t != 0x34027:
                continue
            b = (p + 15) & ~15
            section, first, count = struct.unpack_from('<3h', d, b + 8)
            for k in range(count):
                s = b + 0x10 + 112 * k
                key, override = struct.unpack_from('<IH', d, s)
                model, = struct.unpack_from('<I', d, s + 8)
                m = [struct.unpack_from('<4f', d, s + 0x30 + 16 * r) for r in range(4)]
                found.setdefault(first + k, dict(id=first + k, section=section, type=key, model=model,
                                                 instance=table[override] if override < len(table) else None,
                                                 box=(struct.unpack_from('<3f', d, s + 0x10), struct.unpack_from('<3f', d, s + 0x20)),
                                                 matrix=[r[:3] for r in m]))
                wanted.add(found[first + k]['instance'])
        stream.cache.clear()
    keys = {}
    for name in stream.sections:
        for inst in section_instances(stream.section(name)):
            if (inst.section, inst.index) in wanted:
                keys[(inst.section, inst.index)] = inst
        stream.cache.clear()
    for s in found.values():
        inst = keys.get(s['instance'])
        s['keys'] = inst.keys if inst else (s['model'], 0, 0)
        s['place'] = (inst.keys[0], tuple(round(v, 2) for v in inst.pos), tuple(round(v, 3) for v in inst.rot)) if inst else None
    return sorted(found.values(), key=lambda s: s['id'])


def drivable_sections(track_bun):
    """The level's precomputed visibility (PVS: chunk 0x80034150, read by
    the game's VisibleSectionManager::Loader; tools/convert_pvs.py has the
    layouts): (lod_offset, {section: (box x0 y0 x1 y1, [(x, y)...])} the
    boundaries, game X Y, {drivable section: [sections visible from it]})."""
    sub = {}
    for t, p, n in chunks(track_bun):
        if t == 0x80034150:
            sub = {t2: track_bun[p2:p2 + n2] for t2, p2, n2 in chunks(track_bun, p, p + n)}
    if 0x34152 not in sub or 0x34153 not in sub:
        raise ValueError('no visible sections (0x80034150) in the level')
    lod, = struct.unpack_from('<i', sub.get(0x34151, b'\0' * 4), 0)
    b, at, bounds = sub[0x34152], 0, {}
    while at + 36 <= len(b):
        sec, npt = struct.unpack_from('<hh', b, at + 8)
        box = struct.unpack_from('<4f', b, at + 12)
        bounds[sec] = (box, [struct.unpack_from('<2f', b, at + 36 + 8 * k) for k in range(npt)])
        at += 36 + 8 * npt
    if at != len(b):
        raise ValueError('boundary chunk layout mismatch')
    b, visible = sub[0x34153], {}
    if len(b) % 164:
        raise ValueError('drivable section chunk layout mismatch')
    for at in range(0, len(b), 164):
        sec, = struct.unpack_from('<h', b, at + 8)
        count, = struct.unpack_from('<i', b, at + 16)
        if not 0 <= count <= 72:
            raise ValueError(f'drivable section {sec}: {count} visible')
        visible[sec] = [v for v in struct.unpack_from(f'<{count}h', b, at + 20) if v >= 0]
    return lod, bounds, visible


HORIZON = range(2600, 2700)   # sections the game draws from everywhere (the sky, the planes)


def course_regions(track_bun):
    """A closed track's regions: its drivable sections joined when one draws
    the other (L4RB's three drag strips, each with its own panorama, are
    three). Returns (zone_of(x, y) -> drivable section or None,
    {drivable section: region}, {region: sections any of its drivable
    sections draws})."""
    try:
        _, bounds, visible = drivable_sections(track_bun)
    except ValueError:   # a track without the chunk: no regions
        bounds, visible = {}, {}
    parent = {z: z for z in visible}
    def root(z):
        while parent[z] != z:
            parent[z] = parent[parent[z]]
            z = parent[z]
        return z
    for z, seen in visible.items():
        for w in seen:
            if w in parent:
                parent[root(w)] = root(z)
    region = {z: root(z) for z in visible}
    drawn = {}
    for z, r in region.items():
        drawn.setdefault(r, set(HORIZON)).update(visible[z])
    def inside(pts, x, y):
        c = False
        for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
            if (y0 > y) != (y1 > y) and x < x0 + (x1 - x0) * (y - y0) / (y1 - y0):
                c = not c
        return c
    def zone_of(x, y):
        for z, (box, pts) in bounds.items():
            if z in visible and box[0] <= x <= box[2] and box[1] <= y <= box[3] and inside(pts, x, y):
                return z
        return None
    return zone_of, region, drawn


WALL_ENDLESS = (-1000.0, 3000.0)     # z of a wall with no top or bottom


def section_collision(data):
    """The game's own collision of a section (TrackPolygonCollisionBody):
    chunk 0x80034130 holds a header (0x34131: u32 section number, bounds),
    32-byte polygons (0x34134), a bit-packed lookup tree (0x34136) and its
    index table (0x34137). Polygon: u8 barrier group, u8 ?, u8 surface,
    u8 flags, s16 base height (m), u16 0xffff, then 4 x s16 X, 4 x s16 Y
    (1/8 m, game axes) and 4 x s16 Z (base + z/256). Flags bit 1: a wall;
    bit 2 with it: a wall with no top or bottom (z +-32512, base 0), the 2D
    segment its vertices trace. Triangles repeat their last vertex. The
    group (walls only) indexes the level's barrier groups (barrier_groups):
    0 stands always, the others with their event or career stage.
    Returns [(group, flags, surface, ((x, y, z) x 4))]."""
    out = []
    for tag, p, n in chunks(data):
        if tag != 0x80034130:
            continue
        leaves = {t: data[o:o+s] for t, o, s in chunks(data, p, p+n)}
        polys = leaves.get(0x34134, b'')
        if len(polys) % 32:
            raise ValueError('collision polygon size mismatch')
        for at in range(0, len(polys), 32):
            group, _, surface, flags, base = struct.unpack_from('<4Bh', polys, at)
            q = struct.unpack_from('<12h', polys, at + 8)
            if flags & 6 == 6:
                zs = [WALL_ENDLESS[0] if z < 0 else WALL_ENDLESS[1] for z in q[8:]]
            else:
                zs = [base + z / 256 for z in q[8:]]
            out.append((group, flags, surface, tuple((q[i] / 8, q[4 + i] / 8, zs[i]) for i in range(4))))
    return out


def barrier_groups(track_bun):
    """The level's barrier groups (chunk 0x3412f, 32 bytes each: u32 hash,
    u32 polygon count, u8 1, name[23]): NONE, then BARRIERS_<event>,
    PLAYER_BARRIERS_<event> (the invisible walls that keep a race on its
    course) and, in the city, BARRIERS_CAREER<stage>."""
    data = b''.join(track_bun[p:p+n] for t, p, n in chunks(track_bun) if t == 0x3412f)
    return [data[at+9:at+32].split(b'\0')[0].decode('ascii', 'replace') for at in range(0, len(data) - 31, 32)]


def track_collision(stream):
    """Every section's collision polygons, once each (neighbouring sections
    repeat the polygons they share): ([(ground triangle, road, terrain)], {barrier
    group name: wall polygons}); the walls that always stand are under ''.
    Ground: the polygons that are not walls, face up and slope less than
    ~63 deg; steeper ones (kerb faces) go with the walls, where the
    collision grid keeps only what stands 0.5 m above the ground. The ones
    facing down are ceilings: the undersides of decks, drawn as
    TRN_*_CeilingsA 2-4 m under the deck's TRN_*_RoadA (all of them carry
    flag 0x08, and only they do). Taken for ground, they put roads,
    traffic and courses inside the viaducts (the 4088's freeway for 500 m,
    into a hole where the grid dropped the ceiling). road: without flag
    0x08; the collision grid prefers road where levels stack. terrain: the
    polygon's surface byte, the game's TerrainType (1 ROAD, 8 DIRT, 11
    COBBLESTONE, 15 GRASS, 16 SIDEWALK...: Speed.elf's TerrainTypeInfoTable)."""
    names = barrier_groups(stream.track)
    seen, ground, walls = set(), [], {}
    for name in stream.sections:
        for group, flags, surface, v in section_collision(stream.section(name)):
            if (group, v) in seen:
                continue
            seen.add((group, v))
            if flags & 2:
                key = names[group] if 0 < group < len(names) else ''
                walls.setdefault(key, []).append(v)
                continue
            for tri in ((v[0], v[1], v[2]), (v[0], v[2], v[3])):
                (ax, ay, az), (bx, by, bz), (cx, cy, cz) = tri
                ux, uy, uz, wx, wy, wz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
                nx, ny, nz = uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx
                length = math.sqrt(nx * nx + ny * ny + nz * nz)
                if length < 1e-6:
                    continue
                if abs(nz) / length >= .45:
                    if nz > 0:
                        ground.append((tri, not flags & 8, surface))
                else:
                    walls.setdefault('', []).append(tri)
        stream.cache.clear()
    return ground, walls


def place(inst):
    """Row-vector rotation (v' = v R + t), as the int16 rows read on disc."""
    r, (tx, ty, tz) = inst.rot, inst.pos
    def f(x, y, z):
        return (x*r[0] + y*r[3] + z*r[6] + tx, x*r[1] + y*r[4] + z*r[7] + ty, x*r[2] + y*r[5] + z*r[8] + tz)
    return f


def tessellate(tri, limit):
    """Split triangles whose longest edge exceeds limit (PSP GE culls
    triangles crossing the near plane, so ground must stay small).
    Vertices are flat tuples starting with x, y, z; all fields interpolate."""
    out, stack = [], [tri]
    while stack:
        a, b, c = stack.pop()
        def d2(p, q):
            return (p[0]-q[0])**2 + (p[1]-q[1])**2 + (p[2]-q[2])**2
        e = [d2(a, b), d2(b, c), d2(c, a)]
        m = max(e)
        if m <= limit * limit:
            out.append((a, b, c))
            continue
        def mid(p, q):
            return tuple((p[i]+q[i])/2 for i in range(len(p)))
        if m == e[0]:
            n = mid(a, b); stack += [(a, n, c), (n, b, c)]
        elif m == e[1]:
            n = mid(b, c); stack += [(a, b, n), (a, n, c)]
        else:
            n = mid(c, a); stack += [(a, b, n), (n, b, c)]
    return out


def clip_square(tri, x0, y0, x1, y1):
    """Sutherland-Hodgman clip of a triangle (flat tuples, x/y first) to an
    axis-aligned square in the XY plane; returns a fan of triangles."""
    poly = list(tri)
    for axis, bound, keep_low in ((0, x0, False), (0, x1, True), (1, y0, False), (1, y1, True)):
        if not poly:
            return []
        out = []
        for i in range(len(poly)):
            a, b = poly[i], poly[(i + 1) % len(poly)]
            ina = a[axis] <= bound if keep_low else a[axis] >= bound
            inb = b[axis] <= bound if keep_low else b[axis] >= bound
            if ina:
                out.append(a)
            if ina != inb:
                t = (bound - a[axis]) / (b[axis] - a[axis])
                out.append(tuple(a[k] + (b[k] - a[k]) * t for k in range(len(a))))
        poly = out
    return [(poly[0], poly[i], poly[i + 1]) for i in range(1, len(poly) - 1)]


def _split(poly, axis, value):
    """Split a convex polygon (flat tuples) by the plane axis = value into
    the parts below and above. The cut point is interpolated from the edge's
    endpoints in a fixed order, so both triangles sharing an edge get the
    same point (no T-junctions between neighbours)."""
    low, high = [], []
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        if a[axis] <= value:
            low.append(a)
        if a[axis] >= value:
            high.append(a)
        if (a[axis] < value < b[axis]) or (b[axis] < value < a[axis]):
            p, q = (a, b) if a < b else (b, a)
            t = (value - p[axis]) / (q[axis] - p[axis])
            cut = tuple(p[k] + (q[k] - p[k]) * t for k in range(len(p)))
            cut = cut[:axis] + (value,) + cut[axis + 1:]
            low.append(cut)
            high.append(cut)
    return low, high


def grid_clip(tri, step, zstep):
    """Cut a triangle along a global grid (x and y every step, z every zstep).
    Every triangle uses the same planes, so shared edges are split at the
    same points on both sides and the mesh stays watertight; pieces are
    small enough that the PSP GE never has to cull one crossing the near
    plane right in front of the camera."""
    polys = [list(tri)]
    for axis, size in ((0, step), (1, step), (2, zstep)):
        out = []
        for poly in polys:
            lo = min(p[axis] for p in poly)
            hi = max(p[axis] for p in poly)
            k = math.floor(lo / size) + 1
            rest = poly
            while k * size < hi and rest:
                below, rest = _split(rest, axis, k * size)
                if len(below) >= 3:
                    out.append(below)
                k += 1
            if rest and len(rest) >= 3:
                out.append(rest)
        polys = out
    return [(poly[0], poly[i], poly[i + 1]) for poly in polys for i in range(1, len(poly) - 1)]
