#!/usr/bin/env python3
"""An EBOOT.PBP's sections, and its XMB art taken out or put in.

A PBP is "\\0PBP", its version and the offsets of its eight sections, in
this order: PARAM.SFO, ICON0.PNG, ICON1.PMF, PIC0.PNG, PIC1.PNG, SND0.AT3,
DATA.PSP (the game's PRX) and DATA.PSAR; each runs to the next one's
offset (pack-pbp writes them one after another, with no padding).

The beta's kit carries its EBOOT without the game's art (Jussimir, 12/11):
no ICON0, PIC1 or any other XMB picture or sound, only PARAM.SFO and
DATA.PSP. The generator puts back the icon and the background that
tools/convert_hud.py's eboot_art makes from the player's own disc
(tools/ms_package.py make --xmb-art); PARAM.SFO and DATA.PSP stay byte
for byte.

  tools/pbp.py info EBOOT              the sections' sizes and hashes
  tools/pbp.py strip EBOOT OUT         OUT: EBOOT without the XMB art
  tools/pbp.py art EBOOT OUT --icon0 PNG --pic1 PNG
                                       OUT: EBOOT with this art (the rest of
                                       its art taken out)
  tools/pbp.py check-kit EBOOT         exit 1 when EBOOT carries XMB art
  tools/pbp.py check-card GAME KIT     a generated PSP/GAME/NFSU2VIEW's EBOOT:
                                       the kit's EBOOT (KIT) with GAME's own
                                       icon0.png and pic1.png (loose or in
                                       pack.n2pk) as ICON0 and PIC1; exit 1
                                       on any difference
Python stdlib only (the beta's generator runs it).
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

MAGIC = b'\0PBP'
NAMES = ('PARAM.SFO', 'ICON0.PNG', 'ICON1.PMF', 'PIC0.PNG', 'PIC1.PNG', 'SND0.AT3', 'DATA.PSP', 'DATA.PSAR')
# the XMB's pictures, animation and sound: the game's art when a build carries them
ART = ('ICON0.PNG', 'ICON1.PMF', 'PIC0.PNG', 'PIC1.PNG', 'SND0.AT3')
HEADER = 8 + 4 * len(NAMES)
PNG = b'\x89PNG\r\n\x1a\n'
# The kit's own XMB, from 0.4.0 (Jussimir, 15/11): the icon video, background and theme
# made by BananaSplitGamer (beta issue #205). These exact sections, and nothing else, may be
# in the kit's EBOOT; the generator then keeps them, and the card's EBOOT is the kit's for
# every source (beta/native/release_policy.json holds the same hashes for the release check).
KIT_XMB = {
    'ICON0.PNG': '9ef3871cdd774a7e6d24160033ed70c1ca32f9bfaa357119322840b5566e5f1a',
    'ICON1.PMF': 'c8d4b2b0c9990c00eb6f5805484a49de21b4570ba9fbdfda730047556d36b1c9',
    'PIC1.PNG': 'cb6d6bc57d2442575231dbf246d1412911c1fd3011ac8d74c91180bac52867a9',
    'SND0.AT3': 'cb995ed7e8a8bffeb7c9dbb2d10b31e6460ec2c299e4a82234fa410a00442e7c',
}


def sections(data):
    """{name: bytes} of a PBP's eight sections; ValueError if it is not one."""
    if len(data) < HEADER or data[:4] != MAGIC:
        raise ValueError('not an EBOOT.PBP')
    offsets = list(struct.unpack_from('<8I', data, 8)) + [len(data)]
    if offsets[0] < HEADER or any(b < a for a, b in zip(offsets, offsets[1:])):
        raise ValueError('EBOOT.PBP section offsets out of order')
    return {name: data[offsets[i]:offsets[i + 1]] for i, name in enumerate(NAMES)}


def build(parts, version=0x10000):
    """A PBP from {name: bytes} (a missing section: empty)."""
    out, at = [], HEADER
    for name in NAMES:
        out.append(at)
        at += len(parts.get(name, b''))
    return MAGIC + struct.pack('<I8I', version, *out) + b''.join(parts.get(name, b'') for name in NAMES)


def art_of(data):
    """The XMB art sections that are not empty: [(name, size)]."""
    parts = sections(data)
    return [(name, len(parts[name])) for name in ART if parts[name]]


def kit_xmb(data):
    """True when data's XMB art is exactly the reviewed kit XMB (KIT_XMB), section by section."""
    parts = sections(data)
    have = {name: hashlib.sha256(parts[name]).hexdigest() for name in ART if parts[name]}
    return have == KIT_XMB


def with_xmb(data, files):
    """data with the kit XMB {section: bytes} (KIT_XMB's sections, their exact hashes) and no other
    art; the PRX and PARAM.SFO as they were."""
    if {n: hashlib.sha256(v).hexdigest() for n, v in files.items()} != KIT_XMB:
        raise ValueError('not the reviewed kit XMB (KIT_XMB)')
    parts = sections(data)
    parts.update({name: b'' for name in ART})
    parts.update(files)
    out = build(parts, struct.unpack_from('<I', data, 4)[0])
    new = sections(out)
    assert all(new[n] == parts[n] for n in ('PARAM.SFO', 'DATA.PSP', 'DATA.PSAR'))
    return out


def strip(data):
    """data without its XMB art: PARAM.SFO and DATA.PSP (and DATA.PSAR) as they were."""
    parts = sections(data)
    return build({name: b'' if name in ART else value for name, value in parts.items()},
                 struct.unpack_from('<I', data, 4)[0])


def png_size(png):
    """(width, height) of a PNG; ValueError for anything else."""
    if png[:8] != PNG or png[12:16] != b'IHDR':
        raise ValueError('not a PNG')
    return struct.unpack_from('>2I', png, 16)


def with_art(data, icon0, pic1):
    """data with this icon (144 x 80) and background (480 x 272) for the
    XMB, and no other art; the PRX and PARAM.SFO as they were."""
    if png_size(icon0) != (144, 80):
        raise ValueError(f'ICON0.PNG must be 144 x 80, not {png_size(icon0)}')
    if png_size(pic1) != (480, 272):
        raise ValueError(f'PIC1.PNG must be 480 x 272, not {png_size(pic1)}')
    parts = sections(data)
    parts.update({name: b'' for name in ART})
    parts.update({'ICON0.PNG': icon0, 'PIC1.PNG': pic1})
    out = build(parts, struct.unpack_from('<I', data, 4)[0])
    new = sections(out)
    assert all(new[n] == parts[n] for n in ('PARAM.SFO', 'DATA.PSP', 'DATA.PSAR'))
    return out


def pack_entry(game, name):
    """A file of a generated folder: loose, or out of its pack.n2pk
    (tools/ms_package.py write_pack); None when neither has it."""
    game = Path(game)
    if (game / name).is_file():
        return (game / name).read_bytes()
    pack = game / 'pack.n2pk'
    if not pack.is_file():
        return None
    data = pack.read_bytes()
    magic, _, count, _ = struct.unpack_from('<4sIII', data, 0)
    if magic != b'N2PK':
        raise ValueError(f'{pack} is not a pack.n2pk')
    at = 16
    for _ in range(count):
        off, length, size = struct.unpack_from('<IIH', data, at)
        if data[at + 10:at + 10 + size].decode() == name:
            return data[off:off + length]
        at += 10 + size + (-(10 + size) % 4)
    return None


def card_problems(game, kit):
    """What differs between a generated folder's EBOOT and the kit's EBOOT
    with the folder's own art: [] when it is exactly that."""
    card_data, kit_data = (Path(game) / 'EBOOT.PBP').read_bytes(), Path(kit).read_bytes()
    card, kit_parts = sections(card_data), sections(kit_data)
    if kit_xmb(kit_data):   # the kit carries its own XMB: the card's EBOOT is the kit's, byte for byte
        return [f'{n} differs from the kit\'s' for n in NAMES if card[n] != kit_parts[n]]
    found = [f'{n} differs from the kit\'s' for n in ('PARAM.SFO', 'DATA.PSP', 'DATA.PSAR') if card[n] != kit_parts[n]]
    for section, name in (('ICON0.PNG', 'icon0.png'), ('PIC1.PNG', 'pic1.png')):
        own = pack_entry(game, name)
        if own is None:
            found.append(f'{name} missing in the folder')
        elif card[section] != own:
            found.append(f'{section} is not the folder\'s {name}')
    found += [f'{n} is not empty' for n in ART if n not in ('ICON0.PNG', 'PIC1.PNG') and card[n]]
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('info')
    p.add_argument('eboot', type=Path)
    p = sub.add_parser('strip')
    p.add_argument('eboot', type=Path)
    p.add_argument('out', type=Path)
    p = sub.add_parser('art')
    p.add_argument('eboot', type=Path)
    p.add_argument('out', type=Path)
    p.add_argument('--icon0', type=Path, required=True)
    p.add_argument('--pic1', type=Path, required=True)
    p = sub.add_parser('xmb')
    p.add_argument('eboot', type=Path)
    p.add_argument('out', type=Path)
    p.add_argument('--icon0', type=Path, required=True)
    p.add_argument('--icon1', type=Path, required=True)
    p.add_argument('--pic1', type=Path, required=True)
    p.add_argument('--snd0', type=Path, required=True)
    p = sub.add_parser('check-kit')
    p.add_argument('eboot', type=Path)
    p = sub.add_parser('check-card')
    p.add_argument('game', type=Path)
    p.add_argument('kit', type=Path)
    a = ap.parse_args()
    if a.cmd == 'check-card':
        found = card_problems(a.game, a.kit)
        for line in found:
            print('  - ' + line)
        if found:
            sys.exit(f'{a.game / "EBOOT.PBP"}: not the kit\'s EBOOT with the folder\'s art')
        card = sections((a.game / 'EBOOT.PBP').read_bytes())
        if kit_xmb(a.kit.read_bytes()):
            print(f'{a.game / "EBOOT.PBP"}: the kit\'s EBOOT, byte for byte, with its own XMB')
            return
        print(f'{a.game / "EBOOT.PBP"}: the kit\'s PRX and PARAM.SFO, ICON0 {len(card["ICON0.PNG"])} and '
              f'PIC1 {len(card["PIC1.PNG"])} bytes from the folder\'s icon0.png and pic1.png')
        return
    data = a.eboot.read_bytes()
    if a.cmd == 'info':
        for name, value in sections(data).items():
            print(f'{name:10} {len(value):9} {hashlib.sha256(value).hexdigest()[:16] if value else "-"}')
        print(f'{"EBOOT.PBP":10} {len(data):9} {hashlib.sha256(data).hexdigest()[:16]}')
    elif a.cmd == 'strip':
        a.out.write_bytes(strip(data))
        print(f'{a.out}: {len(strip(data))} bytes, no XMB art ({", ".join(n for n, _ in art_of(data)) or "it had none"} taken out)')
    elif a.cmd == 'art':
        out = with_art(data, a.icon0.read_bytes(), a.pic1.read_bytes())
        a.out.write_bytes(out)
        print(f'{a.out}: {len(out)} bytes, ICON0.PNG and PIC1.PNG from {a.icon0.parent}')
    elif a.cmd == 'xmb':
        out = with_xmb(data, {'ICON0.PNG': a.icon0.read_bytes(), 'ICON1.PMF': a.icon1.read_bytes(),
                              'PIC1.PNG': a.pic1.read_bytes(), 'SND0.AT3': a.snd0.read_bytes()})
        a.out.write_bytes(out)
        print(f'{a.out}: {len(out)} bytes, with the kit XMB (ICON0, ICON1, PIC1, SND0)')
    else:
        if kit_xmb(data):
            print(f'{a.eboot}: the reviewed kit XMB (ICON0, ICON1, PIC1, SND0) and no other art')
            return
        found = art_of(data)
        if found:
            sys.exit(f'{a.eboot}: carries XMB art ({", ".join(f"{n} {s} bytes" for n, s in found)}): '
                     f'the kit\'s EBOOT must have none (tools/pbp.py strip)')
        print(f'{a.eboot}: no XMB art')


if __name__ == '__main__':
    main()
