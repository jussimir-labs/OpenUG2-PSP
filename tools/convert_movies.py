#!/usr/bin/env python3
"""The game's movies -> movie-<NAME>.n2mv for the PSP (psp-viewer/movie.c).

Which: the movie catalog (GLOBALB chunk 0x39020, LoaderMovieCatalog: 40
records of 40 bytes, the name at +8, the volume at +39) names them, with
its language variants (ealogo_fr...); the Xbox disc has 28 of them, all
converted: the boot's EALOGO, THX_LOGO, PSA and FMVOPENING, the story's
SCENE01 to SCENE20 (no 02-04 nor 19), the tutorials (DRAGTUT, DRIFTTUT,
STREETXTUT, ORGANICSTUT) and the regions' (SUBURBAN, UPPERCLASS,
INDUSTEAST, INDUSTWEST).

MOVIES\\<NAME>.VP6 (the Xbox's: EA's container, VP6 640x480 at 29.97 fps,
5.1 EA-XA at 48 kHz): the picture through ffmpeg - 15 frames a second, the
4:3 picture 363 x 272 in the middle of 480 x 272 - each frame a baseline
4:2:0 JPEG made by jpegli (Google's encoder of 2024, built by
tools/setup_jpegli.sh), read by the PSP's own decoder (sceJpeg); the sound
through tools/movie_audio.c, folded to stereo at 44100 Hz and encoded by
lame as MP3 (the PSP's sceMp3, which takes no 32 kHz). movie-<NAME>.n2mv:
'N2MV', u32 version 3, fps, width, height, frames, sound rate, sound
frames, index offset, sound offset; the index (frames x u32 offset, u32
size), the JPEGs, the sound (an MP3 stream to the file's end). Version 1
had libjpeg's JPEGs and PCM.

The PSP's own games give their movies H.264 at 480 x 272 and ~1.1 Mbit/s
with the sound. Here, SCENE13's first 20 s against the scaled picture
unpacked: jpegli at distance 8 comes to ~1.1 Mbit/s for an SSIM of 0.949,
what libjpeg needs ~1.25 Mbit/s for (its quality 9 of before: 0.956 at 1.43
Mbit/s), with visibly less ringing; the MP3 at 128 kbit/s, ATRAC3plus's
usual (the PCM of before was 706 kbit/s).

--h264 (experimental, off): H.264 by x264 (29.97 fps, CRF 23, Main, level
3, no B frames, a reference, AUD and the HRD's SEIs), version 2, which the
Media Engine decodes (movie.c's avc_*) - an SSIM of 0.975 at ~430 kbit/s -
but on the PSP-3000 it froze the Media Engine a few seconds into the movie
(fed by sceMpegBasePESpacketCopy; the players that kept H.264 went to
sceMpegGetAvcNalAu, which needs a kernel module to boot the ME for AVC).
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import mac_slots
from native_runtime import binary, helper as runtime_helper, tool as runtime_tool
from n2formats import game_hash
import n2source

ROOT = Path(__file__).resolve().parents[1]
W, H, PICTURE_W, RATE, MP3_KBPS, VERSION = 480, 272, 363, 44100, 128, 3
FPS, JPEGLI_DISTANCE = 15, 8
CJPEGLI = binary('cjpegli') or ROOT / '.tools/jpegli/build/tools/cjpegli'
H264_VERSION = 2
CRF = 23
X264 = ('bframes=0:ref=1:weightp=0:8x8dct=0:keyint=150:aud=1:nal-hrd=vbr:vbv-maxrate=1500:vbv-bufsize=1500')
AU_MAX = 64 * 1024   # movie.c's AVC_MAX


def movie_ext(game):
    """The movies' extension: the Xbox's VP6, the PS2's MPC (EA's MPEG-2,
    512 x 192 for the whole 4:3 picture, EA ADPCM R3 stereo at 32 kHz:
    ffmpeg reads both; the PS2's 192 lines stretch to the 272)."""
    return 'MPC' if n2source.platform(game) == 'ps2' else 'VP6'


def disc_entry(game, name):
    """MOVIES\\<NAME>.VP6's ZDIR entry (.MPC on the PS2), None when the disc has no such movie."""
    z = (game / 'ZDIR.BIN').read_bytes()
    return next((e for e in struct.iter_unpack('<6I', z) if e[0] == game_hash(f'MOVIES/{name}.{movie_ext(game)}')), None)


def catalog(game):
    """The movies of the catalog (0x39020) the disc has, upper case, in the
    catalog's order."""
    from n2formats import archived as arch, chunks
    gb = arch(game, 'GLOBAL/GLOBALB.BUN')
    names = [gb[p + 8:gb.index(b'\0', p + 8)].decode('latin-1').upper() for t, p, n in chunks(gb) if t == 0x39020]
    return [n for n in dict.fromkeys(names) if disc_entry(game, n)]


def movie_file(game, name):
    """MOVIES\\<NAME>.VP6 out of ZZDATA (SCENE01's 78 MB is past n2formats'
    archived() cap)."""
    e = disc_entry(game, name)
    with (game / f'ZZDATA{e[1]}.BIN').open('rb') as f:
        f.seek(e[2] * 2048)
        return f.read(e[4])


def access_units(stream):
    """An H.264 stream's access units, cut at their delimiters (NAL 9), and
    whether each holds an IDR slice (NAL 5)."""
    cuts = []
    at = stream.find(b'\x00\x00\x01')
    while at >= 0:
        if stream[at + 3] & 0x1f == 9:
            cuts.append(at - 1 if at and stream[at - 1] == 0 else at)
        at = stream.find(b'\x00\x00\x01', at + 3)
    out = []
    for a, b in zip(cuts, cuts[1:] + [len(stream)]):
        au = stream[a:b]
        idr, k = False, au.find(b'\x00\x00\x01')
        while k >= 0 and not idr:
            idr = au[k + 3] & 0x1f == 5
            k = au.find(b'\x00\x00\x01', k + 3)
        out.append((au, idr))
    return out


def jpegli_frames(src, tmp):
    """The picture at 15 fps, each frame a jpegli JPEG: ffmpeg's RGB frames
    one at a time through a PPM to cjpegli (sequential, 4:2:0: sceJpeg's)."""
    pad = (W - PICTURE_W) // 2
    dec = subprocess.Popen([runtime_tool('ffmpeg'), '-hide_banner', '-loglevel', 'error', '-i', str(src), '-an', '-vf',
                            f'fps={FPS},scale={PICTURE_W}:{H}:flags=lanczos,pad={W}:{H}:{pad}:0:black',
                            '-pix_fmt', 'rgb24', '-f', 'rawvideo', '-'], stdout=subprocess.PIPE)
    ppm, jpg, frames = Path(tmp) / 'f.ppm', Path(tmp) / 'f.jpg', []
    while True:
        rgb = dec.stdout.read(W * H * 3)
        if len(rgb) < W * H * 3:
            break
        ppm.write_bytes(b'P6\n%d %d\n255\n' % (W, H) + rgb)
        subprocess.run([str(CJPEGLI), str(ppm), str(jpg), '-p', '0', '--chroma_subsampling=420', '-d', str(JPEGLI_DISTANCE),
                        '--quiet'], check=True)
        frames.append(jpg.read_bytes())
    if dec.wait():
        raise SystemExit(f'{src.name}: ffmpeg failed')
    return frames


def convert(game, out, name, helper, h264=False):
    with tempfile.TemporaryDirectory(prefix='nfsu2-movie-') as tmp:
        src = Path(tmp) / f'{name}.{movie_ext(game).lower()}'
        src.write_bytes(movie_file(game, name))
        pad = (W - PICTURE_W) // 2
        frames = [(f, False) for f in jpegli_frames(src, tmp)] if not h264 else None
        video = None if not h264 else subprocess.run([runtime_tool('ffmpeg'), '-hide_banner', '-loglevel', 'error', '-i', str(src), '-an', '-vf',
                                f'fps=30000/1001,scale={PICTURE_W}:{H}:flags=lanczos,pad={W}:{H}:{pad}:0:black,format=yuv420p',
                                '-c:v', 'libx264', '-preset', 'veryslow', '-profile:v', 'main', '-level', '3.0', '-crf', str(CRF),
                                '-x264-params', X264, '-threads', '2', '-f', 'h264', '-'],   # two threads: the queue's weight
                               capture_output=True, check=True).stdout
        if h264:
            frames = access_units(video)
        if h264 and (not frames or not frames[0][1] or max(len(f) for f, _ in frames) > AU_MAX):
            raise SystemExit(f'{name}: the H.264 stream does not start on an IDR frame or has an access unit past {AU_MAX} bytes')
        raw, mp3 = Path(tmp) / 'sound.raw', Path(tmp) / 'sound.mp3'
        if movie_ext(game) == 'MPC':   # the PS2's sound: EA ADPCM R3, through ffmpeg
            info = subprocess.run([runtime_tool('ffmpeg'), '-hide_banner', '-loglevel', 'error', '-y', '-i', str(src), '-vn', '-ac', '2',
                                   '-ar', str(RATE), '-f', 's16le', str(raw)], capture_output=True, text=True)
        else:
            info = subprocess.run([str(helper), str(src), str(raw), str(RATE)], capture_output=True, text=True)
        samples, sound = 0, b''
        if info.returncode == 0 and raw.exists() and raw.stat().st_size:
            samples = raw.stat().st_size // 4
            subprocess.run([runtime_tool('lame'), '--quiet', '-r', '-s', str(RATE / 1000), '--bitwidth', '16', '--signed', '--little-endian',
                            '-m', 'j', '-b', str(MP3_KBPS), '--cbr', str(raw), str(mp3)], check=True)
            sound = mp3.read_bytes()
    head = 40
    index_at = head
    data_at = index_at + 8 * len(frames)
    index, at = [], data_at
    for f, idr in frames:
        index.append(struct.pack('<2I', at, len(f) | idr << 31))
        at += len(f)
    sound_at = at
    blob = struct.pack('<4s9I', b'N2MV', H264_VERSION if h264 else VERSION, 29970 if h264 else FPS, W, H, len(frames), RATE, samples,
                       index_at, sound_at)
    blob += b''.join(index) + b''.join(f for f, _ in frames) + sound
    (out / f'movie-{name}.n2mv').write_bytes(blob)
    return name, len(frames), samples / RATE, len(blob) // 1024


# The old movies.txt's movies, in its order: their lines stay the first
# ones, so movies.sav's bits (1 + the line) keep meaning the same movies.
LEGACY = ('SCENE10', 'SCENE08', 'SCENE09', 'INDUSTEAST', 'SCENE05', 'SCENE06')
FINAL = 0x04CCE3E1   # the career's last race in CalcShowPostRaceMovie: the bin hash of S5_CIRCUIT_36 (+0x8), 4088


def triggers(game, out, movies):
    """Where the game plays them, as its code and GLOBALB's career data say.
    movies.txt, a line each, the lines of one moment played in their order:
      pre <event> <stage> <movie>    accepting that career event (every
                                     time: RaceEngageDialog::AcceptRace,
                                     InGamePlayMovie::LaunchPreEventMovie);
      post <event> <stage> <movie>   back on the street after winning it,
                                     once (PlayerCareerState::
                                     CalcShowPostRaceMovie);
      stage <stage> <movie>          back on the street from the race that
                                     reached that stage, when the race has
                                     no post movie (SCENE07, then
                                     UPPERCLASS: LaunchPostRaceMovie);
      final <event> <won|lost> <movie>  back on the street from the career's
                                     last race, when it has no post movie
                                     (SCENE15 won, SCENE16 lost);
      place <carlot|crib> <movie>    the first time in from the street
                                     (LaunchPreShopMovie).
    A career race's two movie fields: +0x2 (before) and +0x4 (after), strings
    of the career pool 0x34A1D; SCENE20 brings DRIFTTUT after it. The port
    groups the original's career races into a row per event and stage
    (convert_career.build): a race's movies go to the event the player
    engages for the row it gave (its tournament's first), else for the row
    of its first event. The boot's (EALOGO, THX_LOGO, PSA, FMVOPENING) and
    SCENE01 are the code's own (BootFlowNTSC, LaunchFirstCareerMovie)."""
    import convert_career as cc
    from n2formats import archived as arch, chunks
    gb = arch(game, 'GLOBAL/GLOBALB.BUN')
    pool = cc.sections(gb)[0x34a1d]

    def text(o):
        return pool[o:pool.index(b'\0', o)].decode('latin-1').upper() if 0 < o < len(pool) else ''
    races = cc.career_races(gb)
    ids = cc.read_event_ids(out / 'races.n2rc')
    track_ids = set()
    for f in sorted(out.glob('races-L4R?.n2rc')):
        track_ids.update(cc.read_event_ids(f))
    rows = cc.build(races, ids + sorted(track_ids - set(ids)), track_ids)
    lead = {x['group']: x for x in rows if x['group'] and not x['flags'] & 2}
    by_key = {(x['id'], x['stage']): x for x in rows}

    def engaged(r):
        own = [x for x in rows if x['race'] == r['index'] and x['stage'] == r['stage']]
        x = own[0] if own else by_key.get((r['events'][0][0], r['stage']))
        return None if x is None else (lead[x['group']] if x['group'] else x)['id']
    blob = next(gb[p:p + n] for t, p, n in chunks(gb) if t == 0x34A11)
    lines, last = [], None
    for r in sorted(races, key=lambda r: (r['stage'], r['index'])):
        rec = blob[r['index'] * 0x88:(r['index'] + 1) * 0x88]
        pre, post = (text(struct.unpack_from('<H', rec, f)[0]) for f in (2, 4))
        if (pre or post) and engaged(r) is not None:
            if pre:
                lines += [f'pre {engaged(r)} {r["stage"]} {pre}'] + ([f'pre {engaged(r)} {r["stage"]} DRIFTTUT'] if pre == 'SCENE20' else [])
            if post:
                lines.append(f'post {engaged(r)} {r["stage"]} {post}')
        if struct.unpack_from('<I', rec, 8)[0] == FINAL:
            last = engaged(r)
    lines += ['stage 2 SCENE07', 'stage 2 UPPERCLASS', f'final {last} won SCENE15', f'final {last} lost SCENE16',
              'place carlot SCENE05', 'place crib SCENE06']
    missing = sorted({l.split()[-1] for l in lines} - set(movies))
    if missing:
        raise SystemExit(f'movies.txt names movies not on the disc: {missing}')
    once = [l for l in lines if l.split()[0] in ('post', 'place')]
    order = [next((l for l in lines if l.split()[-1] == m and (l in once or m in ('SCENE10', 'SCENE09'))), None) for m in LEGACY]
    lines = [l for l in order if l] + [l for l in lines if l not in order]
    (out / 'movies.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--only', nargs='*', help='these movies only (default: the catalog\'s on the disc)')
    ap.add_argument('--missing', action='store_true', help='only the movies not yet converted')
    ap.add_argument('--h264', action='store_true', help='experimental: H.264 (version 2), which froze the PSP-3000\'s Media Engine')
    args = ap.parse_args()
    if not args.h264 and not CJPEGLI.is_file():
        raise SystemExit(f'jpegli not built ({CJPEGLI}): tools/setup_jpegli.sh')
    movies = catalog(args.game)
    print('movies.txt:', len(triggers(args.game, args.out, movies)), 'lines;', len(movies), 'movies on the disc')
    def current(m):   # converted, in this version
        f = args.out / f'movie-{m}.n2mv'
        if not f.exists():
            return False
        with f.open('rb') as h:
            return h.read(8) == struct.pack('<4sI', b'N2MV', H264_VERSION if args.h264 else VERSION)
    todo = args.only or [m for m in movies if not (args.missing and current(m))]
    helper = Path(tempfile.gettempdir()) / 'nfsu2-movie-audio'
    helper = runtime_helper('movie_audio', helper, [ROOT / 'tools/movie_audio.c'])
    with concurrent.futures.ThreadPoolExecutor(max(1, (os.cpu_count() or 2) // 2)) as pool:
        # each movie's ffmpeg and encoder: two cores from the Mac's queue (tools/mac_slots.py)
        for name, frames, secs, kb in pool.map(lambda n: mac_slots.call('convert_movies', convert, args.game, args.out, n, helper,
                                                                        args.h264, weight=2), todo):
            print(f'movie-{name}.n2mv: {frames} frames, {secs:.1f} s of sound, {kb} KB')


if __name__ == '__main__':
    main()
