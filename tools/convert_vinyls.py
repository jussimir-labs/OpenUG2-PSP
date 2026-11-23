#!/usr/bin/env python3
"""The vinyl shop's whole catalogue, one pack per player car, read by the
PSP a vinyl at a time as the shop browses it.

The catalogue is the game's parts table (tools/partsdb.py): the vinyls
are part type 50, each with its group in the low 5 bits of its level byte,
as the shop's carousel numbers them (the game's table at 0x142500 of
default.xbe: Tear 0, Tear Set 1, Stripes 2... Contest 27; 28 the hidden
ones, left out). Its attributes say whether it takes the player's colours
(0x39dd714: 1; 0 keeps the art's own: sponsors' ads, contest winners, the
unique and art factory pieces, country flags) and how many
(NUMREMAPCOLOURS); its name in the language table is keyed by its own
name's hash ("CANADA", "SKATER"; most have none).

The art is per car (CARS/<CAR>/VINYLS.BIN: <CAR>_<NAME> and its _MASK,
512 x 512), brought to 256 x 256 as tools/convert_garage.py's N2SK layers:
a recoloured vinyl as weights of its three colours (r, g, b summing to 255)
and coverage, any colours a 256-entry palette away; one with its own
colours as RGBA. Each is deflated (zlib).

vinyls-<MODEL>.n2vc: 'N2VC', u32 version 1, u32 count, then count x
(char name[24], char title[24] (its name in the language table, '' for
most), u8 group, u8 colours (0 its own, 1-3 the player's it takes), u16 0,
u32 offset, u32 deflated bytes), then the data: per vinyl, deflated, a
1024-byte RGBA palette and 256 x 256 indices.
Runs on the venv's Python (numpy, PIL), the cars in parallel.
"""
import argparse
import concurrent.futures
import functools
import os
from pathlib import Path
import struct
import zlib

import numpy as np
from PIL import Image

import convert_car as cc
import convert_hud
import convert_physics
import mac_slots
import n2feng as fe
from n2formats import archived
import partsdb
import n2source

ROOT = Path(__file__).resolve().parents[1]
VINYL = 50                     # the parts table's type of a vinyl
HIDDEN = 28                    # the group the shop never shows (celebrities' ads, the debug ones)
RECOLOUR = 0x39dd714           # the attribute: 1 takes the player's colours, 0 keeps its own
SIZE = 256


def title(text):
    """'ONE COLOR' -> 'One Color'."""
    return ' '.join(w[:1] + w[1:].lower() for w in text.split())


def catalogue(game):
    """[(name, group, colours (0 its own, 1-3 the player's), title)] in the
    parts table's order."""
    parts, text = partsdb.Parts(game), convert_hud.strings(game)
    out = []
    for q in parts.of_kind(VINYL):
        if not q['name'] or q['level'] & 31 == HIDDEN:
            continue
        a = parts.attributes(q)
        colours = min(3, max(1, a.get(fe.bin_hash('NUMREMAPCOLOURS'), 1))) if a.get(RECOLOUR, 1) else 0
        out.append((q['name'], q['level'] & 31, colours, title(text[q['key']]) if q['key'] in text else ''))
    return out


def half(a):
    """512 -> 256 by 2 x 2 boxes."""
    h, w = a.shape[:2]
    return a.reshape(h // 2, 2, w // 2, 2, -1).mean(axis=(1, 3))


def layer(rgba):
    """(palette bytes, indices) of a 256 x 256 RGBA array."""
    im = Image.fromarray(rgba.astype(np.uint8), 'RGBA')
    q = im.quantize(256, method=Image.Quantize.FASTOCTREE)
    pal = q.getpalette('RGBA')[:1024]
    return bytes(pal) + bytes(1024 - len(pal)), q.tobytes()


def vinyl(design, mask, colours):
    """(palette, indices) of one vinyl: weights of its colours, or its own."""
    rgb = design[..., :3].astype(np.float32)
    a = mask[..., 0].astype(np.float32)
    if colours:
        s = rgb.sum(axis=2)
        on = (s > 0) & (a > 0)
        w = np.zeros(rgb.shape, np.float32)
        w[on] = rgb[on] / s[on][:, None]
        w[..., 2] = np.where(on, 1 - w[..., 0] - w[..., 1], 0)
        out = np.dstack([w * 255, np.where(on, a, 0)])
    else:
        out = np.dstack([rgb, a])
    out = half(out) if out.shape[0] == 2 * SIZE else out
    out = np.rint(out)
    if colours:   # weights summing to 255 again after the averaging
        out[..., 2] = np.clip(255 - out[..., 0] - out[..., 1], 0, 255)
    return layer(out)


def convert_car(game, out, car, items):
    if hasattr(os, 'nice'):   # POSIX only; Windows keeps the default priority
        os.nice(15)
    store, index = cc.vinyl_index(game, car)
    rows, blobs, at = [], [], 0
    for name, group, colours, label in items:
        key, mkey = index.get(f'{car}_{name}'), index.get(cc.mask_name(f'{car}_{name}'))
        if key is None or mkey is None:
            continue
        t, m = store.get(key), store.get(mkey)
        if (t.w, t.h) != (2 * SIZE, 2 * SIZE) or (m.w, m.h) != (t.w, t.h):
            continue
        design = np.frombuffer(bytes(t.rgba()), np.uint8).reshape(t.h, t.w, 4)
        mask = np.frombuffer(bytes(m.rgba()), np.uint8).reshape(m.h, m.w, 4)
        pal, idx = vinyl(design, mask, colours)
        blob = zlib.compress(pal + idx, 9)
        rows.append((name, label, group, colours, at, len(blob)))
        blobs.append(blob)
        at += len(blob)
    head = struct.pack('<4s2I', b'N2VC', 1, len(rows))
    base = len(head) + 60 * len(rows)
    table = b''.join(struct.pack('<24s24s2BH2I', n.encode()[:23], t.encode()[:23], g, c, 0, base + o, size)
                     for n, t, g, c, o, size in rows)
    (out / f'vinyls-{car}.n2vc').write_bytes(head + table + b''.join(blobs))
    return car, len(rows), base + at


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--car', action='append', help='only these cars (default: the fleet, as tools/convert_garage.py)')
    ap.add_argument('--jobs', type=int, help='maximum vinyl conversion workers')
    args = ap.parse_args()
    items = catalogue(args.game)
    cars = args.car or convert_physics.playable(convert_physics.records(archived(args.game, 'GLOBAL/GLOBALB.BUN')))
    args.out.mkdir(parents=True, exist_ok=True)
    total = 0
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        # one core per car from the Mac's queue (tools/mac_slots.py)
        for car, n, size in pool.map(functools.partial(mac_slots.call, 'convert_vinyls', convert_car), *zip(*[(args.game, args.out, c, items) for c in cars])):
            print(f'{car}: {n} vinis, {size / 1e6:.1f} MB')
            total += size
    print(f'{len(items)} vinyls in the catalog, {len(cars)} cars, {total / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
