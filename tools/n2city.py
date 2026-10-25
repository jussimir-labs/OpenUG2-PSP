"""Read-only access to the city archive's collision grid (city.n2cy)."""
from pathlib import Path
import struct

import n2cy_patch

DROP = 1.2   # m: steps and drops past this stop a car (vehicle.c)


class CityCollision:
    """Ground layers per 1 m cell, in game axes (X, Y; heights Z in m)."""

    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(64)
        if h[:4] != b'N2CY':
            raise ValueError(f'{path}: not a city archive')
        _, _, self.cell, self.i0, self.j0, self.w, self.h, _, _, _, _, _, index_off, _ = struct.unpack_from('<4sIf2i3I4f2I', h)
        self.f.seek(index_off)
        raw = self.f.read(self.w * self.h * 24)
        self.index = [struct.unpack_from('<6I', raw, 24 * k) for k in range(self.w * self.h)]
        self.cache = {}

    def with_overlay(self, path):
        """This archive with an event's barriers (bar-<id>.n2cy) over it:
        the overlay's cells stand in for the archive's."""
        out = CityCollision.__new__(CityCollision)
        out.__dict__.update(self.__dict__)
        out.over = CityCollision(path) if Path(path).exists() else None
        return out

    def _raw(self, ci, cj):
        """The cell's collision blob as stored (b'' none)."""
        i, j = ci - self.i0, cj - self.j0
        if 0 <= i < self.w and 0 <= j < self.h:
            off, size = self.index[j * self.w + i][4:6]
            if size:
                self.f.seek(off)
                return self.f.read(size)
        return b''

    def _cell(self, ci, cj):
        over = getattr(self, 'over', None)
        if over is not None:
            if (ci, cj) not in over.cache:
                raw = over._raw(ci, cj)
                if raw[:4] == n2cy_patch.MAGIC:   # a barrier's patch on this archive's cell (tools/n2cy_patch.py)
                    raw = n2cy_patch.apply_patch(self._raw(ci, cj), raw)
                over.cache[(ci, cj)] = over._grid(raw)
            g = over.cache[(ci, cj)]
            if g:
                return g
        if (ci, cj) not in self.cache:
            self.cache[(ci, cj)] = self._grid(self._raw(ci, cj))
        return self.cache[(ci, cj)]

    def _grid(self, b):
        """A collision blob's layers: low, high, wall bits, middle, second middle."""
        if not b:
            return None
        n = int(self.cell)
        base, unit = struct.unpack_from('<2f', b, 0)
        h = lambda o: tuple(None if v == -32768 else base + v * unit for v in struct.unpack_from(f'<{n*n}h', b, 16 + o))
        none = (None,) * (n * n)
        return (h(0), h(2 * n * n), b[16 + 4 * n * n:16 + 5 * n * n], h(5 * n * n),
                h(9 * n * n) if len(b) >= 16 + 11 * n * n else none)

    def layers(self, gx, gy):
        """Distinct ground heights (m) at a point, lowest first."""
        ci, cj = int(gx // self.cell), int(gy // self.cell)
        grid = self._cell(ci, cj)
        if not grid:
            return []
        n = int(self.cell)
        k = int(gy - cj * self.cell) * n + int(gx - ci * self.cell)
        out = []
        for v in (grid[0][k], grid[3][k], grid[4][k], grid[1][k]):
            if v is not None and all(abs(v - o) > 1 for o in out):
                out.append(v)
        return sorted(out)

    def blocked(self, gx, gy, z):
        """True when the layer nearest height z has a wall at this cell, or
        when no ground is within 3 m of z (a ledge or a void)."""
        ci, cj = int(gx // self.cell), int(gy // self.cell)
        grid = self._cell(ci, cj)
        if not grid:
            return False
        n = int(self.cell)
        k = int(gy - cj * self.cell) * n + int(gx - ci * self.cell)
        if grid[1][k] is None:
            return True
        layers = [(abs(v - z), bit) for v, bit in ((grid[0][k], 1), (grid[1][k], 2), (grid[3][k], 4), (grid[4][k], 8))
                  if v is not None]
        gap, bit = min(layers)
        return gap > 3 or bool(grid[2][k] & bit)

    def crosses_wall(self, ax, ay, az, bx, by, bz):
        """True when a segment's centre line (game coordinates, every 0.5 m)
        runs through a thin wall across it: up to 3 m of walled cells with
        free ground before and after them along the segment and wall beside
        them. A car-wide corridor reads it as a metre or two blocked, inside
        the edges' tolerance (the 4083's course went through two single
        walls in a row, the 4084's through a double one). A wall the segment
        runs along walls it for longer, or to its end."""
        d = ((bx - ax) ** 2 + (by - ay) ** 2) ** .5
        if d < 1e-6:
            return False
        ux, uy = (bx - ax) / d, (by - ay) / d
        n = max(1, int(2 * d))
        # (a course point can sit in the wall: the crossing then spans two
        # segments, one ending in it and the next starting in it)
        run, beside = 0, False
        for k in range(n + 1):
            t = k / n
            x, y, z = ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t
            if self._walled(x, y, z):
                run += 1
                beside = beside or self._walled(x - uy, y + ux, z) or self._walled(x + uy, y - ux, z)
                continue
            if run and run <= 6 and beside:
                return True
            run, beside = 0, False
        return bool(run) and run <= 6 and beside

    def crosses_step(self, ax, ay, az, bx, by, bz):
        """True when the ground under a segment's centre line (every metre,
        the layer nearest the height running from end to end) jumps more
        than DROP between samples: a step or a drop that stops a car. The
        course of the 4043 climbed onto a terrain block and dropped 1.9 m
        off its far side back to the road; the nearest layer alone, a metre
        from the running height, read as free."""
        n = max(1, int(((bx - ax) ** 2 + (by - ay) ** 2) ** .5))
        last = None
        for k in range(n + 1):
            t = k / n
            x, y, z = ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t
            here = self.layers(x, y)
            h = min(here, key=lambda v: abs(v - z)) if here else None
            if h is not None and last is not None and abs(h - last) > DROP:
                return True
            last = h
        return False

    def _walled(self, gx, gy, z):
        """A wall bit on the layer nearest z (not a void or a step)."""
        ci, cj = int(gx // self.cell), int(gy // self.cell)
        grid = self._cell(ci, cj)
        if not grid:
            return False
        n = int(self.cell)
        k = int(gy - cj * self.cell) * n + int(gx - ci * self.cell)
        layers = [(abs(v - z), bit) for v, bit in ((grid[0][k], 1), (grid[1][k], 2), (grid[3][k], 4), (grid[4][k], 8))
                  if v is not None]
        if not layers:
            return False
        gap, bit = min(layers)
        return gap <= 3 and bool(grid[2][k] & bit)

    def corridor(self, ax, ay, az, bx, by, bz):
        """Blocked metres along a segment (game coordinates) for a car-wide
        corridor: the centre line and 1 m to each side, sampled every metre."""
        n = max(1, int(((bx - ax) ** 2 + (by - ay) ** 2) ** .5))
        sx, sy = -(by - ay) / n, (bx - ax) / n
        hits = []
        for k in range(1, n + 1):
            t = k / n
            x, y, z = ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t
            hits.append(any(self.blocked(x + sx * o, y + sy * o, z) for o in (0, -1, 1)))
        return hits

    def crosses_barrier(self, ax, ay, az, bx, by, bz):
        """True when a segment's centre line (game coordinates, sampled
        every 0.5 m) runs through the event's barriers: an overlay cell
        held solid where the city alone is free. A barrier is one cell
        wide, so a car-wide corridor crossing it on the diagonal reads as
        only a metre or two blocked, the grid's noise."""
        over = getattr(self, 'over', None)
        if over is None:
            return False
        base = CityCollision.__new__(CityCollision)
        base.__dict__.update(self.__dict__)
        base.over = None
        n = max(1, int(2 * ((bx - ax) ** 2 + (by - ay) ** 2) ** .5))
        for k in range(n + 1):
            t = k / n
            x, y, z = ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t
            if over._cell(int(x // over.cell), int(y // over.cell)) and over.blocked(x, y, z) and not base.blocked(x, y, z):
                return True
        return False

