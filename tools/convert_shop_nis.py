#!/usr/bin/env python3
"""The shops' scenes (NIS\\Scene_ShopEnter1 and Scene_ShopExit1) -> shopnis.n2sh:
the player's car driving in through the shop's roll-up door from the street,
and out of it back to the street, with the scenes' own camera.

What the game does with them (the GameCube's executable, read for the rules,
none of it copied):
- which scene: GLOBAL\\INGAMECOMMON.BUN 0x37070 lists two shop candidates
  (8-byte records from +0x35c: scene hash, camera track, flags with 0x40,
  and 1 for entering, 2 for leaving); they play only in the city (track
  4000), and not the career's very first time (its own arrival scene);
- where: the shop's record (GLOBALB 0x34A12, 0xa0 bytes) names at +0x38 the
  track position marker the scene stands on (TRACKS\\ROUTESL4RA\\
  TRACKPOSMARKERSALL.BIN 0x34146, 48-byte markers: name hash at +8, the
  place at +16, a binary angle at +32); the scene's frame is turned a
  quarter turn back from the marker's angle;
- the door: the record's +0x3c is the shop's trigger, which the world
  animation instance of its roll-up door lists among its two event hashes
  (L4RA 0x37090 +0x20; its id at +0, worldanim.n2wa's tree id); the scene's
  WORLD_MESSAGE DOOR_POSITION events (position 0 closed to 1 open, over so
  many frames at 30 a second; none: at once) move it, as the game's
  ShopDoorControlScenario;
- the car: ZPM_Car1 (_t, _q) in the bundle's EAGL bank, the car's root in
  the scene's frame (15 frames a second); the camera: the scene's group in
  GLOBAL\\INGAMEFREEROAM.BUN 0x8003b201 (one track each).

shopnis.n2sh (little endian): 'N2SH', u32 1, then
  u32 scenes (2: entering, leaving), each a scene_block
    (convert_ending_nis.py, psp-viewer/nisscene.h) with its n2ni's bytes 0
    (no people); events of type 12 the door (sub 1, a its position, b the
    frames to get there);
  u32 n, n x shop (char name[24], f32 game x, y, z of its marker, u16 the
    marker's angle, u16 pad, u32 its door's instance id, 0 none).
"""
import argparse
import struct
import sys
from pathlib import Path

from n2formats import archived, chunks, unpad
import convert_career as cc
import convert_ending_nis as ce
import convert_nis as nis

ROOT = Path(__file__).resolve().parents[1]
SCENES = ('ShopEnter1', 'ShopExit1')
DOOR_EVENT = 12   # NIS_EV_DOOR (nisscene.h): the game's WORLD_MESSAGE, DOOR_POSITION


def camera_groups(game):
    d = archived(game, 'GLOBAL/INGAMEFREEROAM.BUN')
    for t, o, s in chunks(d):
        if t != 0x8003b201:
            continue
        groups, p = {}, o
        while p < o + s:
            _, size = struct.unpack_from('<II', d, p)
            h, n = struct.unpack_from('<II', d, p + 8)
            q, tracks = p + 16, []
            for _ in range(n):
                nk = struct.unpack_from('<b', d, q + 0x10)[0]
                tracks.append([d[q + 0x20 + 0x88 * j:q + 0x20 + 0x88 * (j + 1)] for j in range(nk)])
                q += 0x20 + 0x88 * nk
            groups[h] = tracks
            p += 8 + size
        return groups
    raise ValueError('no camera set in INGAMEFREEROAM.BUN')


def scene_events(text, name):
    """The track's events: the door's moves (WORLD_MESSAGE DOOR_POSITION
    position frames) here, the cars' (the engine's sound pattern) by the
    ending's reader."""
    door, rest = [], []
    for line in text.split('\n'):
        f = line.split('//')[0].split()
        if len(f) >= 5 and f[0] == 'EVENT:' and f[2] == 'WORLD_MESSAGE' and f[3] == 'DOOR_POSITION':
            num = lambda v: float(v.rstrip('f'))
            door.append((num(f[1]) / 30., DOOR_EVENT, 0, 1, num(f[4]), num(f[5])))
        else:
            rest.append(line)
    return sorted(door + ce.track_events('\n'.join(rest), name))


def shops(game):
    """[(name, marker x, y, z, angle, door id)] in the 0x34A12 records' order."""
    sec = cc.sections(archived(game, 'GLOBAL/GLOBALB.BUN'))[0x34a12]
    marks = {}
    mk = archived(game, 'TRACKS/ROUTESL4RA/TRACKPOSMARKERSALL.BIN')
    for t, o, s in chunks(mk):
        if t != 0x34146:
            continue
        p = (o + 15) & ~15
        for i in range((o + s - p) // 48):
            r = mk[p + 48 * i:p + 48 * i + 48]
            marks[struct.unpack_from('<I', r, 8)[0]] = (*struct.unpack_from('<3f', r, 16), struct.unpack_from('<H', r, 32)[0])
    doors = {}   # a door's event hashes (the shop's trigger) -> its instance id
    l4ra = archived(game, 'TRACKS/L4RA.BUN')
    for t, p, n in chunks(l4ra):
        if t == 0x37090:
            u = unpad(l4ra[p:p + n])   # convert_worldanim.level_trees' reading
            if len(u) >= 0x28:
                for h in struct.unpack_from('<2I', u, 0x20):
                    if h:
                        doors[h] = struct.unpack_from('<I', u)[0]
    out = []
    for k in range(len(sec) // 0xa0):
        r = sec[0xa0 * k:0xa0 * k + 0xa0]
        name = r[:32].split(b'\0')[0].decode()
        mark, trigger = struct.unpack_from('<2I', r, 0x38)
        if mark in marks:
            out.append((name, *marks[mark], doors.get(trigger, 0)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', type=Path, default=ROOT / 'game/xbox/NFSUNDER')
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    args = ap.parse_args()
    groups = camera_groups(args.game)
    blob = bytearray(struct.pack('<4sI', b'N2SH', 1))
    blob += struct.pack('<I', len(SCENES))
    for name in SCENES:
        data = archived(args.game, f'NIS/Scene_{name}_BundleB.bun')
        cars = ce.car_paths(data)
        cams = [ce.camera_track(t) for t in groups[nis.h33(name)]]
        evs = scene_events(archived(args.game, f'NIS/Scene_{name}_EvtTrk.txt').decode('latin-1'), name)
        blob += ce.scene_block(0, data, cars, cams, evs)
        print(dict(scene=name, times=[round(v, 2) for v in ce.scene_times(data)], cars=len(cars),
                   car_frames=[len(c) for c in cars], cameras=len(cams), events=evs))
    rows = shops(args.game)
    blob += struct.pack('<I', len(rows))
    for name, x, y, z, ang, door in rows:
        blob += struct.pack('<24s3fH2xI', name.encode()[:23], x, y, z, ang, door)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'shopnis.n2sh').write_bytes(bytes(blob))
    print(dict(shops=len(rows), with_door=sum(1 for r in rows if r[-1]), bytes=len(blob)))


if __name__ == '__main__':
    sys.exit(main())
