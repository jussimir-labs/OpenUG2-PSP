"""The game's route network: TRACKS/ROUTES<track>/ROUTESFREEROAM.BIN (routes
and signposts) and the event barriers of PATHS<id>.BIN, read raw.

Routes (leaf 0x34121, 8-byte header): a record at `at` has the route's
number (u16) at +2, its name at +8 (32 bytes), its point count (u16) at
+44, then the points from +132 every 56 bytes; the next record follows
after 140 + 56 x points bytes. A point: f32 X, Y (game axes) at +0; flags
at +0x0c (bits 0-10 the route number; posted for racers forward bit 11,
backward bit 18, for traffic 25/26, for players 27/28); u16 left and
right widths at +0x14/+0x16 (/256 m); s16 direction X, Y at +0x18/+0x1a
(/32767); s8 at +0x22 + L the driving lines' offsets to the right of each
one's way, in 128ths of the wider width (L 0 and 1 the racing lines, 2-5
the traffic lanes along the route, 6-9 those against it; a drag's lanes
are these, DragController::LockOnToNearestTrafficLane). The record's 8
lane bytes at +0x7c: non-zero where a lane exists (slots 0-3 along, 4-7
against, slot 3 the rightmost of its way; where an event's trains stand
on the L4RB rail yard drag, its routes there have only the free ones). The game keeps each record 8
bytes earlier in memory: the tracks it is left out of (4 x u16) sit at
+0x3c of the record, those it is kept for at +0x44.

Signposts (leaf 0x34122, 4-byte header, 32 bytes each): u16 new point
+4, old point +6, new route +8, old route +0x0a; flags at +0x0c: bits
0-3 the sign type for racers and the player, 4-7 for traffic (2
always, 4 optional, 7 never, 5 and 10 not taken, 8 "Player Always"),
8-11 and 12-15 the original racer and traffic types, put back on every
load (Signpost::RestoreSignposts);
bit 18 the sign stands for cars going backwards along the old route;
bit 23 the new route is driven against its way; bit 26 the game picks
between two such exits by the one gaining most on the race (its
MostFruitfulExit); bit 27 an optional sign is taken a third of the time
instead of half.

Event barriers (PATHS<id> leaf 0x3414d, 36 bytes each): f32 X, Y of each
end at +0 and +8, u8 count at +0x11 and that many u32 name hashes at
+0x14 (the game's bStringHash, h = h * 33 + c from 0xffffffff, case
kept) of the scenery groups that put the barrier up: BARRIERS_<id> and
PLAYER_BARRIERS_<id> for an event, BARRIERS_CAREER<n> in the career.

convert_races.py drives the routes as the original's racers do;
convert_routes.py (the road graph) and the traffic lanes read the same
file and are to move onto this module.
"""
import struct

from n2formats import archived, chunks


def read_routes(game, track='L4RA'):
    """{route number: [point dicts]}: x, y, flags, wl, wr (m), dx, dy,
    offset (the racing line's, m to the right of the route's way), lines
    (the 10 driving lines' offsets, m to the right of each one's way) and
    lanes (the route's 8 lane slots, True where the lane exists)."""
    d = archived(game, f'TRACKS/ROUTES{track}/ROUTESFREEROAM.BIN')
    leaf = [d[p:p+n] for t, p, n in chunks(d) if t == 0x34121][0]
    routes, at = {}, 8
    while at + 140 <= len(leaf):
        num, = struct.unpack_from('<H', leaf, at + 2)
        n, = struct.unpack_from('<H', leaf, at + 44)
        mask = tuple(b != 0 for b in leaf[at + 0x7c:at + 0x84])   # its traffic lanes: slots 0-3 along, 4-7 against
        pts = []
        for k in range(n):
            p = at + 132 + 56 * k
            if p + 56 > len(leaf):
                break
            x, y = struct.unpack_from('<2f', leaf, p)
            flags, = struct.unpack_from('<I', leaf, p + 0x0c)
            wl, wr, dx, dy = struct.unpack_from('<2H2h', leaf, p + 0x14)
            off, = struct.unpack_from('<b', leaf, p + 0x22)
            lines = struct.unpack_from('<10b', leaf, p + 0x22)
            pts.append(dict(x=x, y=y, flags=flags, wl=wl / 256, wr=wr / 256, dx=dx / 32767, dy=dy / 32767,
                            offset=off * max(wl, wr) / 256 / 128, lanes=mask,
                            lines=tuple(v * max(wl, wr) / 256 / 128 for v in lines)))
        routes[num] = pts
        at += 140 + 56 * n
    return routes


def route_events(game, track='L4RA'):
    """{route number: (events it is left out of, events it is kept for)}:
    u16 x 4 each at +0x3c and +0x44 of the record (0xffff none). L4RB's
    rail yard drag has three copies of its routes, one an event, each
    with the lanes its trains leave free (4211's left out of 4212 and
    4213, kept for 4211)."""
    d = archived(game, f'TRACKS/ROUTES{track}/ROUTESFREEROAM.BIN')
    leaf = [d[p:p+n] for t, p, n in chunks(d) if t == 0x34121][0]
    out, at = {}, 8
    while at + 140 <= len(leaf):
        num, = struct.unpack_from('<H', leaf, at + 2)
        n, = struct.unpack_from('<H', leaf, at + 44)
        off = {e for e in struct.unpack_from('<4H', leaf, at + 0x3c) if e != 0xffff}
        kept = {e for e in struct.unpack_from('<4H', leaf, at + 0x44) if e != 0xffff}
        out[num] = (off, kept)
        at += 140 + 56 * n
    return out


def route_for(events, route, eid):
    """Whether a route stands in an event (route_events)."""
    off, kept = events.get(route, (set(), set()))
    return eid not in off and (not kept or eid in kept)


def read_signposts(game, track='L4RA'):
    """[sign dicts]: new_route, new_point, old_route, old_point, flags,
    racer_type, traffic_type, backwards (bit 18), flip (bit 23)."""
    d = archived(game, f'TRACKS/ROUTES{track}/ROUTESFREEROAM.BIN')
    leaf = [d[p:p+n] for t, p, n in chunks(d) if t == 0x34122][0]
    out = []
    for at in range(4, len(leaf) - 31, 32):
        newp, oldp, newr, oldr, flags = struct.unpack_from('<4HI', leaf, at + 4)
        out.append(dict(new_route=newr, new_point=newp, old_route=oldr, old_point=oldp, flags=flags,
                        racer_type=flags & 15, traffic_type=flags >> 4 & 15,
                        backwards=bool(flags >> 18 & 1), flip=bool(flags >> 23 & 1)))
    return out


def name_hash(s):
    """The game's bStringHash."""
    h = 0xffffffff
    for c in s.encode():
        h = (h * 33 + c) & 0xffffffff
    return h


def read_event_barriers(game, track, eid, groups=None):
    """[((ax, ay), (bx, by))]: the barrier segments the event's groups put
    up (default BARRIERS_<id> and PLAYER_BARRIERS_<id>, a quick race's)."""
    try:
        d = archived(game, f'TRACKS/ROUTES{track}/PATHS{eid}.BIN')
    except (KeyError, ValueError):
        return []
    leaf = [d[p:p+n] for t, p, n in chunks(d) if t == 0x3414d]
    if not leaf:
        return []
    mine = {name_hash(g) for g in (groups or (f'BARRIERS_{eid}', f'PLAYER_BARRIERS_{eid}'))}
    out = []
    for at in range(0, len(leaf[0]) - 35, 36):
        ax, ay, bx, by = struct.unpack_from('<4f', leaf[0], at)
        n = min(leaf[0][at + 0x11], 4)
        if mine & set(struct.unpack_from(f'<{n}I', leaf[0], at + 0x14)):
            out.append(((ax, ay), (bx, by)))
    return out
