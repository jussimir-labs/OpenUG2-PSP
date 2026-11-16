#!/usr/bin/env python3
"""The game for the Memory Stick (a microSD in an adapter), and its runs from it.

  tools/ms_package.py make OUT [--dist DIR] [--eboot EBOOT.PBP] [--runs bench,4101,outer,4101-32]
      OUT/PSP/GAME/NFSU2VIEW: the dist's files the game reads (no test
      outputs, no converters' .json, no legacy opt-*.n2pv or vinyl-*.n2sk
      from before the packs),
      the EBOOT, and ms-runs.txt: each start from the XMB runs its next line
      (the card's speed, the 4101 sprint, the Outer Ring, the sprint again
      with the city read in PSPLink's pieces of 32 KB: from the card the
      game reads 512) and goes back to
      the XMB; with all done the game boots as ever. Files are hard links
      where the disk allows (the copy to the card is the user's); the
      barriers (bar-*.n2cy) are written anew with their collision as
      patches on the world's (tools/n2cy_patch.py, ~170 MB less), unless
      --whole-bars (an EBOOT from before N2CY v3 refuses the patched ones).
      --pack: the files under 1 MB in one, pack.n2pk, read through one
      handle (an EBOOT that reads it: psp-viewer/main.c pack_load).
      --release: the players' package (an EBOOT of make RELEASE=1): no
      ms-runs.txt (no measurement runs), none of the dist's tools and test
      files (PSPLink's PRX, the ARK's notes, the tests' control files), the
      PARAM.SFO's MEMSIZE=1 checked, and a README for the player.
      --xmb-art: the EBOOT's XMB icon and background put in from the dist's
      icon0.png and pic1.png (tools/convert_hud.py eboot_art, from the
      player's disc): the beta's kit carries its EBOOT without the game's
      art and its generator packages with this (tools/pbp.py); PARAM.SFO
      and the PRX stay byte for byte.
  tools/ms_package.py push VOLUME OUT [--fresh]
      the package onto the card the XMB's USB connection mounts (VOLUME,
      e.g. /Volumes/PSP): the one way to copy it. rsync with no extended
      attributes and no macOS ._ files, the saves kept, dot_clean, and an
      error if a ._ or .DS_Store is left in the game's folder. --fresh: a
      new folder (a FAT folder keeps its deleted files' entries: every open
      walks past them), the saves moved over, the old one deleted.
  tools/ms_package.py put VOLUME FILE... [--backup TAG]
      single files into the card's game folder (an EBOOT swapped by hand):
      cp -X with COPYFILE_DISABLE=1 (no ._ copies), dot_clean, the same error
      if a ._ or .DS_Store is left. NAME=PATH puts PATH under NAME. --backup
      TAG: a file already there renamed first, EBOOT.PBP -> EBOOT-TAG.PBP.bak.
  tools/ms_package.py clean VOLUME
      the macOS files (._, .DS_Store) of a copy made some other way out of
      the card's game folder, and nothing else.
  tools/ms_package.py collect CARD_DIR TAG
      the runs' files from the card's NFSU2VIEW (or a copy of it) into
      reports/psp-runs/ms-TAG-<run>/, under the names the tools read
      (frame-log.bin, viewer-stats.json, boot.txt), and their summary.
  tools/ms_package.py summary DIR...   the summary of runs already collected
      (a host0: run of tools/psp_run.py too, for the comparison).
  tools/ms_package.py bench FILE       a card-bench.txt's table

The game's side: psp-viewer/main.c (ms_run_next, card_bench, the frame
log's reads and holes in view).
"""
import argparse
import json
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import n2cy_patch  # noqa: E402  (the barriers' collision as patches)
import ppsspp_run  # noqa: E402  (DIST, game_output)
# psp_run (records, summary, io_summary) is imported where the runs need
# it: make and push go without it and its tools, as in the beta's repository

# tag: the smoke line (10 numbers: frames lod script yaw pitch debug mode race burst hold)
RUNS = {
    'bench': '1 0 97 -1 -1 0 -1 0 0 1',
    '4101': '9000 0 4 0.95 0.18 0 1 31 0 1',      # tools/psp_run.py's SPRINT_4101, race 31 of races.json's kept
    'outer': '30000 0 4 0.95 0.18 0 1 0 0 1',     # the Outer Ring (4001), race 0
    '4101-512': '9000 0 4 0.95 0.18 0 1 31 0 1 512',   # the sprint, the city read in pieces of 512 KB (world.c's read_piece; from the card the default)
    '4101-32': '9000 0 4 0.95 0.18 0 1 31 0 1 32',     # the sprint with PSPLink's pieces of 32 KB, the comparison (11 frames with holes from the card in 10/2026)
}
# files a test or a session leaves in the dist that the players' game never
# reads (--release): the tests' controls (psp-viewer/main.c, lang.c), the
# ARK's and PSPLink's
TEST_CONTROLS = {'smoke-test.txt', 'track-smoke.txt', 'intro-force.txt', 'skipboot.txt', 'language.txt', 'repro.txt',
                 'repro-replay.txt', 'bug-at.txt', 'crash-test.txt', 'brake-test.txt', 'paint-tune.txt', 'street-tune.txt',
                 'probe.txt', 'io-trace.txt', 'ms-stall.txt', 'bench-dir.txt', 'hostcmd.txt', 'memory-admission.on'}
# the text the players' game reads (the rest of a release's .txt is a test's)
RELEASE_TEXT = {'credits.txt', 'data-id.txt', 'movies.txt', 'names.txt'}


def release_skip(name):
    """Not in the players' package: a tool's or test's file in the dist."""
    low = name.lower()
    return (name in TEST_CONTROLS or low.endswith(('.prx', '.bin', '.jsonl', '.log', '.elf'))   # (icon0.png stays: the saves' icon, savedata.c)
            or name.startswith('ark-') or (low.endswith('.txt') and name not in RELEASE_TEXT and not name.startswith('help')))


OUTPUTS = ('boot.txt', 'viewer-stats.json', 'frame-log.bin', 'viewer-frame.ppm', 'card-bench.txt')


def legacy(name):
    """Left by builds before the packs (tools/build.sh deletes them since):
    an option per file (now parts-<MODEL>.n2op; the decals' places stay)
    and a vinyl per file (now vinyls-<MODEL>.n2vc)."""
    return ((name.startswith('opt-') and name.endswith('.n2pv') and not name.endswith('-decal.n2pv'))
            or (name.startswith('vinyl-') and name.endswith('.n2sk')))


PACK_MAX = 1 << 20   # files under this go into pack.n2pk (--pack)


def packable(name, size):
    """In pack.n2pk: a file the game reads through sceIoOpen from its folder,
    under PACK_MAX. Not the EBOOT, the PRX the kernel loads, the saves and
    the tests' files (the game writes them), ms-runs.txt."""
    low = name.lower()
    return (size < PACK_MAX and name not in ('EBOOT.PBP', 'ms-runs.txt', 'pack.n2pk') and not low.endswith(('.prx', '.sav'))
            and not ppsspp_run.game_output(name) and len(name.encode()) < 200)


def write_pack(dest):
    """dest's small files in one, pack.n2pk (psp-viewer/main.c, pack_load):
    "N2PK", 1, the entries, the index's bytes; each entry its data's offset,
    size, the name's length and the name, to 4 bytes; then the data, each
    to 16 bytes. The loose files go: the card's folder ~800 names fewer,
    and an open of one walks no folder. Returns (files, bytes)."""
    import struct
    files = sorted(p for p in dest.iterdir() if p.is_file() and packable(p.name, p.stat().st_size))
    index = b''.join(struct.pack('<IIH', 0, 0, len(p.name.encode())) + p.name.encode() + b'\0' * (-(10 + len(p.name.encode())) % 4)
                     for p in files)
    at = (16 + len(index) + 15) & ~15
    entries, offsets = [], []
    for p in files:
        offsets.append(at)
        at = (at + p.stat().st_size + 15) & ~15
    index = b''.join(struct.pack('<IIH', o, p.stat().st_size, len(p.name.encode())) + p.name.encode()
                     + b'\0' * (-(10 + len(p.name.encode())) % 4) for o, p in zip(offsets, files))
    with open(dest / 'pack.n2pk', 'wb') as f:
        f.write(b'N2PK' + struct.pack('<III', 1, len(files), len(index)) + index)
        for o, p in zip(offsets, files):
            f.write(b'\0' * (o - f.tell()))
            f.write(p.read_bytes())
    total = sum(p.stat().st_size for p in files)
    for p in files:
        p.unlink()
    return len(files), total


def sfo_of(eboot):
    """The EBOOT's PARAM.SFO keys (int or text)."""
    import struct
    b = Path(eboot).read_bytes()
    if b[:4] != b'\0PBP':
        sys.exit(f'{eboot} is not an EBOOT.PBP')
    start, end = struct.unpack_from('<2I', b, 8)
    b = b[start:end]
    _, _, keys, data, n = struct.unpack_from('<4sIIII', b, 0)
    out = {}
    for i in range(n):
        ko, fmt, used, _, at = struct.unpack_from('<HHIII', b, 20 + 16 * i)
        raw = b[data + at:data + at + used]
        out[b[keys + ko:b.index(b'\0', keys + ko)].decode()] = (struct.unpack('<I', raw)[0] if fmt == 0x404
                                                                 else raw.rstrip(b'\0').decode(errors='replace'))
    return out


def pack_names(path):
    """The names in a pack.n2pk (write_pack's index); none without one."""
    import struct
    if not Path(path).is_file():
        return []
    with open(path, 'rb') as f:
        magic, _, n, size = struct.unpack('<4sIII', f.read(16))
        index = f.read(size)
    names, at = [], 0
    for _ in range(n):
        _, _, length = struct.unpack_from('<IIH', index, at)
        names.append(index[at + 10:at + 10 + length].decode())
        at += 10 + length + (-(10 + length) % 4)
    return names


def make(out, dist, eboot, runs, patch_bars=True, pack=False, release=False, xmb_art=False):
    dest = Path(out) / 'PSP/GAME/NFSU2VIEW'
    if dest.exists():
        sys.exit(f'{dest} already exists: choose another folder')
    sfo = sfo_of(eboot)
    if release and sfo.get('MEMSIZE') != 1:   # (MEMSIZE=0: the compact profile on the 64 MB models too)
        sys.exit(f'{eboot}: PARAM.SFO has no MEMSIZE=1 ({sfo})')
    dest.mkdir(parents=True)
    n = size = skipped = skipped_bytes = 0
    for p in sorted(Path(dist).iterdir()):
        if not p.is_file():
            continue
        # the converters' .json (tools read them, the game never does): 70 entries fewer in the card's FAT
        # (a name starting with a dot: macOS's ._ and .DS_Store, the game's hidden logs)
        if (p.name == 'EBOOT.PBP' or ppsspp_run.game_output(p.name) or legacy(p.name) or p.name.startswith(('ps2cmp-', '.'))
                or p.suffix == '.json' or (release and release_skip(p.name))):
            skipped += 1
            skipped_bytes += p.stat().st_size
            continue
        src = p.resolve()
        if patch_bars and p.name.startswith('bar-') and p.suffix == '.n2cy':
            # the barriers' collision as patches on the world's (an EBOOT that
            # reads N2CY v3: tools/n2cy_patch.py), not a copy of it per event
            worlds = [Path(dist) / 'city.n2cy'] + sorted(Path(dist).glob('track-*.n2cy'))
            n2cy_patch.patch_archive(src, n2cy_patch.base_of(src, worlds), dest / p.name)
            n += 1
            size += (dest / p.name).stat().st_size
            continue
        try:
            os.link(src, dest / p.name)
        except OSError:
            shutil.copy2(src, dest / p.name)
        n += 1
        size += src.stat().st_size
    if xmb_art and __import__('pbp').kit_xmb(Path(eboot).read_bytes()):
        # the kit carries its own XMB (pbp.KIT_XMB, from 0.4.0): the card's EBOOT is the kit's
        shutil.copy2(eboot, dest / 'EBOOT.PBP')
    elif xmb_art:
        # the art from the player's own disc (the kit's EBOOT has none):
        # read before --pack moves icon0.png and pic1.png into pack.n2pk
        import pbp
        art = {n: Path(dist) / n for n in ('icon0.png', 'pic1.png')}
        missing = [n for n, p in art.items() if not p.is_file()]
        if missing:
            sys.exit(f'--xmb-art: {", ".join(missing)} missing in {dist} (tools/convert_hud.py writes them)')
        (dest / 'EBOOT.PBP').write_bytes(pbp.with_art(Path(eboot).read_bytes(), art['icon0.png'].read_bytes(),
                                                      art['pic1.png'].read_bytes()))
        shutil.copystat(eboot, dest / 'EBOOT.PBP')
    else:
        shutil.copy2(eboot, dest / 'EBOOT.PBP')
    size += (dest / 'EBOOT.PBP').stat().st_size
    if runs:
        lines = [f'{t} {RUNS[t]}' for t in runs]
        (dest / 'ms-runs.txt').write_text('# NFSU2VIEW: each start from the XMB runs the next line (ms-done.txt counts them)\n'
                                          + '\n'.join(lines) + '\n')
    if pack:   # (an EBOOT from 17/10 on: one before it reads no pack.n2pk, and its files are gone)
        packed, packed_bytes = write_pack(dest)
        n -= packed - 1
        print(f'pack.n2pk: {packed} files, {packed_bytes / 2**20:.0f} MB')
    if release:
        (Path(out) / 'README.txt').write_text(player_steps(sfo, size))
    else:
        (Path(out) / 'README-card.txt').write_text(steps(runs, size))
    print(f'{dest}: {n + 1 + bool(runs)} files, {size / 2**20:.0f} MB (excluding {skipped} files, {skipped_bytes / 2**20:.0f} MB: '
          f'test outputs, converter .json files and legacy opt-/vinyl- files)')
    print('runs:', ', '.join(runs) or 'none')
    print(f"PARAM.SFO: {sfo.get('TITLE')} {sfo.get('DISC_VERSION')}, MEMSIZE={sfo.get('MEMSIZE')}")


WHAT = {'bench': 'memory card benchmark: on-screen text, about 1 minute',
        '4101': 'race 4101 (Bayview Plaza), about 3 minutes',
        'outer': 'Outer Ring, about 7 minutes',
        '4101-512': '4101 with larger city read chunks, about 3 minutes',
        '4101-32': '4101 with smaller city read chunks (comparison), about 3 minutes'}


def steps(runs, size):
    """Instructions included with the memory card benchmark package."""
    order = '\n'.join(f'   {k + 1}. {WHAT[r]}' for k, r in enumerate(runs))
    return f"""NFSU2 PSP: Memory Stick benchmark

The game runs these benchmarks automatically. PSPLink and a connected Mac
are not required during the races.

1. Connect the PSP to the Mac by USB. In the XMB, open Settings > USB Connection.
   The memory card appears in Finder.
2. Use the copy helper to avoid macOS ._ sidecar files, which slow card access.
   On the Mac, run:
   tools/ms_package.py push /Volumes/<card> <this-package> --fresh
   ({size / 2**30:.1f} GB; takes a few minutes; saves are preserved).
3. Eject the card in Finder, then leave USB Connection with the Circle button.
4. In XMB Game > Memory Stick, start "NFSU2 PSP" {len(runs)} times. Each launch
   runs one benchmark automatically and returns to the XMB:
{order}
   If the game freezes, hold the power switch until it shuts down, turn it
   back on and launch again. The unfinished benchmark will repeat.
5. Open USB Connection again and report that the results are ready. Results
   are in the card's NFSU2VIEW folder: files starting with {', '.join(r + '-' for r in runs)}
   and ms-done.txt.

After all benchmarks, the game starts normally from the XMB.
"""


def player_steps(sfo, size):
    """Installation instructions included with the player package."""
    return f"""{sfo.get('TITLE', 'NFSU2 PSP')} (version {sfo.get('DISC_VERSION', '?')})

Install on PSP (Memory Stick or microSD with an adapter)

1. Connect the PSP to the Mac by USB. In the XMB, open Settings > USB Connection.
2. Use the copy helper to avoid macOS ._ sidecar files, which slow card access.
   On the Mac, run:
   tools/ms_package.py push /Volumes/<card> <this-package> --fresh
   ({size / 2**30:.1f} GB; takes a few minutes; saves are preserved).
3. Eject the card in Finder, then leave USB Connection with the Circle button.
4. In XMB Game > Memory Stick, open "{sfo.get('TITLE', 'NFSU2 PSP')}".

The game selects its memory profile automatically: extended on PSP-2000,
PSP-3000 and Go, compact on PSP-1000. Saves appear in XMB Saved Data Utility.
"""


def collect(card, tag):
    card = Path(card)
    done = (card / 'ms-done.txt').read_text().split()[0] if (card / 'ms-done.txt').exists() else '?'
    print(f'{card}: ms-done.txt = {done}')
    found = []
    for run in RUNS:
        files = {f: card / f'{run}-{f}' for f in OUTPUTS if (card / f'{run}-{f}').exists()}
        if not files:
            continue
        import psp_run
        o = psp_run.RUNS / f'ms-{tag}-{run}'
        o.mkdir(parents=True, exist_ok=True)
        for f, p in files.items():
            shutil.copy2(p, o / f)
        print(f'{run}: {", ".join(files)} -> {o}')
        found.append(o)
    for o in found:
        summary(o)


def bench(path):
    """The table, then each scattered case's median read by where in the
    file it landed (quarters): a seek far into city.n2cy paying FAT's
    cluster chain shows as a median growing with the quarter."""
    lines = Path(path).read_text().splitlines()
    us, off, size = {}, {}, 0
    for line in lines:
        p = line.split()
        if line.startswith('us '):
            us[p[1]] = list(map(int, p[2:]))
        elif line.startswith('off '):
            off[p[1]] = list(map(int, p[2:]))
        else:
            print(line)
            if line.startswith('card-bench') and 'bytes,' in p:
                size = int(p[p.index('bytes,') - 1])
    for case, o in off.items():
        if not size or case not in us:
            continue
        q = [[] for _ in range(4)]
        for t, at in zip(us[case], o):
            q[min(3, at * 4 // size)].append(t)
        med = ['%.2f' % (sorted(x)[len(x) // 2] / 1000) if x else '-' for x in q]
        print(f'{case}: median (ms) per quarter of the file: {" / ".join(med)}')


def verdict(st):
    """A race's line against the goal (ROADMAP, "Prioridade: jogar do Memory
    Stick"): read in pieces of 512 KB, to the flag, at least 99% of its
    frames in 2 vblanks and no hole in view."""
    race, vb = st.get('race') or {}, st.get('vblanks_per_frame')
    if not race.get('racers') or not vb:
        return
    piece = (st.get('stream') or {}).get('piece_kb')
    on_time = 100 * (vb[0] + vb[1]) / max(sum(vb), 1)
    holes = (st.get('popin') or {}).get('hole_frames')
    done = race.get('state') == 3 and race['racers'][0].get('finished') == 1
    ok = done and on_time >= 99 and holes == 0
    print(f"  verdict: chunk {piece if piece is not None else '?'} KB, race {'finished' if done else 'incomplete'}, "
          f"{on_time:.1f}% in 2 vblanks, visible holes in {holes} frames -> {'OK' if ok else 'BELOW TARGET'}")


def summary(folder):
    folder = Path(folder)
    print(f'== {folder.name}')
    if (folder / 'card-bench.txt').exists():
        bench(folder / 'card-bench.txt')
    if (folder / 'frame-log.bin').exists():
        import psp_run
        psp_run.summary(folder)
    if (folder / 'viewer-stats.json').exists():
        st = json.loads((folder / 'viewer-stats.json').read_text())
        io, lat = st.get('io'), st.get('io_lat')
        if io:
            print(f"  whole-game I/O: {io['reads']} reads, {io['kb'] / 1024:.1f} MB, {io['ms'] / 1000:.1f} s reading, "
                  f"{io['opens']} opens, {io['seeks']} seeks")
        if lat:
            edges = lat['edges_ms']
            for k, kb in enumerate(lat['sizes_kb']):
                h = lat['hist'][k]
                if not lat['reads'][k]:
                    continue
                label = f'up to {kb} KB' if kb else 'larger'
                tail = sum(h[b] for b in range(len(h)) if b and edges[b - 1] >= 16)
                print(f"  reads {label:10}: {lat['reads'][k]:6}, {lat['kb'][k] / 1024:7.1f} MB, "
                      f"average {lat['ms'][k] / max(lat['reads'][k], 1):6.2f} ms, {tail} over 16 ms")
            print(f"  longest read: {lat['max_ms']:.1f} ms ({lat['max_kb']} KB)")
        pop = st.get('world', {}).get('popin') if isinstance(st.get('world'), dict) else st.get('popin')
        if pop:
            print(f"  pop-in: visible holes in {pop['hole_frames']} of {pop['frames']} frames "
                  f"({pop['near_hole_frames']} within 70 m)")
        verdict(st)
    if (folder / 'boot.txt').exists():
        text = (folder / 'boot.txt').read_text().split('\n')
        for note in (r for r in text if r.startswith('#')):
            print(f'  boot.txt {note}')
        rows = [r for r in text if not r.startswith('#')]
        last = [r for r in rows if r.strip()][-1].split()
        print(f'  boot.txt: {len(rows) - 2} steps, last "{" ".join(last[:-7])}" at {last[-7]} ms, '
              f'{last[-6]} reads, {int(last[-3]) / 1024:.1f} MB, {last[-2]} ms reading')


def litter(folder):
    """macOS's files in the game's folder: the AppleDouble ._ of each file a
    Mac copies to a FAT volume, and .DS_Store. Each is an entry more the
    card's driver walks at every open (16/10: 977 of the card's 1,957)."""
    return sorted(p.name for p in Path(folder).iterdir() if p.name.startswith('._') or p.name == '.DS_Store')


def card_folder(volume):
    vol = Path(volume)
    if not (vol / 'PSP').is_dir():
        sys.exit(f'{vol} does not look like a PSP memory card (no PSP folder): mount it through XMB USB Connection')
    return vol / 'PSP/GAME/NFSU2VIEW'


def tidy(dest, env):
    """dot_clean -m on the game's folder (the ._ a copy left merged away),
    then an error (True) if any ._ or .DS_Store is still there."""
    import subprocess
    subprocess.run(['dot_clean', '-m', str(dest)], env=env)
    left = litter(dest)
    if left:
        print(f'ERROR: {len(left)} macOS files remain in the game folder: {left[:10]}')
    return bool(left)


def put(volume, files, backup=None):
    """Single files (PATH or NAME=PATH) into the card's game folder: cp -X
    under COPYFILE_DISABLE=1, so no ._ is written beside them; a file
    already there renamed to NAME-BACKUP.ext.bak first when backup is set;
    then tidy. Returns the exit code."""
    import subprocess
    dest = card_folder(volume)
    if not dest.is_dir():
        sys.exit(f'{dest} does not exist: use push to copy the game to the card')
    env = dict(os.environ, COPYFILE_DISABLE='1')
    for f in files:
        name, _, path = f.partition('=') if '=' in f else (Path(f).name, '', f)
        src, to = Path(path), dest / name
        if not src.is_file() or name.startswith('.') or '/' in name:
            sys.exit(f'invalid file: {f}')
        if backup and to.exists():
            bak = to.with_name(f'{to.stem}-{backup}{to.suffix}.bak')
            to.rename(bak)
            print(f'{name} previous -> {bak.name}', flush=True)
        r = subprocess.run(['cp', '-X', str(src), str(to)], env=env)
        if r.returncode:
            print(f'cp failed ({r.returncode}): {name}')
            return r.returncode
        print(f'{name} <- {src} ({src.stat().st_size} bytes)', flush=True)
    if tidy(dest, env):
        return 2
    subprocess.run(['sync'])
    print('Eject the card in Finder before leaving USB Connection.')
    return 0


def clean(volume):
    """The card's game folder without macOS's files: dot_clean, then the
    ._ and .DS_Store left deleted (only those, only in that folder)."""
    import subprocess
    dest = card_folder(volume)
    before = litter(dest)
    subprocess.run(['dot_clean', '-m', str(dest)], env=dict(os.environ, COPYFILE_DISABLE='1'))
    for n in litter(dest):
        (dest / n).unlink()
    print(f'{dest}: {len(before)} macOS files removed' + (f', {len(litter(dest))} remain' if litter(dest) else ''))
    return 2 if litter(dest) else 0


def push(volume, package, fresh):
    """A package (make's OUT) onto the card mounted by the XMB's USB
    connection, the one way to copy it: rsync with no extended attributes
    (COPYFILE_DISABLE=1, no ._ copies), only the changed files, the ones
    the package no longer has deleted but the saves (*.sav) kept; then
    dot_clean -m, and an error if any ._ or .DS_Store is left in the game's
    folder. --fresh: the old folder renamed first, the package copied into
    a new one and the saves moved over, then the old one deleted: a FAT
    folder never shrinks, and the entries of the files deleted in it stay
    (16/10: the opens 250 ms in the 4,190 files' folder, 8-470 ms after
    rsync --delete into it). Returns the exit code."""
    import subprocess
    import time
    src = Path(package) / 'PSP/GAME/NFSU2VIEW'
    dest = card_folder(volume)
    if not (src / 'EBOOT.PBP').is_file():
        sys.exit(f'{src} is not a package (no EBOOT.PBP): run tools/ms_package.py make first')
    if litter(src):
        sys.exit(f'the package contains macOS files: {litter(src)[:5]}')
    env = dict(os.environ, COPYFILE_DISABLE='1')
    if dest.is_dir():   # the runs' files and the rest the game wrote (not the saves, kept on the card): on the Mac before they go
        kept = [p for p in dest.iterdir() if p.is_file() and ppsspp_run.game_output(p.name) and not p.name.endswith('.sav')
                and not p.name.startswith('._')]
        if kept:
            try:
                import psp_run
                reports = psp_run.main_checkout() / 'reports'
            except ImportError:   # (the beta's repository: beside the package)
                reports = Path(package).resolve().parent
            where = reports / f'ms0-push-{time.strftime("%Y%m%d-%H%M%S")}'
            where.mkdir(parents=True, exist_ok=True)
            for p in kept:
                shutil.copy2(p, where / p.name)
            print(f'{len(kept)} game outputs from the card saved in {where}', flush=True)
    old = None
    if fresh and dest.exists():
        old = dest.with_name('NFSU2VIEW-old-' + time.strftime('%Y%m%d%H%M%S'))
        dest.rename(old)
    dest.mkdir(parents=True, exist_ok=True)
    if old:   # the saves first, so a copy cut short still has them
        for p in old.glob('*.sav'):
            shutil.copy2(p, dest / p.name)
    # -r -t, no -a: FAT keeps no owners or modes; no -E/-X: no extended attributes
    cmd = ['rsync', '-rt', '--modify-window=2', '--delete', '--exclude', '._*', '--exclude', '.DS_Store',
           '--exclude', '*.sav', '--itemize-changes', f'{src}/', f'{dest}/']
    print(' '.join(cmd), flush=True)
    r = subprocess.run(cmd, env=env)
    if r.returncode:
        print(f'rsync failed ({r.returncode})' + (f'; previous folder is at {old}' if old else ''))
        return r.returncode
    if tidy(dest, env):
        return 2
    if old:
        shutil.rmtree(old)
    subprocess.run(['sync'])
    have = sorted(p.name for p in dest.iterdir())
    print(f'{dest}: {len(have)} files, no ._ or .DS_Store' + ('; new folder (old folder removed, saves preserved)' if old else ''))
    print('Eject the card in Finder before leaving USB Connection.')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    m = sub.add_parser('make')
    m.add_argument('out')
    m.add_argument('--dist', default=str(ppsspp_run.DIST))
    m.add_argument('--eboot', default=str(ROOT / 'psp-viewer/EBOOT.PBP'))
    m.add_argument('--runs', default='bench,4101,outer,4101-32')
    m.add_argument('--whole-bars', action='store_true', help='the barriers as in the dist (an EBOOT from before N2CY v3)')
    m.add_argument('--pack', action='store_true', help='the files under 1 MB in pack.n2pk, not loose (an EBOOT that reads it: main.c pack_load)')
    m.add_argument('--release', action='store_true', help="the players' package: no runs, no tools' or tests' files, MEMSIZE=1 checked")
    m.add_argument('--xmb-art', action='store_true',
                   help="the EBOOT's XMB icon and background from the dist's icon0.png and pic1.png (the beta's kit EBOOT has none)")
    c = sub.add_parser('collect')
    c.add_argument('card')
    c.add_argument('tag')
    s = sub.add_parser('summary')
    s.add_argument('dirs', nargs='+')
    u = sub.add_parser('push', help='the package onto the card mounted by USB (the one way to copy it)')
    u.add_argument('volume', help='the card as the Mac mounts it, e.g. /Volumes/PSP')
    u.add_argument('package', help="make's OUT")
    u.add_argument('--fresh', action='store_true', help='a new folder (the FAT entries of deleted files gone), the saves moved over')
    t = sub.add_parser('put', help="single files into the card's game folder, with no macOS ._ files")
    t.add_argument('volume', help='the card as the Mac mounts it, e.g. /Volumes/PSP')
    t.add_argument('files', nargs='+', help='PATH or NAME=PATH')
    t.add_argument('--backup', metavar='TAG', help='a file already there renamed NAME-TAG.ext.bak first')
    k = sub.add_parser('clean', help="macOS's ._ and .DS_Store out of the card's game folder")
    k.add_argument('volume')
    b = sub.add_parser('bench')
    b.add_argument('file')
    a = ap.parse_args()
    if a.cmd == 'make':
        runs = [] if a.release else [r for r in a.runs.split(',') if r]
        bad = [r for r in runs if r not in RUNS]
        if bad:
            ap.error(f'unknown runs: {bad} (available: {", ".join(RUNS)})')
        make(a.out, a.dist, a.eboot, runs, not a.whole_bars, a.pack, a.release, a.xmb_art)
    elif a.cmd == 'push':
        sys.exit(push(a.volume, a.package, a.fresh))
    elif a.cmd == 'put':
        sys.exit(put(a.volume, a.files, a.backup))
    elif a.cmd == 'clean':
        sys.exit(clean(a.volume))
    elif a.cmd == 'collect':
        collect(a.card, a.tag)
    elif a.cmd == 'summary':
        for d in a.dirs:
            summary(d)
    else:
        bench(a.file)


if __name__ == '__main__':
    main()
