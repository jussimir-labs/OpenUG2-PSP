"""Indexed triangle lists -> triangle strips, for the PSP's GE.

On a real PSP an indexed vertex costs ~220 ns and a vertex of a plain strip
~50 ns (tools/psp-bench, reports/psp-bench-psp3000.json: the GE keeps no
vertex cache), so the city draws strips: each batch one strip, its pieces
joined by repeating the last vertex of one and the first of the next (the
triangles between them have no area and draw no pixel).

The viewer draws with face culling off, so a strip's alternating winding
does not matter: pieces are joined without parity fixes and a piece may
be grown from both ends.

stripify reorders the triangles for long strips (~2 vertices a triangle in
the city); in_order keeps their order (~3), for blends, whose overlaps
depend on it.
"""
from collections import Counter, defaultdict


def stripify(tris):
    """tris: [(a, b, c)] vertex ids. Returns the strip pieces, each a list
    of vertex ids whose every 3 consecutive ids are a triangle. Triangles
    with a repeated id (no area) are left out."""
    tris = [t for t in tris if t[0] != t[1] and t[1] != t[2] and t[0] != t[2]]
    edges = defaultdict(list)
    for k, (a, b, c) in enumerate(tris):
        for e in ((a, b), (b, c), (c, a)):
            edges[min(e), max(e)].append(k)
    used = bytearray(len(tris))

    def free_neighbours(k):
        a, b, c = tris[k]
        return sum(1 for e in ((a, b), (b, c), (c, a)) for m in edges[min(e), max(e)] if m != k and not used[m])

    def take(a, b):
        """The unused triangle on edge a-b with the fewest free neighbours."""
        best, score = None, 4
        for m in edges[min(a, b), max(a, b)]:
            if not used[m]:
                s = free_neighbours(m)
                if s < score:
                    best, score = m, s
        return best

    def grow(seq):
        while True:
            m = take(seq[-2], seq[-1])
            if m is None:
                return seq
            used[m] = 1
            seq.append(next(v for v in tris[m] if v != seq[-2] and v != seq[-1]))

    pieces = []
    # Start where the mesh is thinnest (fewest neighbours): the strips then
    # run along it instead of leaving lone triangles behind.
    for k in sorted(range(len(tris)), key=free_neighbours):
        if used[k]:
            continue
        used[k] = 1
        a, b, c = tris[k]
        # the rotation whose far edge has a free neighbour, so it grows
        for x, y, z in ((a, b, c), (b, c, a), (c, a, b)):
            if take(y, z) is not None:
                break
        seq = grow([x, y, z])
        seq.reverse()
        pieces.append(grow(seq))
    return pieces


def in_order(tris):
    """Strip pieces drawing the triangles in their order: a piece goes on
    while the next triangle shares its last edge. Triangles with a repeated
    id (no area, no pixel) are left out."""
    tris = [t for t in tris if len(set(t)) == 3]
    pieces, cur = [], None
    for k, t in enumerate(tris):
        if cur and cur[-2] in t and cur[-1] in t:
            cur.append(next(v for v in t if v != cur[-2] and v != cur[-1]))
            continue
        # a new piece, turned so that its far edge is the next triangle's
        nxt = tris[k + 1] if k + 1 < len(tris) else ()
        a, b, c = t
        cur = next((list(r) for r in ((a, b, c), (b, c, a), (c, a, b)) if r[1] in nxt and r[2] in nxt), [a, b, c])
        pieces.append(cur)
    return pieces


def join(pieces):
    """One strip from the pieces: the last id of one and the first of the next
    repeated in between (+2 vertices a join)."""
    out = []
    for p in pieces:
        if out:
            out += [out[-1], p[0]]
        out += p
    return out


def _turn(t):
    """A triangle's ids from its smallest on, its winding kept."""
    k = t.index(min(t))
    return t[k:] + t[:k]


def wound(strip, k):
    """The strip's k-th triangle as the GE winds it: every other one turned."""
    a, b, c = strip[k:k + 3]
    return (a, b, c) if k % 2 == 0 else (b, a, c)


def oriented(tris):
    """Strip pieces for a batch drawn with culling: each triangle keeps its
    winding as the GE reads a strip (the odd ones turned). stripify's
    pieces, cut where one would turn a triangle; a piece that starts turned
    starts on an odd place (its first id twice: a triangle with no area).
    Triangles with a repeated id are left out."""
    want = {}
    for t in tris:
        if len(set(t)) == 3:
            want[tuple(sorted(t))] = _turn(tuple(t))
    pieces = []

    def first(tri):
        return list(tri) if _turn(tri) == want[tuple(sorted(tri))] else [tri[0]] + list(tri)
    for piece in stripify(tris):
        cur = None
        for k in range(len(piece) - 2):
            tri = tuple(piece[k:k + 3])
            if cur is None:
                cur = first(tri)
                continue
            cur.append(piece[k + 2])
            if _turn(wound(cur, len(cur) - 3)) != want[tuple(sorted(tri))]:
                cur.pop()
                pieces.append(cur)
                cur = first(tri)
        if cur:
            pieces.append(cur)
    return pieces


def join_oriented(pieces):
    """join, each piece starting on an even place (its windings kept)."""
    out = []
    for p in pieces:
        if out:
            out += [out[-1], p[0]]
            if len(out) % 2:
                out.append(p[0])
        out += p
    return out


def check_oriented(tris, strips):
    """The strips together draw the triangles, each wound as given."""
    want = Counter(_turn(tuple(t)) for t in tris if len(set(t)) == 3)
    got = Counter(_turn(wound(st, k)) for st in strips for k in range(len(st) - 2) if len(set(st[k:k + 3])) == 3)
    if got != want:
        raise ValueError(f'oriented strip differs: {sum((want - got).values())} missing, {sum((got - want).values())} extra')


def triangles(strip):
    """The triangles of a strip with area (no repeated id), as sorted tuples."""
    return Counter(tuple(sorted(strip[k:k + 3])) for k in range(len(strip) - 2)
                   if len(set(strip[k:k + 3])) == 3)


def check(tris, strips):
    """The strips together draw exactly the list's triangles (as vertex sets)."""
    want = Counter(tuple(sorted(t)) for t in tris if len(set(t)) == 3)
    got = sum((triangles(st) for st in strips), Counter())
    if got != want:
        missing, extra = want - got, got - want
        raise ValueError(f'strip differs from its triangles: {sum(missing.values())} missing, {sum(extra.values())} extra')


if __name__ == '__main__':
    import random
    # a 40x40 grid of quads, shuffled, plus loose and degenerate triangles
    w = 40
    tris = []
    for y in range(w):
        for x in range(w):
            a, b, c, d = y * (w + 1) + x, y * (w + 1) + x + 1, (y + 1) * (w + 1) + x, (y + 1) * (w + 1) + x + 1
            tris += [(a, b, c), (b, d, c)]
    tris += [(5000, 5001, 5002), (6000, 6001, 6002), (7, 7, 8), (1, 2, 3), (1, 2, 3)]
    random.seed(1)
    random.shuffle(tris)
    for name, make in (('stripify', stripify), ('in_order', in_order)):
        strip = join(make(tris))
        check(tris, [strip])
        print(f'{name}: {len(tris)} triangles -> strip of {len(strip)} vertices ({len(strip) / len(tris):.2f} per triangle)')
    strip = join_oriented(oriented(tris))
    check_oriented(tris, [strip])
    print(f'oriented: {len(tris)} triangles -> strip of {len(strip)} vertices ({len(strip) / len(tris):.2f} per triangle)')
    ordered = [tuple(sorted(t)) for t in tris if len(set(t)) == 3]
    strip = join(in_order(tris))
    assert [k for k in (tuple(sorted(strip[i:i + 3])) for i in range(len(strip) - 2) if len(set(strip[i:i + 3])) == 3)] == ordered
