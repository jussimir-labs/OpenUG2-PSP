#!/usr/bin/env python3
"""Xbox NFSU2 audio -> PSP: engine sweep, sound effects, EA Trax music.

- The cars' engines as the original picks them (car_sound): carsnd.n2cs,
  the original's sound tables (default.xbe); engine-<GIN>.n2en, each
  engine's Ginsu sweep (SOUND\\ENGINE\\GIN_*.gin, "Gnsu20": f32 rpm
  min/max, u32 n1/n2/total samples/rate, rpm_pos[n1+1], grain[n2+1], then
  EA-XAS v0 frames of 0x13 bytes / 32 samples, decoded to s16 PCM), and
  engine.n2en the 350Z's for Rachel's car; aems-NN.n2ab, the AEMS engine
  banks (the layer under the sweep, and the rivals' whole engine);
  gear-<bank>.n2sf, the gearboxes' shift sounds.
- sfx.n2sf: named one-shots and loops from ABKC banks (BNKl at +0x18; PT
  headers via the BNKl offset table; Xbox PT platform 7 uses codec 0x14 =
  Xbox IMA ADPCM, 36-byte mono blocks of 64 samples), decoded to s16 PCM.
- music.mp3 + music.idx: SOUND\\PFDATA\\MUSICSFX.MUS (43 GSTR SCHl
  tracks, EA-XA v2, stereo 32 kHz, big-endian block fields), each song
  decoded (tools/eaxa2pcm.c, the decoding the PSP did) and encoded by lame
  as MP3, resampled to 44.1 kHz (32 kHz fails with 0x80671302 on the
  current SDK path in PPSSPP; 44.1 kHz also matches the PSP's output rate),
  joint stereo at 128 kbit/s CBR, no tags: the PSP's Media Engine decodes
  it (sceMp3), off the CPU. 27 tracks are the EA Trax songs
  (RMS -9 to -13 dBFS); the other 16, 10-20 dB quieter, are the city's
  and the shops' ambiences (see pathfinder_ambience),
  which the playlist skips (gain 0). Songs play at their own level, as the
  original's music channel plays them: one gain for all, the mean of the
  gains that would bring each to -10 dBFS RMS (the PS2's music against its
  engine, measured, agrees with that mean). The 8 ambiences of the city's zones (about two minutes each) are kept,
  at 64 kbit/s (44.1 kHz kept: LAME would go down to 24), made to loop
  without a seam: resampled to 44.1 kHz as
  three copies end to end (the middle one free of the resampler's edges),
  encoded with 4096 samples of its own end before and of its start after,
  and the samples to drop before the loop (that lead and the codec's delay)
  and the loop's length kept: a decoder off by a few samples still plays a
  continuous sound across the seam.
  music.idx: 'N2M3', u32 tracks, u32 offset and size per track (0: left
  out), f32 gains; then 'AMB1' and per track u32 samples to drop, u32 loop
  samples (0: not an ambience), f32 its gain.
- speech.big + speech.n2sp: Rachel's lines, SOUND\\SPEECH\\UG2_SPEECH.BIG
  copied verbatim (857 SCHl streams at 256-byte steps, PT platform 7,
  codec 10 = EA-XA v2 mono 32 kHz, little-endian block fields: u32
  samples, u32 data offset) and their index (speech() below).

Formats per the OpenUG2 notes and checks on the USA Xbox disc.
"""
import argparse
import concurrent.futures
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

import mac_slots
from n2formats import archived, game_hash
import n2source

ROOT = Path(__file__).resolve().parents[1]
IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
            107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
            876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
            4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
            22385, 24623, 27086, 29794, 32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8] * 2
XAS_C1 = [0, .9375, 1.796875, 1.53125]
XAS_C2 = [0, 0, -.8125, -.859375]
# name -> (bank path, stream slot in the BNKl table (slot 0 is always empty), loop[, keep every n-th sample])
SFX = {
    # the skids (CARSFX_Skids, SKID_PAV: five loops; which one a magnitude
    # plays is inside the FX_SKID patch, so here by their sound and by
    # what the banks share: the 1st, in all four banks, the slide; the 2nd,
    # changing with the car only, the wheels spinning; the 5th, the lock),
    # at 24 kHz; the other banks: skid-<bank>.n2sf (skid_banks)
    'skid':      (r'SOUND\SKIDS\SKID_PAV_MB.abk', 1, True, 2),
    'skid_spin': (r'SOUND\SKIDS\SKID_PAV_MB.abk', 2, True, 2),
    'skid_lock': (r'SOUND\SKIDS\SKID_PAV_MB.abk', 5, True, 2),
    'nitrous':   (r'SOUND\NOS\Nitrous_00_MB.abk', 1, True),
    # the bigger nitrous kits' (CARSFX_Nitrous: Nitrous_01 for levels 2-3), at 24 kHz
    'nitrous_2': (r'SOUND\NOS\Nitrous_01_MB.abk', 1, True, 2),
    'shift':     (r'SOUND\SHIFTING\GEAR_MED_Base.abk', 1, False),
    # the road noise (CARSFX_RoadNoise, ROADNOISE_00: seven loops at 38 kHz
    # for the terrains' eight loop ids, which one an id plays inside the
    # FX_ROADNOISE patch, so here in their order by their sound: 0 the
    # dirt's broad crunch, 1 the sidewalk, 2 the cobbles' even knocks, 3
    # the deep water, 4 the wet road's hiss, 5 the asphalt's long rumble, 6
    # the grate's; 7 the bridge's and metal's, the 4th's slow beat again;
    # and three of its one-shots as the terrains' transitions, the two
    # thumps on and off a kerb and the long splash), at 19 and 24 kHz
    **{f'road_{k}': (r'SOUND\IG_GLOBAL\ROADNOISE_00_MB.abk', slot, True, 2) for k, slot in enumerate((1, 2, 3, 4, 5, 6, 7, 4))},
    **{f'road_trans_{k}': (r'SOUND\IG_GLOBAL\ROADNOISE_00_MB.abk', slot, False, 2) for k, slot in enumerate((12, 13, 14))},
    'wind':      (r'SOUND\IG_GLOBAL\WIND_00_MB.abk', 1, True),
    # the hydraulics (7 streams, unnamed in the data): the pump building the
    # pressure (CAR_HYDRAULIC_EXTEND: the 1 s steady one) and the hop's thud
    # (CAR_HYDRAULIC_BOUNCE: the low one dying away)
    'hydro_pump':   (r'SOUND\NOS\Hydraulics_00_MB.abk', 1, True),
    'rain':         (r'SOUND\IG_GLOBAL\FX_Rain_MB.abk', 1, True),   # CARSFX_Rain: the 2.75 s loop (the other, 1.8 s, unused)
    'hydro_bounce': (r'SOUND\NOS\Hydraulics_00_MB.abk', 2, False),
    # the traffic's engines (CARSFX_TrafficEngine: TRAFFIC_MB's two 2.1 s
    # loops, a car's ~118 Hz and a bigger one's ~75/150 Hz), on the nearest
    # traffic car, placed by psp-viewer/main.c traffic_sound
    'traffic_a':    (r'SOUND\IG_GLOBAL\TRAFFIC_MB.abk', 1, True),
    'traffic_b':    (r'SOUND\IG_GLOBAL\TRAFFIC_MB.abk', 2, True),
    # scraping along a wall (FX_Scrape: FX_Misc_MB.abk's module names
    # FX_Camera and FX_Scrape, and its two samples are the camera's 1.4 s
    # shutter and this 0.8 s broadband loop); the impacts themselves are
    # the stitches (stitch.n2st)
    'scrape':       (r'SOUND\IG_GLOBAL\FX_Misc_MB.abk', 2, True),
    # the traffic's horn (CARSFX_TrafficHorn, default.xbe 0x8c2c0: the
    # ENV_STATIC patch of ENV_COMMON_MB with sample 5 + rand(3), the bank's
    # 4th to 6th: three steady ~0.95 s tones, 404, 475/950 and 316 Hz),
    # held 1.2 to 4 s, so looped
    'horn_1':       (r'SOUND\IG_GLOBAL\ENV_COMMON_MB.abK', 4, True),
    'horn_2':       (r'SOUND\IG_GLOBAL\ENV_COMMON_MB.abK', 5, True),
    'horn_3':       (r'SOUND\IG_GLOBAL\ENV_COMMON_MB.abK', 6, True),
    # the world's moving things (SFXObj_Train, SFXObj_Plane: the same
    # ENV_STATIC patch with samples 0 and 1; the patch's index lands on the
    # bank's slot 3 for the fountains' 4, a steady hiss, and 4-6 for the
    # horns' 5-7, so these two go by their sound: the bank's 1st, a 2.8 s
    # broadband rumble, the train; its 2nd, a 4.5 s low drone, the plane),
    # looped, kept at 12 and 8 kHz
    'train':        (r'SOUND\IG_GLOBAL\ENV_COMMON_MB.abK', 1, True, 3),
    'plane':        (r'SOUND\IG_GLOBAL\ENV_COMMON_MB.abK', 2, True, 4),
    # the turbo (SOUND\TURBO\TURBO_<size>_<level>_MB: five sounds each,
    # 48 kHz, kept at 24: the whistle's loop (1, ~5 kHz on the MED), three
    # blow-offs (2-4, one-shots decaying over a second), a hiss loop (5,
    # 8-9.5 kHz)); the port fits the medium one to every turbo car
    'turbo':     (r'SOUND\TURBO\TURBO_MED_0_MB.abk', 1, True, 2),
    'turbo_hiss': (r'SOUND\TURBO\TURBO_MED_0_MB.abk', 5, True, 2),
    'turbo_bov': (r'SOUND\TURBO\TURBO_MED_0_MB.abk', 3, False, 2),
    # its other two blow-offs: the first under 75% of the top pressure, the
    # third (or the second, at random) over it (CARSFX_Turbo::PlayBlowoff)
    'turbo_bov1': (r'SOUND\TURBO\TURBO_MED_0_MB.abk', 2, False, 2),
    'turbo_bov3': (r'SOUND\TURBO\TURBO_MED_0_MB.abk', 4, False, 2),
    # the exhaust's pops lifting off (SOUND\ENGINE\Sparkchatter_00: ten
    # bursts of 0.17-0.42 s, ~100 Hz): five of them, at 24 kHz
    **{f'pop_{k}': (r'SOUND\ENGINE\Sparkchatter_00.abk', s, False, 2) for k, s in enumerate((1, 3, 5, 7, 9))},
    # the menus (FE_COMMON_MB: 18 short one-shots, unnamed in the data;
    # the first ones, by their order and length, as move / accept / back)
    'fe_move':   (r'SOUND\GLOBAL\FE_COMMON_MB.abk', 1, False),     # 0.17 s
    'fe_accept': (r'SOUND\GLOBAL\FE_COMMON_MB.abk', 3, False),     # 0.27 s
    'fe_back':   (r'SOUND\GLOBAL\FE_COMMON_MB.abk', 2, False),     # 0.19 s
}


def s16(v):
    return max(-32768, min(32767, int(v)))


def decode_xas(data, nsamples):
    """EA-XAS v0: u32 LE header (coef, hist2, shift, hist1) + 15 bytes of
    nibbles, high nibble first; 32 samples per 0x13-byte frame."""
    out = []
    for at in range(0, len(data) - 0x12, 0x13):
        h = struct.unpack_from('<I', data, at)[0]
        ci = h & 0xF
        p2 = struct.unpack('<h', struct.pack('<H', h & 0xFFF0))[0]
        shift = (h >> 16) & 0xF
        p1 = struct.unpack('<h', struct.pack('<H', (h >> 16) & 0xFFF0))[0]
        out += [p2, p1]
        for b in data[at + 4:at + 0x13]:
            for nib in (b >> 4, b & 15):
                n = nib - 16 if nib >= 8 else nib
                s = s16(((n << 12) >> shift) + p1 * XAS_C1[ci] + p2 * XAS_C2[ci])
                out.append(s)
                p2, p1 = p1, s
        if len(out) >= nsamples:
            break
    return out[:nsamples]


ENG_SPAN, ENG_MATCH = 8, 32   # psp-viewer/audio.c's: neighbours cached, match window


def entry_table(pcm, grain):
    """The mixer's grain entry points (psp-viewer/audio.c eng_entry) for
    every grain and its ENG_SPAN neighbours either side: the offset into
    grain h whose next ENG_MATCH samples best match those past the end of
    grain g (normalised correlation, every second sample). On the PSP the
    search took 3.2 s a boot, and again at each car change; -1 where h is
    out of range. None without numpy (the game then searches at load)."""
    try:
        import numpy as np
    except ImportError:
        return None
    x = np.asarray(pcm, dtype=np.float32)
    n2, samples = len(grain) - 1, len(pcm)
    win = np.lib.stride_tricks.sliding_window_view(x, ENG_MATCH)   # win[i] = x[i:i+32]
    energy = 1 + np.einsum('ij,ij->i', win, win)
    out = np.full((n2, 2 * ENG_SPAN + 1), -1, dtype=np.int16)
    for g in range(n2):
        at = grain[g + 1]
        for d in range(-ENG_SPAN, ENG_SPAN + 1):
            h = g + d
            if h < 0 or h >= n2:
                continue
            if at + ENG_MATCH >= samples:
                out[g, d + ENG_SPAN] = 0
                continue
            b, length = grain[h], grain[h + 1] - grain[h]
            offs = np.arange(0, length, 2)
            offs = offs[b + offs + ENG_MATCH < samples]
            best = 0
            if len(offs):
                dot = win[b + offs] @ x[at:at + ENG_MATCH]
                best = int(offs[np.argmax(dot * np.abs(dot) / energy[b + offs])])
            out[g, d + ENG_SPAN] = best if best < 32767 else 0
    return out


def sweep(game, name, rms=None):
    """A Gnsu20 sweep -> an N2EN blob (header, rpm positions, grains, PCM);
    rms: its PCM scaled to that RMS (a coast sweep to its car's own). After
    them, when numpy is there, the grain entry table: 'N2EC', u32 span, u32
    grains, then grains x (2 span + 1) s16 (entry_table)."""
    d = archived(game, f'SOUND\\ENGINE\\{name}.gin')
    if d[:6] != b'Gnsu20':
        raise ValueError(f'{name}: not a Gnsu20 sweep')
    rpm_min, rpm_max, n1, n2, total, rate = struct.unpack_from('<2f4I', d, 8)
    pos = struct.unpack_from(f'<{n1+1}I', d, 0x20)
    grain = struct.unpack_from(f'<{n2+1}I', d, 0x20 + 4 * (n1 + 1))
    pcm = decode_xas(d[0x20 + 4 * (n1 + 1) + 4 * (n2 + 1):], total)
    own = math.sqrt(sum(v * v for v in pcm) / max(1, len(pcm)))
    if rms and own:
        pcm = [max(-32768, min(32767, round(v * rms / own))) for v in pcm]
    blob = struct.pack('<4s2f4I', b'N2EN', rpm_min, rpm_max, n1, n2, len(pcm), rate)
    blob += struct.pack(f'<{n1+1}I', *pos) + struct.pack(f'<{n2+1}I', *grain) + struct.pack(f'<{len(pcm)}h', *pcm)
    table = entry_table(pcm, grain)
    if table is not None:
        blob += struct.pack('<4s2I', b'N2EC', ENG_SPAN, n2) + table.tobytes()
    return blob, dict(rpm=(round(rpm_min), round(rpm_max)), rate=rate, samples=len(pcm), descending=pos[0] > pos[-1], rms=own)


# The car type (the GLOBALB 0x34600 records' order) -> the sound tables'
# car (the original's GetCarTypeMapping: a switch in the code, not data;
# read off the GameCube's Speed.elf). Types left out (the traffic's, the
# Lancer's) are its 18.
SND_CAR = {0: 22, 1: 10, 2: 6, 3: 0, 4: 19, 5: 5, 6: 23, 7: 7, 8: 14, 9: 21, 10: 9, 11: 31, 12: 28, 13: 4, 14: 17,
           15: 30, 16: 12, 17: 3, 18: 24, 19: 8, 20: 32, 21: 26, 22: 2, 23: 11, 24: 1, 25: 13, 26: 20, 27: 29,
           28: 18, 29: 25, 30: 15}
# a shift pattern's fields after its bank: i an int, f a float
SHIFT_FIELDS = 'ifiiiiif' 'iiiiiii' 'f' 'iiiiii' 'f'


from n2exe import Exe as Xbe   # the executable's data by virtual address (default.xbe or the PS2's SLUS_210.65)
import n2exe
# the strings the sound tables point at: the PS2 fills those tables in code
SOUND_ANCHORS = ('GIN_HondaS2000_Church.gin', 'GEAR_SML_Base.abk', 'TURBO_SML1_0_MB.abk', 'SWTN_CAR_00_MB.abk')


def sound_tables(xbe):
    """The original's car sound tables (EAXCar / SFXCTL_*), initialised data
    in the Xbox default.xbe (the GameCube fills the same in its static
    initialiser): g_SND_ENGINE_DATA (43 engines of 0x4c bytes: the AEMS
    bank, the GIN, the GIN's rpm range, the two layers' volumes and
    weights, the low-pass floor, the launch's row), g_SND_CARDATAMAPPING
    (33 cars x 4 levels: engine, shift pattern, turbo), g_SND_SHIFTPATTERNS
    (18 x 24 words), g_SND_ACCELFROMIDLE (12 x 5), g_SND_TURBODATASET (15 x
    6), g_SND_SWEETNERDATASET (43 x 3), the rivals' SND_AI_RPM_Lengths_FINE
    and SND_AI_DOWNSHIFT_RPMS (9 each, by gear: R, N, 1st..7th) and
    g_DOPPLER_PARAMS (6)."""
    b = xbe.b
    at = xbe.ref('CAR_00_ENG_MB_EE.abk')
    engines = []
    for k in range(43):
        w, f = struct.unpack_from('<19I', b, at + 0x4c * k), struct.unpack_from('<19f', b, at + 0x4c * k)
        gin = xbe.cstr(w[3])
        if not gin.startswith('GIN_') or not gin.endswith('.gin') or w[2] > 60:
            raise ValueError(f'g_SND_ENGINE_DATA row {k}: {gin}')
        engines.append(dict(gin=gin[:-4], aems=w[2], idle=w[6], gin_max=f[7], gin_min=f[8], vol_aems=w[9], vol_gin=w[10],
                            drpm=f[11], weights=f[12:18], lp_min=w[18]))
    # 33 cars x 4 levels x (engine, shift pattern, turbo): the first car (the 240SX) 18/0/0, 2/1/0, 20/2/1, 20/3/2
    at = xbe.find((18, 0, 0, 2, 1, 0, 20, 2, 1, 20, 3, 2), 'i')
    mapping = [[struct.unpack_from('<3i', b, at + 48 * c + 12 * lv) for lv in range(4)] for c in range(33)]
    if any(e >= 43 or s >= 18 or t >= 15 for row in mapping for e, s, t in row):
        raise ValueError('g_SND_CARDATAMAPPING out of range')
    at = xbe.ref('GEAR_SML_Base.abk')
    shifts = []
    for k in range(18):
        w = struct.unpack_from('<I', b, at + 96 * k)[0]
        vals = [struct.unpack_from('<' + t, b, at + 96 * k + 4 * (i + 1))[0] for i, t in enumerate(SHIFT_FIELDS)]
        shifts.append(dict(bank=xbe.cstr(w)[:-4], v=vals))
    at = xbe.find((3500, 500, 800, 1200), 'i') - 4
    idle = [struct.unpack_from('<f4i', b, at + 20 * k) for k in range(12)]
    at = xbe.ref('TURBO_SML1_0_MB.abk')
    turbos = [(xbe.cstr(struct.unpack_from('<I', b, at + 24 * k)[0])[:-4], *struct.unpack_from('<3i2f', b, at + 24 * k + 4))
              for k in range(15)]
    at = xbe.ref('SWTN_CAR_00_MB.abk')
    sweet = [(xbe.cstr(struct.unpack_from('<I', b, at + 12 * k)[0])[:-4], *struct.unpack_from('<2i', b, at + 12 * k + 4))
             for k in range(43)]
    lengths = struct.unpack_from('<9f', b, xbe.find((40., 40., 40.5, 21., 12., 6.5, 3.6, 2., 1.), 'f'))
    down = struct.unpack_from('<9f', b, xbe.find((6000., 6000., 5700., 6200.), 'f'))
    doppler = struct.unpack_from('<6f', b, xbe.find((.35, .33, .6), 'f'))
    return dict(engines=engines, mapping=mapping, shifts=shifts, idle=idle, turbos=turbos, sweet=sweet,
                ai_lengths=lengths, ai_down=down, doppler=doppler)


def car_types(game):
    d = archived(game, 'GLOBAL/GLOBALB.BUN')
    from n2formats import chunks
    t = [d[p:p + n] for k, p, n in chunks(d) if k == 0x34600][0][8:]
    return [t[a:a + 32].split(b'\0')[0].decode() for a in range(0, len(t), 0x890)]


def xa_bytes(d, at, samples):
    """The bytes of samples of EA-XA v2 frames from at (28 a frame; 0xEE: raw)."""
    n, p = 0, at
    while n < samples:
        p += 61 if d[p] == 0xEE else 15
        n += 28
    return p - at


def cycle_rpm(pcm, rate, want):
    """The revs an engine loop was recorded at: its waveform's period (one
    four-stroke cycle, two turns: the original reads the AEMS revs back as
    the GIN's cycle rate x 120), the octave nearest want (the strongest
    repeat may be a firing's, a quarter of a four's cycle, or two cycles)."""
    import numpy as np
    x = np.asarray(pcm, np.float32)
    x -= x.mean()
    c = np.fft.irfft(np.abs(np.fft.rfft(x)) ** 2, len(x))
    lo, hi = int(rate * 120 / 12000), min(int(rate * 120 / 500), len(x) // 2 - 2)
    c = c[:hi + 2] / max(c[0], 1e-9)
    peaks = [k for k in range(lo, hi) if c[k] > c[k - 1] and c[k] >= c[k + 1]]
    k = max(peaks, key=lambda k: c[k])
    rpm = rate * 120 / k
    return rpm * 2 ** round(math.log2(want / rpm))


def aems_bank(game, n):
    """aems-NN.n2ab: an AEMS engine bank (SOUND\\ENGINE\\CAR_NN_ENG_MB_SPU,
    the 'CAR' patch's eight EA-XA v2 loops, 24 kHz): 1 idle, 2-4 low, mid
    and high off the throttle, 5-7 the same on it (brighter), 8 the limiter
    (not in every bank).
    The patch (AEMS bytecode) sets their revs: its three points (the blocks
    of 16171, rpm, rpm), each loop one engine cycle long or a multiple, so
    its own revs come from its period. 'N2AB', u32 version 1, u32 loops,
    f32 points[3], per loop u32 rate, u32 samples, f32 revs, u32 offset, u32
    bytes; then the frames."""
    d = archived(game, f'SOUND\\ENGINE\\CAR_{n:02d}_ENG_MB_SPU.abk')
    bnk = struct.unpack_from('<I', d, 0x18)[0]
    count = struct.unpack_from('<H', d, bnk + 6)[0]
    blocks = []   # (offset, rpm) of each block of the patch
    for at in range(0x20, bnk - 12, 4):
        a, x, y = struct.unpack_from('<3i', d, at)
        if a == 16171 and x == y:
            blocks.append((at, x))
    groups, last = [], -1 << 20
    for at, v in blocks:   # a group: blocks close together
        if at - last > 0x100:
            groups.append([])
        groups[-1].append(v)
        last = at
    loops = []
    for i in range(1, count):
        f = pt_header(d, bnk + 0x14 + 4 * i + struct.unpack_from('<I', d, bnk + 0x14 + 4 * i)[0])
        if f['platform'] == 5:      # the PS2's VAG loops, as XA for the port's mixer
            pcm = stream_pcm(d, bnk, f, f'CAR_{n:02d}')
            xa = encode_xa(pcm)
            loops.append((f.get(0x84, 24000), len(pcm), xa, len(xa), decode_xa(xa, 0, len(xa), len(pcm), [0, 0])))
            continue
        if f.get(0xA0) != 10:
            raise ValueError(f'CAR_{n:02d} loop {i}: codec {f.get(0xA0)}')
        at, ns, rate = bnk + f[0x88], f[0x85], f.get(0x84, 24000)
        loops.append((rate, ns, at, xa_bytes(d, at, ns), decode_xa(d, at, len(d), ns, [0, 0])))
    if len(loops) == 6 and d[4] == 1:   # the PS2's banks (ABKC v1) have no limiter loop: CAR_02's six, its last twice
        loops.append(loops[-1])
    if len(loops) not in (7, 8):   # 7: no limiter loop (CAR_02, and every bank of the PS2's)
        raise ValueError(f'CAR_{n:02d}: {len(loops)} loops')
    # the three points: the group of three revs (the other holds its mid and high again)
    trio = next((sorted(g) for g in groups if len([v for v in g if v > 1]) == 3), None)
    if not trio:
        trio = [cycle_rpm(lp[4], lp[0], w) for lp, w in zip(loops[1:4], (2000, 4000, 7000))]
    # the idle's octave near the low point (at 1 500, the 206's idle, its
    # firing then at the sweep's 50 Hz, as the PS2's)
    want = [trio[0] * .75, *trio, *trio, trio[2]][:len(loops)]
    revs = [cycle_rpm(lp[4], lp[0], w) for lp, w in zip(loops, want)]
    head = struct.pack('<4s2I3f', b'N2AB', 1, len(loops), *trio)
    data, table = bytearray(), b''
    for (rate, ns, at, size, _), r in zip(loops, revs):
        table += struct.pack('<2If2I', rate, ns, r, len(data), size)
        data += at if isinstance(at, bytes) else d[at:at + size]   # (the PS2's: the XA itself)
    return head + table + data, dict(points=trio, revs=[round(r) for r in revs], bytes=len(data))


def gear_bank(game, name):
    """gear-<bank>.n2sf: a gearbox's two shift sounds (GEAR_<size>_<level>:
    48 kHz, kept at 24), 'up' and 'down' (the bank's order)."""
    streams = [s for s in bank_streams(game, f'SOUND\\SHIFTING\\{name}.abk') if s]
    blob, entries = bytearray(), []
    for tag, (rate, pcm, ch) in zip(('up', 'down'), streams):
        pcm = [sum(pcm[i:i + 2]) // 2 for i in range(0, len(pcm) - 1, 2)]
        entries.append((tag.encode().ljust(16, b'\0'), rate // 2, len(pcm), 0, len(blob)))
        blob += struct.pack(f'<{len(pcm)}h', *pcm)
    return struct.pack('<4sI', b'N2SF', len(entries)) + b''.join(struct.pack('<16s4I', *e) for e in entries) + blob


TURBO_SOUNDS = (('turbo', True), ('turbo_bov1', False), ('turbo_bov', False), ('turbo_bov3', False), ('turbo_hiss', True))


def turbo_bank(game, name):
    """turbo-<bank>.n2sf: a turbo's five sounds (TURBO_<size>_<level>_MB,
    48 kHz, kept at 24 like sfx.n2sf's): the whistle's loop, the three
    blow-offs, the hiss's loop, under the names sfx.n2sf gives the medium
    one's (audio_turbo_load swaps them in)."""
    streams = bank_streams(game, f'SOUND\\TURBO\\{name}.abk')
    blob, entries = bytearray(), []
    for (tag, loop), (rate, pcm, ch) in zip(TURBO_SOUNDS, streams[1:6]):
        pcm = [sum(pcm[i:i + 2]) // 2 for i in range(0, len(pcm) - 1, 2)]
        if loop:
            pcm = seamless_loop(pcm)
        entries.append((tag.encode().ljust(16, b'\0'), rate // 2, len(pcm), int(loop), len(blob)))
        blob += struct.pack(f'<{len(pcm)}h', *pcm)
    return struct.pack('<4sI', b'N2SF', len(entries)) + b''.join(struct.pack('<16s4I', *e) for e in entries) + blob


def car_sound(game, out):
    """The cars' engines as the original picks and mixes them (EAXCar::
    CarPreSetup): carsnd.n2cs, the tables (sound_tables) with the car types'
    order; engine-<GIN>.n2en, each engine's Ginsu sweep; aems-NN.n2ab, the
    AEMS banks; gear-<bank>.n2sf, the shift sounds; turbo-<bank>.n2sf, the
    turbos. engine.n2en: the 350Z's stock engine (Rachel's car, and the
    boot's).
    carsnd.n2cs: 'N2CS', u32 version 1; u32 types, then per car type char
    model[16], u32 sound car; u32 cars (33), per car 4 levels of u8 engine,
    shift pattern, turbo, pad; u32 engines (43), per engine char gin[32]
    (the file's stem), u8 aems bank, u8 launch row, u16 low-pass floor
    (Hz), f32 GIN max and min rpm, AEMS and GIN volume (/32767), revs'
    change for the full sweep, weights (AEMS, GIN) accelerating, steady and
    off the throttle; u32 patterns (18), per pattern char bank[16], f32
    fields[23] (g_SND_SHIFTPATTERNS' after the bank); u32 launch rows (12),
    f32 x 5 (volume, revs up, attack, release, lift ms); u32 turbos (15),
    char bank[24], f32 spool, blow-off 1, blow-off 2/3 volumes, the top
    pressure, its fall; u32 sweeteners (43), char bank[16], f32 x 2; f32
    rivals' revs steps[9], downshift revs[9], Doppler[6]."""
    t = sound_tables(n2exe.of(game, SOUND_ANCHORS))
    types = car_types(game)
    blob = struct.pack('<4s2I', b'N2CS', 1, len(types))
    for k, name in enumerate(types):
        blob += struct.pack('<16sI', name.encode(), SND_CAR.get(k, 18))
    blob += struct.pack('<I', len(t['mapping']))
    for row in t['mapping']:
        blob += b''.join(struct.pack('<4B', e, s, tb, 0) for e, s, tb in row)
    blob += struct.pack('<I', len(t['engines']))
    for e in t['engines']:
        blob += struct.pack('<32s2BH5f6f', e['gin'][4:].encode(), e['aems'], e['idle'], e['lp_min'], e['gin_max'], e['gin_min'],
                            e['vol_aems'] / 32767, e['vol_gin'] / 32767, e['drpm'], *e['weights'])
    blob += struct.pack('<I', len(t['shifts']))
    for s in t['shifts']:
        blob += struct.pack('<16s23f', s['bank'].encode(), *s['v'])
    blob += struct.pack('<I', len(t['idle'])) + b''.join(struct.pack('<5f', *r) for r in t['idle'])
    blob += struct.pack('<I', len(t['turbos'])) + b''.join(struct.pack('<24s5f', r[0].encode(), *r[1:]) for r in t['turbos'])
    blob += struct.pack('<I', len(t['sweet'])) + b''.join(struct.pack('<16s2f', r[0].encode(), *r[1:]) for r in t['sweet'])
    blob += struct.pack('<9f9f6f', *t['ai_lengths'], *t['ai_down'], *t['doppler'])
    (out / 'carsnd.n2cs').write_bytes(blob)
    report = dict(gins={}, banks={})
    gins = sorted({e['gin'] for e in t['engines']})

    def one_gin(g):
        data, info = sweep(game, g)
        (out / f'engine-{g[4:]}.n2en').write_bytes(data)
        return g, dict(rpm=info['rpm'], bytes=len(data))
    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        report['gins'] = dict(pool.map(lambda g: mac_slots.call('convert_audio', one_gin, g), gins))
    for n in sorted({e['aems'] for e in t['engines']}):
        data, info = aems_bank(game, n)
        (out / f'aems-{n:02d}.n2ab').write_bytes(data)
        report['banks'][n] = info
    for s in sorted({s['bank'] for s in t['shifts']}):
        (out / f'gear-{s}.n2sf').write_bytes(gear_bank(game, s))
    for tb in sorted({t['turbos'][tb][0] for row in t['mapping'] for _, _, tb in row}):
        (out / f'turbo-{tb}.n2sf').write_bytes(turbo_bank(game, tb))   # the turbos the cars use (no BIG_*)
    # the 350Z's stock engine: Rachel's car and the boot's
    z = t['engines'][t['mapping'][SND_CAR[types.index('350Z')]][0][0]]['gin']
    shutil.copyfile(out / f'engine-{z[4:]}.n2en', out / 'engine.n2en')
    for old in list(out.glob('engine-*-dcl.n2en')) + [out / 'engine-dcl.n2en']:
        old.unlink(missing_ok=True)   # the coast sweeps the port crossed in before (the original never does)
    report['cars'] = {name: [t['engines'][t['mapping'][SND_CAR.get(k, 18)][lv][0]]['gin'] for lv in range(4)]
                      for k, name in enumerate(types[:31])}
    return report


def pt_header(b, at):
    """'PT' + u16 platform, then TLVs: u8 tag, u8 length, big-endian value."""
    if b[at:at+2] != b'PT':
        raise ValueError('missing PT header')
    p, fields = at + 4, {'platform': struct.unpack_from('<H', b, at + 2)[0]}
    while p < len(b):
        tag = b[p]
        p += 1
        if tag == 0xFF:
            break
        if tag in (0xFC, 0xFD, 0xFE):
            continue
        n = b[p]
        p += 1
        fields[tag] = int.from_bytes(b[p:p+n], 'big')
        p += n
    return fields


def decode_ima(data, nsamples):
    """Xbox IMA: 36-byte mono blocks: s16 predictor, u8 index, pad, then
    32 bytes = 64 nibbles, low nibble first."""
    out = []
    for at in range(0, len(data) - 35, 36):
        pred, idx = struct.unpack_from('<hB', data, at)
        idx = min(idx, 88)
        for b in data[at + 4:at + 36]:
            for nib in (b & 15, b >> 4):
                step = IMA_STEP[idx]
                diff = step >> 3
                if nib & 1: diff += step >> 2
                if nib & 2: diff += step >> 1
                if nib & 4: diff += step
                pred = s16(pred - diff if nib & 8 else pred + diff)
                idx = max(0, min(88, idx + IMA_INDEX[nib]))
                out.append(pred)
        if len(out) >= nsamples:
            break
    return out[:nsamples]


VAG_COEF = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def decode_vag(data, nsamples):
    """PS2 SPU ADPCM (the PS2 disc's banks, PT platform 5 with no codec
    field): 16-byte frames of 28 samples, u8 filter << 4 | shift, u8
    flags, 14 bytes of nibbles low first."""
    out, h1, h2 = [], 0, 0
    for at in range(0, len(data) - 15, 16):
        f0, f1 = VAG_COEF[min(data[at] >> 4, 4)]
        shift = data[at] & 15
        for b in data[at + 2:at + 16]:
            for nib in (b & 15, b >> 4):
                v = ((nib << 12) - (0x10000 if nib & 8 else 0)) >> shift
                v = max(-32768, min(32767, v + ((h1 * f0 + h2 * f1 + 32) >> 6)))
                h1, h2 = v, h1
                out.append(v)
        if len(out) >= nsamples:
            break
    return out[:nsamples]


def encode_xa(pcm):
    """EA-XA v2 frames (decode_xa's), for the PS2's VAG banks the port
    plays as XA: per 28 samples the filter and shift with the least error."""
    out, p1, p2 = bytearray(), 0, 0
    for at in range(0, len(pcm), 28):
        x = list(pcm[at:at + 28]) + [0] * max(0, at + 28 - len(pcm))
        best = None
        for ci in range(4):
            q1, q2, peak = p1, p2, 0
            for v in x:   # the residual's size with the ideal history, for the shift
                peak = max(peak, abs(v * 256 - q1 * XA1[ci] - q2 * XA2[ci]))
                q1, q2 = v, q1
            shift = 8
            while shift < 20 and (peak >> shift) > 7:
                shift += 1
            for sh in {shift, max(8, shift - 1)}:
                q1, q2, err, nibs = p1, p2, 0, []
                for v in x:
                    pred = q1 * XA1[ci] + q2 * XA2[ci] + 0x80
                    n = max(-8, min(7, round((v * 256 - pred) / (1 << sh))))
                    y = max(-32768, min(32767, ((n << sh) + pred) >> 8))
                    err += (y - v) ** 2
                    nibs.append(n & 15)
                    q1, q2 = y, q1
                if best is None or err < best[0]:
                    best = (err, ci, sh, nibs, q1, q2)
        _, ci, sh, nibs, p1, p2 = best
        out.append(ci << 4 | (20 - sh))
        out += bytes(nibs[k] << 4 | nibs[k + 1] for k in range(0, 28, 2))
    return bytes(out)


def stream_pcm(d, base, f, path):
    """One bank stream's samples, mono: Xbox IMA (codec 0x14), EA-XA (10)
    or, on the PS2's platform 5 without a codec, VAG; a stream of more
    channels is their mean (channel n's data at PT field 0x88 + n)."""
    codec, ns, ch = f.get(0xA0, 0), f.get(0x85, 0), f.get(0x82, 1)
    chans = []
    for c in range(ch):
        data = d[base + f.get(0x88 + c, 0):]
        if codec == 0x14:
            chans.append(decode_ima(data, ns))
        elif codec == 10:
            chans.append(decode_xa(d, base + f.get(0x88 + c, 0), len(d), ns, [0, 0]))
        elif f.get('platform') == 5 and codec in (0, 5):
            chans.append(decode_vag(data, ns))
        else:
            raise ValueError(f'{path}: codec {codec:#x} not handled for SFX')
    return [sum(v) // len(v) for v in zip(*chans)] if len(chans) > 1 else chans[0]


def resample(pcm, src, dst):
    """Linear resampling (the PS2's banks to the rate a table needs)."""
    if src == dst or not pcm:
        return list(pcm)
    n = max(1, len(pcm) * dst // src)
    out = []
    for i in range(n):
        x = i * src / dst
        k = int(x)
        a, b = pcm[min(k, len(pcm) - 1)], pcm[min(k + 1, len(pcm) - 1)]
        out.append(int(a + (b - a) * (x - k)))
    return out


def bank_streams(game, path):
    d = archived(game, path)
    if d[:4] != b'ABKC':
        raise ValueError(f'{path}: not ABKC')
    bnk = struct.unpack_from('<I', d, 0x18)[0]
    if d[bnk:bnk+4] != b'BNKl':
        raise ValueError(f'{path}: BNKl not found')
    count = struct.unpack_from('<H', d, bnk + 6)[0]
    streams = []
    for i in range(count):
        slot = bnk + 0x14 + 4 * i
        off = struct.unpack_from('<I', d, slot)[0]
        if not off:
            streams.append(None)
            continue
        f = pt_header(d, slot + off)
        rate = f.get(0x84, 24000)
        if f['platform'] == 5:      # the PS2's: VAG, its channels mixed down
            streams.append((rate, stream_pcm(d, bnk, f, path), 1))
            continue
        codec, ns = f.get(0xA0, 0), f.get(0x85, 0)
        data = d[bnk + f.get(0x88, 0):]
        if codec == 0x14:
            pcm = decode_ima(data, ns)
        else:
            raise ValueError(f'{path}: codec {codec:#x} not handled for SFX')
        streams.append((rate, pcm, f.get(0x82, 1)))
    return streams


XA1, XA2 = (0, 240, 460, 392), (0, 0, -208, -220)
SONG_RMS = -14.0      # dBFS: louder tracks are songs, the rest ambient loops
MUSIC_TARGET = -10.0  # dBFS RMS every song is brought to


def decode_xa(d, at, end, samples, hist):
    """One channel of an EA-XA v2 block (as psp-viewer/audio.c)."""
    out, (p1, p2) = [], hist
    while len(out) < samples and at < end:
        b0 = d[at]
        if b0 == 0xEE:
            p1, p2 = struct.unpack_from('>2h', d, at + 1)
            out += struct.unpack_from('>28h', d, at + 5)[:samples - len(out)]
            if len(out) >= 2:
                p1, p2 = out[-1], out[-2]
            at += 61
            continue
        ci, shift = b0 >> 4 & 3, 20 - (b0 & 15)
        for k in range(min(28, samples - len(out))):
            nib = d[at + 1 + k // 2] >> (0 if k & 1 else 4) & 15
            nib -= 16 if nib >= 8 else 0
            v = max(-32768, min(32767, ((nib << shift) + p1 * XA1[ci] + p2 * XA2[ci] + 0x80) >> 8))
            out.append(v)
            p1, p2 = v, p1
        at += 15
    hist[:] = [p1, p2]
    return out


def track_rms(data, at, every=20):
    """RMS (dBFS) of a track's left channel, from every n-th data block."""
    total, count, k = 0, 0, 0
    while at + 8 <= len(data):
        tag, n = data[at:at+4], struct.unpack_from('<I', data, at + 4)[0]
        if n < 8 or tag == b'SCEl':
            break
        if tag == b'SCDl':
            if k % every == every // 2:
                samples, o0 = struct.unpack_from('>2I', data, at + 8)
                x = decode_xa(data, at + 20 + o0, at + n, samples, [0, 0])
                total += sum(v * v for v in x)
                count += len(x)
            k += 1
        at += n
    return 20 * math.log10(max(1, (total / max(1, count)) ** .5) / 32768)


def seamless_loop(pcm, n=1024):
    """A loop whose last sample does not meet its first clicks on every
    pass (nitrous jumped from -11972 to 458). Crossfade the start into the
    end and loop from just after it: the end now runs into pcm[n - 1] and
    the next pass starts at pcm[n]."""
    if len(pcm) < 4 * n:
        return list(pcm)
    head, body = pcm[:n], list(pcm[n:])
    for i in range(n):
        t = (i + .5) / n
        body[-n + i] = max(-32768, min(32767, round(body[-n + i] * (1 - t) + head[i] * t)))
    return body


def speech(game, out):
    """speech.big and its index, speech.n2sp. The game's own tables:
    - UG2_SPEECH.EVT: u32 at +4 the offset of the event names, 32 bytes
      each (name offset, 0x721e | hash << 16), their strings after them:
      Career, Career_Magazine, Career_Dvd, Outrun, Time, FE_Screen,
      FE_OptionScreen, Intro_Area, Intro_Events, FE_Dynoshop; u16 count at
      +0x10, u16 record offsets (x4) at +0x18, in the names' order. A
      record: its rules, each with the parameters' sources (the game's
      request is the event's values in order; a line plays when each of
      its parameters is a bit set in the value of its source); byte 28 the
      event's bank tag (the .CSI lists the same events in another order);
    - UG2_SPEECH.IDX: u32 1, u32 banks at +4 and +0x54, from +0x58 16 bytes
      a bank (id, table size, table offset, offset in the .big). A table:
      byte 0 the event's bank tag, byte 2 the bank's number for an event
      with several (Outrun: 1-5), byte 4 the event's parameters (n, 2-4),
      byte 5 the lines, byte 7 the unit of the line starts (256 x (it +
      1) bytes), u16 at 8 the end of the last line, then from 12 a line
      each 2 + n bytes: u16 big-endian start, then the line's parameters
      (bit numbers: which screen, outrun voice and outcome...).
    (The game's selection: 0x281b40 and 0x280730 in the Xbox default.xbe.)
    speech.n2sp: 'N2SP', u32 version 1, u32 events, u32 lines; events: char
    name[16], u16 first line, u16 lines; lines (16 bytes): u32 offset in
    speech.big, u32 samples, u8 bank, u8 parameters, u8 parameter[4], 2
    pad."""
    z = (game / 'ZDIR.BIN').read_bytes()
    entry = [e for e in struct.iter_unpack('<6I', z) if e[0] == game_hash(r'SOUND\SPEECH\UG2_SPEECH.BIG')][0]
    _, arc, sector, _, size, _ = entry
    with (game / f'ZZDATA{arc}.BIN').open('rb') as src, (out / 'speech.big').open('wb') as dst:
        src.seek(sector * 2048)
        left = size
        while left:
            chunk = src.read(min(left, 1 << 22))
            dst.write(chunk)
            left -= len(chunk)
    big = (out / 'speech.big').read_bytes()
    evt, idx = (archived(game, rf'SOUND\SPEECH\UG2_SPEECH.{e}') for e in ('EVT', 'IDX'))
    nev = struct.unpack_from('<H', evt, 0x10)[0]
    table_at = struct.unpack_from('<I', evt, 4)[0]
    names = [evt[table_at + 32 * nev + struct.unpack_from('<I', evt, table_at + 32 * k)[0]:].split(b'\0')[0].decode()
             for k in range(nev)]
    tags = [evt[4 * o + 28] for o in struct.unpack_from(f'<{nev}H', evt, 0x18)]
    banks = [struct.unpack_from('<4I', idx, 0x58 + 16 * k) for k in range(struct.unpack_from('<I', idx, 0x54)[0])]
    lines_of = {k: [] for k in range(nev)}
    for b, (_, tsize, toff, base) in enumerate(banks):
        t = idx[toff:toff + tsize]
        ev = tags.index(t[0])
        np, unit = t[4] & 0x7f, 256 * (t[7] + 1)
        for i in range(t[5]):
            rec = t[12 + (2 + np) * i:12 + (2 + np) * (i + 1)]
            start = base + struct.unpack_from('>H', rec)[0] * unit
            if big[start:start + 4] != b'SCHl':
                raise ValueError(f'speech bank {b} line {i}: no SCHl at {start:#x}')
            samples = pt_header(big, start + 8)[0x85]
            lines_of[ev].append((start, samples, b, np, *(list(rec[2:]) + [0] * 4)[:4]))
    source_codec = 'EA-XA'
    if n2source.platform(game) == 'ps2':
        # The runtime's speech contract stays mono EA-XA/32 kHz. Decode the
        # PS2's version-3 MicroTalk (with PCM blocks) entirely offline.
        from n2microtalk import Decoder
        converted, cache = bytearray(), {}
        with Decoder() as decoder:
            for event_lines in lines_of.values():
                for i, line in enumerate(event_lines):
                    start, ns, *params = line
                    if start not in cache:
                        fields = pt_header(big, start + 8)
                        if fields.get('platform') != 5 or fields.get(0xA0) != 4 or fields.get(0x80) != 3 or fields.get(0x82, 1) != 1:
                            raise ValueError(f'unsupported PS2 speech header at {start:#x}: {fields}')
                        rate = fields.get(0x84, 22050)
                        at = start
                        while True:
                            if at + 8 > len(big):
                                raise ValueError('truncated PS2 speech block')
                            size = struct.unpack_from('<I', big, at + 4)[0]
                            if size < 8 or at + size > len(big):
                                raise ValueError('invalid PS2 speech block size')
                            end = big[at:at + 4] == b'SCEl'
                            at += size
                            if end:break
                        pcm = resample(decoder.decode(big[start:at], ns), rate, 32000)
                        xa = decoder.encode_xa(pcm)
                        offset = len(converted)
                        converted += struct.pack('<4sI', b'SCHl', 8)
                        # Blocks stay far below audio.c SP_BLOCK/SP_SAMPLES.
                        for frame in range(0, (len(pcm) + 27)//28, 64):
                            chunk = xa[frame*15:(frame+64)*15]
                            samples = min(64*28, len(pcm)-frame*28)
                            converted += struct.pack('<4s3I', b'SCDl', 16+len(chunk), samples, 0) + chunk
                        converted += struct.pack('<4sI', b'SCEl', 8)
                        cache[start] = (offset, len(pcm))
                    event_lines[i] = (*cache[start], *params)
        (out / 'speech.big').write_bytes(converted)
        size = len(converted)
        source_codec = 'PS2 MicroTalk v3/22050 Hz -> EA-XA/32000 Hz (offline)'
    blob, table, n = bytearray(), bytearray(), 0
    for k in range(nev):
        table += struct.pack('<16s2H', names[k].encode(), n, len(lines_of[k]))
        for line in lines_of[k]:
            blob += struct.pack('<2I6B2x', *line)
        n += len(lines_of[k])
    (out / 'speech.n2sp').write_bytes(struct.pack('<4s3I', b'N2SP', 1, nev, n) + table + blob)
    return dict(events={names[k]: len(lines_of[k]) for k in range(nev)}, lines=n, bytes=size, source_codec=source_codec,
                seconds=round(sum(line[1] for v in lines_of.values() for line in v) / 32000))


SKID_BANKS = {   # the slide's, the spin's and the lock's loops (as sfx.n2sf's SKID_PAV ones): the drift banks' slide their own long loop
    'SKID_PAV_MB': (1, 2, 5), 'SKID_PAV2_MB': (1, 2, 5), 'SKIDS_DRIFT_MB': (4, 2, 5), 'SKIDS_DRIFT2_MB': (4, 2, 5)}


def skid_banks(game, out):
    """skid-<bank>.n2sf: each skids bank's three loops under sfx.n2sf's
    names (audio_sfx_swap), at 24 kHz: the big cars' (SKID_PAV2) and the
    drift event's (SKIDS_DRIFT, SKIDS_DRIFT2), CARSFX_Skids::SetupLoadData."""
    for name, slots in SKID_BANKS.items():
        streams = bank_streams(game, f'SOUND\\SKIDS\\{name}.abk')
        blob, entries = bytearray(), []
        for tag, slot in zip(('skid', 'skid_spin', 'skid_lock'), slots):
            rate, pcm, ch = streams[slot]
            pcm = seamless_loop([sum(pcm[i:i + 2]) // 2 for i in range(0, len(pcm) - 1, 2)])
            entries.append((tag.encode().ljust(16, b'\0'), rate // 2, len(pcm), 1, len(blob)))
            blob += struct.pack(f'<{len(pcm)}h', *pcm)
        (out / f'skid-{name}.n2sf').write_bytes(struct.pack('<4sI', b'N2SF', len(entries)) + b''.join(struct.pack('<16s4I', *e) for e in entries) + blob)
    return len(SKID_BANKS)


# The PS2's banks that differ from the Xbox's order: ROADNOISE has no
# slot 4 (the Xbox's 1.05 s noise; its neighbour 3 plays instead), the
# later ones one lower.
PS2_SLOTS = {r'SOUND\IG_GLOBAL\ROADNOISE_00_MB.abk': lambda s: s if s < 4 else 3 if s == 4 else s - 1}


def ps2_slot(game, path, index):
    import n2source
    if n2source.platform(game) == 'ps2' and path in PS2_SLOTS:
        return PS2_SLOTS[path](index)
    return index


def write_sfx(game, out):
    """sfx.n2sf: the SFX table's one-shots and loops (a loop made seamless)."""
    blob, entries, cache = bytearray(), [], {}
    report = {}
    for name, (path, index, loop, *down) in SFX.items():
        if path not in cache:
            cache[path] = bank_streams(game, path)
        streams = cache[path]
        index = ps2_slot(game, path, index)
        rate, pcm, ch = streams[index]
        if rate < 4000:     # the PS2's spark chatter: 84 samples "at 100 Hz", no sound of its own (a gap: silence)
            rate, pcm = 22050, [0] * 2205
        if ch != 1:
            raise ValueError(f'{name}: {ch} channels; the mixer plays mono SFX only')
        if down and (rate // down[0] >= 16000 or n2source.platform(game) != 'ps2'):   # every down-th sample, each the mean of its group (a box filter against the fold-back); the PS2's lower rates kept
            k = down[0]
            pcm = [sum(pcm[i:i + k]) // k for i in range(0, len(pcm) - k + 1, k)]
            rate //= k
        if loop:
            pcm = seamless_loop(pcm)
        entries.append((name.encode().ljust(16, b'\0'), rate, len(pcm), int(loop), len(blob)))
        blob += struct.pack(f'<{len(pcm)}h', *pcm)
        report[name] = dict(rate=rate, seconds=round(len(pcm) / rate, 2), channels=ch, bank_streams=sum(1 for x in streams if x))
    head = struct.pack('<4sI', b'N2SF', len(entries)) + b''.join(struct.pack('<16s4I', *e) for e in entries)
    (out / 'sfx.n2sf').write_bytes(head + blob)
    return report


def encode_ima(pcm):
    """decode_ima's blocks from s16 PCM (the predictor and step carried on)."""
    out, pred, idx = bytearray(), pcm[0] if pcm else 0, 0
    for at in range(0, len(pcm), 64):
        out += struct.pack('<hBx', pred, idx)
        nibs = []
        for x in (pcm[at:at + 64] + [pcm[-1]] * 64)[:64]:
            step, d, nib = IMA_STEP[idx], x - pred, 0
            if d < 0:
                nib, d = 8, -d
            diff = step >> 3
            for bit, part in ((4, step), (2, step >> 1), (1, step >> 2)):
                if d >= part:
                    nib |= bit
                    d -= part
                    diff += part
            pred = s16(pred - diff if nib & 8 else pred + diff)
            idx = max(0, min(88, idx + IMA_INDEX[nib]))
            nibs.append(nib)
        out += bytes(nibs[k] | nibs[k + 1] << 4 for k in range(0, 64, 2))
    return bytes(out)


STITCH_BANK = r'SOUND\IG_GLOBAL\MB_Stich_Collision.abk'
WOOSH_BANK = r'SOUND\IG_GLOBAL\MB_Stitch_Whoosh.abk'
WOOSH_TRIM = 26028   # g_WOOSHS: the same -2 dB for all 60 (filled by the static initializer)


def stitch_bank(game, path, rate=None):
    """A stitch bank's samples, kept as Xbox IMA blocks (one with six
    channels made its front pair's mean, encoded again): [(ima, samples)]
    and their rate."""
    bank = archived(game, path)
    bnk = struct.unpack_from('<I', bank, 0x18)[0]
    count = struct.unpack_from('<H', bank, bnk + 6)[0]
    samples = []
    for i in range(1, count):
        off = struct.unpack_from('<I', bank, bnk + 0x14 + 4 * i)[0]
        f = pt_header(bank, bnk + 0x14 + 4 * i + off)
        ns, ch = f[0x85], f.get(0x82, 1)
        if f['platform'] == 5:      # the PS2's VAG: decoded, at the collision bank's rate, IMA again
            rate = rate or f[0x84]
            pcm = resample(stream_pcm(bank, bnk, f, path), f[0x84], rate)
            samples.append((encode_ima(pcm), len(pcm)))
            continue
        if f.get(0xA0) != 0x14 or (rate and f[0x84] != rate):
            raise ValueError(f'stitch.n2st: {path} slot {i} not 36 kHz Xbox IMA')
        rate = f[0x84]
        size = (ns + 63) // 64 * 36
        if ch == 1:
            ima = bank[bnk + f[0x88]:bnk + f[0x88] + size]
        else:
            a, b = (decode_ima(bank[bnk + f[t]:bnk + f[t] + size], ns) for t in (0x88, 0x89))
            ima = encode_ima([(x + y) // 2 for x, y in zip(a, b)])
        samples.append((ima, ns))
    return samples, rate


def write_stitches(game, out):
    """stitch.n2st: the stitches as the original plays them
    (cSTICH_PlayBack). GLOBAL\\INGAMECOMMON.BUN holds two 0x8003B500
    blocks, the collisions (type 0: the impacts, the bottoming out and the
    landings, the road's bumps) and the wooshes (type 1: passing posts,
    trees, tunnels and cars), each stitch a 0x3B502 header (u32 name hash,
    u16 volume Q15, u16 id, u8 type, u8 layers) and a 0x3B503 run of
    16-byte layers (u8 sample, the bank's slot - 1; s8 bank, 0 the
    collision bank, 1 the woosh one; u16 volume Q15; u16 pitch Q12; u16
    start delay, ms; u16 azimuth (65536 a turn); s16 the volume's random
    cut; s16 the pitch's (taken off at random up to its size); u16 flags).
    Each stitch's trim beside it: g_COLLISION (s16 Q15 in default.xbe) for
    the collisions, g_WOOSHS' -2 dB for the wooshes. The collision bank's
    94 samples and then the woosh bank's 16 (both 36 kHz) stay Xbox IMA
    blocks, as on the disc; the woosh layers' samples count past the
    collision ones'.

    'N2ST', u16 samples, u16 stitches, u32 layers, u32 rate; per sample
    u32 data offset, u32 samples; per stitch u16 first layer, u8 layers,
    u8 type, u16 volume, u16 trim; the layers as above; the IMA data."""
    from n2formats import chunks
    d = archived(game, 'GLOBAL/INGAMECOMMON.BUN')
    blocks = [(p, s) for t, p, s in chunks(d) if t == 0x8003B500]
    coll, rate = stitch_bank(game, STITCH_BANK)
    woosh, _ = stitch_bank(game, WOOSH_BANK, rate)
    stitches, layers = [], bytearray()
    for kind, (bp, bs) in enumerate(blocks[:2]):
        head = None
        for t, p, s in chunks(d, bp, bp + bs):
            if t == 0x3B502:
                head = struct.unpack_from('<IHHBB', d, p)
            elif t == 0x3B503:
                n = s // 16
                if not head or head[4] != n or head[3] != kind:
                    raise ValueError('stitch.n2st: layers without their header')
                for k in range(n):
                    layer = bytearray(d[p + 16 * k:p + 16 * k + 16])
                    if layer[1] != kind:
                        raise ValueError('stitch.n2st: a layer off its block\'s bank')
                    if kind:
                        layer[0] += len(coll)   # the woosh bank after the collision one
                    layers += layer
                stitches.append((len(layers) // 16 - n, n, head[3], head[1]))
                head = None
    xbe = n2exe.of(game, SOUND_ANCHORS)
    ncoll = sum(1 for st in stitches if st[2] == 0)
    trims = struct.unpack_from(f'<{ncoll}h', xbe.b, xbe.find((23197,) * 6 + (19518, 13045, 16422, 14636, 26028), 'h'))
    trims += (WOOSH_TRIM,) * (len(stitches) - ncoll)
    samples, data = [], bytearray()
    for ima, ns in coll + woosh:
        samples.append((len(data), ns))
        data += ima
    used = max(layers[k] for k in range(0, len(layers), 16))
    if used >= len(samples):
        raise ValueError('stitch.n2st: a layer past the bank')
    blob = struct.pack('<4sHHII', b'N2ST', len(samples), len(stitches), len(layers) // 16, rate)
    blob += b''.join(struct.pack('<2I', *s) for s in samples)
    blob += b''.join(struct.pack('<HBBHH', first, n, typ, vol, trim) for (first, n, typ, vol), trim in zip(stitches, trims))
    (out / 'stitch.n2st').write_bytes(blob + layers + data)
    return dict(stitches=len(stitches), wooshes=len(stitches) - ncoll, layers=len(layers) // 16, samples=len(samples), rate=rate,
                bytes=len(blob) + len(layers) + len(data))


DRIVEBY_KINDS = {'LAMPPOST': 0, 'PILLAR': 0, 'TREE': 1}   # the wooshes' two sets (the 7 FOUNTAINs play none)
DRIVEBY_CELL = 64.


def write_driveby(game, out):
    """driveby.n2db: the world's sound spheres the player's car whooshes
    past (EAXTunerCar::ProcessSoundSphere): TRACKS\\STREAML4RA.BUN's
    0x80036000 packs, one a streaming section (0x36001 a header, 0x36002
    an AABB tree, 0x36003 32-byte triggers: u32 instance hash, u32 event
    0x10007, u32 bStringHash of the kind, u32 3, f32 x, y, z, f32 radius,
    12 m in all). Kept: the lamp posts, pillars and trees, sorted into
    64 m cells for the lookup.

    'N2DB', u32 version 1, u32 events, f32 radius, f32 cell, f32 x0, y0,
    u16 columns, rows; u16 first event per cell (+1: the end, padded to
    4 bytes); per event
    f32 x, y, z (the game's axes), u8 kind (0 post or pillar, 1 tree), 3
    pad."""
    from n2formats import chunks
    from n2world import Stream

    def bhash(name):
        h = 0xffffffff
        for c in name.encode():
            h = (h * 33 + c) & 0xffffffff
        return h
    kinds = {bhash(k): v for k, v in DRIVEBY_KINDS.items()}
    st, events, radius = Stream(game), [], set()
    for name in sorted(st.sections):
        d = st.section(name)
        for t, p, n in chunks(d):
            if t != 0x80036000:
                continue
            for t1, q, m in chunks(d, p, p + n):
                if t1 != 0x36003:
                    continue
                a = (q + 15) & ~15   # the loader aligns each payload to 16
                for r in range(a, q + m - 31, 32):
                    _, ev, kind, _, x, y, z, rad = struct.unpack_from('<4I4f', d, r)
                    if ev == 0x10007 and kind in kinds:
                        events.append((x, y, z, kinds[kind]))
                        radius.add(round(rad, 3))
    if len(radius) != 1:
        raise ValueError(f'driveby.n2db: radii {radius}')
    events = sorted(set(events))
    x0, y0 = min(e[0] for e in events) - 16, min(e[1] for e in events) - 16
    cols = int((max(e[0] for e in events) - x0) // DRIVEBY_CELL) + 1
    rows = int((max(e[1] for e in events) - y0) // DRIVEBY_CELL) + 1
    cell = lambda e: int((e[1] - y0) // DRIVEBY_CELL) * cols + int((e[0] - x0) // DRIVEBY_CELL)
    events.sort(key=cell)
    starts, k = [], 0
    for c in range(cols * rows + 1):
        while k < len(events) and cell(events[k]) < c:
            k += 1
        starts.append(k)
    blob = struct.pack('<4s2I4f2H', b'N2DB', 1, len(events), radius.pop(), DRIVEBY_CELL, x0, y0, cols, rows)
    blob += struct.pack(f'<{len(starts)}H', *starts) + b'\0' * (len(starts) % 2 * 2)   # (the events 4-aligned)
    blob += b''.join(struct.pack('<3fB3x', *e) for e in events)
    (out / 'driveby.n2db').write_bytes(blob)
    return dict(events=len(events), cells=cols * rows, bytes=len(blob),
                kinds={k: sum(1 for e in events if e[3] == v) for k, v in (('posts', 0), ('trees', 1))})


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--out', type=Path, default=ROOT / 'dist/PSP/GAME/NFSU2VIEW')
    ap.add_argument('--sfx', action='store_true', help='sfx.n2sf alone')
    ap.add_argument('--music', action='store_true', help='music.mp3 and music.idx alone')
    ap.add_argument('--music-gains', action='store_true', help='music.idx\'s gains alone, in place (the songs at their own level)')
    ap.add_argument('--cars', action='store_true', help='the cars\' engines alone (carsnd.n2cs, engine-*, aems-*, gear-*, turbo-*)')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.sfx:
        print(json.dumps(dict(write_sfx(args.game, args.out), stitches=write_stitches(args.game, args.out),
                              driveby=write_driveby(args.game, args.out), skid_banks=skid_banks(args.game, args.out)), indent=1))
        return
    if args.music:
        print(json.dumps(music(args.game, args.out)))
        return
    if args.music_gains:
        print(json.dumps(music_gains(args.out)))
        return
    if args.cars:
        print(json.dumps(car_sound(args.game, args.out)))
        return
    report = {'cars': car_sound(args.game, args.out)}
    report['sfx'] = write_sfx(args.game, args.out)
    report['stitches'] = write_stitches(args.game, args.out)
    report['driveby'] = write_driveby(args.game, args.out)
    report['skid_banks'] = skid_banks(args.game, args.out)
    report['music'] = music(args.game, args.out)
    report['speech'] = speech(args.game, args.out)
    (args.out / 'audio.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


def pathfinder_ambience(game):
    """The ambiences of SOUND\\PFDATA\\MUSICSFX.MPF, the Pathfinder map of
    MUSICSFX.MUS ('xDFP' v4, little-endian): event -> stream of the MUS.
    u8 events at 0x0f; at 0x20 u16 node offsets (x4 bytes), u8 node count at
    0x12. A track is three nodes, the third's first u32 holding its group in
    the high half (0x20 songs, 0x40 ambiences) and its stream + 1 in the low
    byte. After the nodes, u16 event offsets (x4, + 0x0c; found as the table
    whose every entry leads to its own event id): an event's record
    is u16 id, u16 commands, then commands of 16 bytes whose word
    (bytes 00 04 00 00) is followed by the u16 node it starts. The game starts an ambience with
    the event of gnAmbientZoneCrossMap (EAXS_PFController::SetupAmbientStreams,
    the Xbox default.xbe 0x3844f0 of the file); the songs are events 0-26."""
    m = archived(game, 'SOUND/PFDATA/MUSICSFX.MPF')
    events, nodes = m[0x0f], m[0x12]
    node_at = [struct.unpack_from('<H', m, 0x20 + 2 * k)[0] * 4 for k in range(nodes)]
    def ids(t):   # the event table: right after the nodes, each entry leading to its own id
        at = [struct.unpack_from('<H', m, t + 2 * k)[0] * 4 + 0x0c for k in range(events)]
        return at if all(a + 4 <= len(m) and struct.unpack_from('<H', m, a)[0] == k for k, a in enumerate(at)) else None
    ev_at = next(a for t in range(node_at[-1], len(m) - 2 * events, 2) if (a := ids(t)))
    out = {}
    for e, at in enumerate(ev_at):
        eid, cmds = struct.unpack_from('<2H', m, at)
        assert eid == e, (e, eid)
        p = m.find(b'\0\x04\0\0', at + 4, at + 4 + 16 * cmds)
        if p < 0:
            continue
        node = struct.unpack_from('<H', m, p + 4)[0]
        if node + 2 >= nodes:
            continue
        word = struct.unpack_from('<I', m, node_at[node + 2])[0]
        if word >> 16 == 0x40 and word & 0xff:
            out[e] = (word & 0xff) - 1
    return out


CODEC_DELAY = 576 + 529   # LAME's encoder delay and an MP3 decoder's (measured: 1105 on the 128 kbit/s loops)


def encode_loop(tool, sch, at, tmp, edge=4096):
    """An ambience as a seamless MP3 loop (the module's doc): (mp3, samples
    to drop, loop samples)."""
    pcm = subprocess.run([str(tool), str(sch), str(at)], check=True, capture_output=True).stdout
    n = len(pcm) // 4
    hi = subprocess.run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', '32000', '-ac', '2', '-i', '-', '-ar', '44100',
                         '-f', 's16le', '-'], input=pcm * 3, check=True, capture_output=True).stdout
    loop = round(n * 44100 / 32000)
    seg = hi[(loop - edge) * 4:(2 * loop + edge) * 4]
    mp3 = subprocess.run(['lame', '--quiet', '-r', '-s', '44.1', '--bitwidth', '16', '--signed', '--little-endian',
                          '--resample', '44.1', '-m', 'j', '-b', '64', '--cbr', '-t', '--noreplaygain', '-', '-'],
                         input=seg, check=True, capture_output=True).stdout
    return mp3, edge + CODEC_DELAY, loop


def music_gains(out):
    """music.idx's gains (the module's doc) set in place, without encoding
    the songs again: every song's to the mean of the songs'."""
    p = out / 'music.idx'
    b = bytearray(p.read_bytes())
    assert b[:4] == b'N2M3'
    n = struct.unpack_from('<I', b, 4)[0]
    at = 8 + 8 * n
    gains = struct.unpack_from(f'<{n}f', b, at)
    songs = [g for g in gains if g]
    mean = sum(songs) / max(1, len(songs))
    struct.pack_into(f'<{n}f', b, at, *[mean if g else 0. for g in gains])
    p.write_bytes(bytes(b))
    return dict(songs=len(songs), gain=round(mean, 4), was=[round(min(songs), 4), round(max(songs), 4)])


def music(game, out):
    """music.mp3 and music.idx (the module's doc)."""
    z = (game / 'ZDIR.BIN').read_bytes()
    entry = [e for e in struct.iter_unpack('<6I', z) if e[0] == game_hash(r'SOUND\PFDATA\MUSICSFX.MUS')][0]
    _, arc, sector, _, size, _ = entry
    with (game / f'ZZDATA{arc}.BIN').open('rb') as src:
        src.seek(sector * 2048)
        data = src.read(size)
    tracks = []
    at = 0
    while at + 8 <= len(data):
        tag, n = data[at:at+4], struct.unpack_from('<I', data, at + 4)[0]
        if tag == b'SCHl':
            tracks.append(at)
        if n < 8:
            at = (at + 4) & ~3
            at = data.find(b'SCHl', at)
            if at < 0:
                break
            continue
        at += n
    levels = [track_rms(data, t) for t in tracks]
    gains = [10 ** ((MUSIC_TARGET - r) / 20) if r > SONG_RMS else 0. for r in levels]
    from convert_envsound import xbe_tables   # the city's zones (0-21; the shops' are the frontend's, not played)
    events = pathfinder_ambience(game)
    from convert_envsound import exe_of
    ambient = sorted({events[e] for e in xbe_tables(exe_of(game))[0][:22] if e in events})
    song_gain = sum(gains) / max(1, sum(1 for g in gains if g))
    gains = [song_gain if g else 0. for g in gains]   # one gain: each song at its own level
    with tempfile.TemporaryDirectory(prefix='nfsu2-music-') as tmp:
        tmp = Path(tmp)
        (tmp / 'music.sch').write_bytes(data)
        tool = tmp / 'eaxa2pcm'
        subprocess.run(['cc', '-O2', '-o', str(tool), str(ROOT / 'tools/eaxa2pcm.c')], check=True)

        def encode(k):
            if k in ambient:
                mp3, lead, loop = encode_loop(tool, tmp / 'music.sch', tracks[k], tmp)
                return mp3, loop * 32000 // 44100, lead, loop
            pcm = subprocess.run([str(tool), str(tmp / 'music.sch'), str(tracks[k])], check=True, capture_output=True).stdout
            mp3 = subprocess.run(['lame', '--quiet', '-r', '-s', '32', '--bitwidth', '16', '--signed', '--little-endian',
                                  '--resample', '44.1', '-m', 'j', '-b', '128', '--cbr', '-t', '--noreplaygain', '-', '-'],
                                 input=pcm, check=True, capture_output=True).stdout
            if mp3[:3] == b'ID3' or mp3[:2] not in (b'\xff\xfb', b'\xff\xfa'):
                raise ValueError(f'track {k}: not a bare MP3 stream')
            return mp3, len(pcm) // 4, 0, 0
        songs = [k for k in range(len(tracks)) if gains[k]]
        with concurrent.futures.ThreadPoolExecutor(8) as pool:
            # each song's decoder and encoder: a core from the Mac's queue (tools/mac_slots.py)
            done = dict(zip(songs + ambient, pool.map(lambda k: mac_slots.call('convert_audio', encode, k), songs + ambient)))
    blob, places, loops = bytearray(), [], b''
    for k in range(len(tracks)):
        mp3 = done[k][0] if k in done else b''
        places += [len(blob) if mp3 else 0, len(mp3)]
        blob += mp3
        lead, loop = done[k][2:] if k in done else (0, 0)
        loops += struct.pack('<2If', lead, loop, song_gain if loop else 0.)
    (out / 'music.mp3').write_bytes(blob)
    (out / 'music.idx').write_bytes(struct.pack('<4sI', b'N2M3', len(tracks)) + struct.pack(f'<{2 * len(tracks)}I', *places)
                                    + struct.pack(f'<{len(tracks)}f', *gains) + b'AMB1' + loops)
    (out / 'music.mus').unlink(missing_ok=True)   # the EA-XA stream the PSP decoded before
    return dict(tracks=len(tracks), songs=len(songs), ambiences={k: dict(lead=done[k][2], loop=done[k][3]) for k in ambient},
                source_bytes=size, mp3_bytes=len(blob),
                seconds=round(sum(v[1] for v in done.values()) / 32000), rms_dbfs=[round(r, 1) for r in levels])

if __name__ == '__main__':
    main()
