#!/usr/bin/env python3
"""Run the viewer once in PPSSPP for the smoke tests and captures.

Two emulators, the same emulated clock:
- headless: PPSSPPHeadless built from the same release as the desktop app
  (tools/setup_ppsspp_headless.sh -> .tools/ppsspp-<version>, 1.20.4 the
  latest stable and the reference; PPSSPP_VERSION picks another, as the
  setup script does: a new release means re-checking the smoke's timing
  goals). The upstream master (tools/build_ppsspp.sh) is no reference: it
  charges a guessed GE cost per vertex. No window and no
  frame limiter, so it runs as fast as the host can emulate (2-4x real time
  here) and several runs can share the machine. The default when present.
- app: the desktop PPSSPPSDL (windowed, real time). Needed for what only the
  app does: video and audio dumps (capture.py --video/--sound) and the
  hardware renderer.

A run folder holds the EBOOT and every converted file as symlinks to
dist/PSP/GAME/NFSU2VIEW (copying the 650 MB per run cost more than short
runs themselves) plus smoke-test.txt; what the game writes (stats, frames,
logs) lands there as real files; the savedata goes to the folder's own
memory stick, <folder>/memstick (headless: PPSSPP_MEMSTICK).
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

import mac_slots

ROOT = Path(__file__).resolve().parents[1]
# NFSU2_DIST: another converted game folder than the dist's (a conversion
# tried out beside it, while other sessions run on the dist)
DIST = Path(os.environ['NFSU2_DIST']).resolve() if os.environ.get('NFSU2_DIST') else ROOT / 'dist/PSP/GAME/NFSU2VIEW'
APP = '/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL'
VERSION = os.environ.get('PPSSPP_VERSION', '1.20.4')


def _main_checkout():
    """The repository's main checkout (a worktree has no .tools of its own)."""
    try:
        common = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--git-common-dir'],
                                capture_output=True, text=True, check=True).stdout.strip()
        return (ROOT / common).resolve().parent
    except (OSError, subprocess.CalledProcessError):
        return ROOT


# the worktree's build, else the main checkout's (a worktree without it fell
# back to the windowed app: windows over the user's work)
HEADLESS = next((p for p in (r / f'.tools/ppsspp-{VERSION}/build/PPSSPPHeadless' for r in (ROOT, _main_checkout()))
                 if p.is_file()), ROOT / f'.tools/ppsspp-{VERSION}/build/PPSSPPHeadless')
# The calibrated build (tools/setup_ppsspp_headless.sh --calib): 1.20.4 with
# tools/ppsspp-calib.patch, the PSP-3000's timing model; off unless asked
# (run(calibrated=...), NFSU2_CALIB=1 for the smokes), the plain 1.20.4 stays
# the emulator of the functional smokes.
CALIBRATED = next((p for p in (r / f'.tools/ppsspp-{VERSION}-calib/build/PPSSPPHeadless' for r in (ROOT, _main_checkout()))
                   if p.is_file()), ROOT / f'.tools/ppsspp-{VERSION}-calib/build/PPSSPPHeadless')
CALIB_PARAMS = ROOT / 'tools/ppsspp-calib.ini'
# The headless that answers the savedata utility's dialog (PPSSPP_DIALOG_AUTO,
# tools/ppsspp-headless-dialog-auto.patch): a copy built beside the plain one,
# or the plain one once rebuilt with the patch (tools/setup_ppsspp_headless.sh).
DIALOG_COPY = next((p for p in (r / f'.tools/ppsspp-{VERSION}-dialog/build/PPSSPPHeadless' for r in (ROOT, _main_checkout()))
                    if p.is_file()), None)


def dialog_emulator():
    """The PPSSPPHeadless that answers the save dialog, None without one."""
    if DIALOG_COPY:
        return str(DIALOG_COPY)
    try:
        return str(HEADLESS) if b'PPSSPP_DIALOG_AUTO' in HEADLESS.read_bytes() else None
    except OSError:
        return None


def dialog_game(game, on):
    """savedata-dialog.on beside the game (main.c savedata_dialog_wanted):
    with it a test keeps the explicit saves' system dialog, which the dialog
    headless answers; without it the tests save silently."""
    flag = Path(game) / 'savedata-dialog.on'
    flag.unlink(missing_ok=True)
    if on:
        flag.write_bytes(b'N2SD1\n')
FRAME_BYTES = 480 * 272 * 3 + 15
# the cores one emulator keeps busy, its weight in the Mac's queue
# (tools/mac_slots.py): ~1.4 measured on a race (JIT, the software
# renderer's threads), more while it boots
WEIGHT = float(os.environ.get('NFSU2_PPSSPP_WEIGHT', '1.5'))
CONFIG = ('[General]\nEnable Logging = True\n[Graphics]\nSoftwareRenderer = True\nInternalResolution = 1\nFrameSkip = 0\n'
          'AutoFrameSkip = False\nFrameSkipType = 0\n[SystemParam]\nPSPModel = 1\n[Sound]\nEnable = False\n')


# The dumps' guard. PPSSPP's audio dump (the memory stick's PSP/AUDIO, on
# with PPSSPP_DUMP_AV or PPSSPP_DUMP_AUDIO) twice grew a WAV of 84-104 GB in
# minutes on 22/10 and filled the Mac's disk. A thread beside each run kills
# the emulator once PSP/AUDIO passes NFSU2_DUMP_AUDIO_MAX_MB (1024), PSP/VIDEO
# passes NFSU2_DUMP_VIDEO_MAX_MB (20480) or the disk's free space falls under
# NFSU2_DUMP_MIN_FREE_GB (15); the reason goes to stderr and dump-guard.txt.
def dump_guard(proc, memstick, every=1.0):
    ms = Path(memstick)
    audio_max = int(os.environ.get('NFSU2_DUMP_AUDIO_MAX_MB', 1024)) << 20
    video_max = int(os.environ.get('NFSU2_DUMP_VIDEO_MAX_MB', 20480)) << 20
    free_min = int(os.environ.get('NFSU2_DUMP_MIN_FREE_GB', 15)) << 30

    def size(d):
        try:
            return sum(f.stat().st_size for f in d.iterdir() if f.is_file())
        except OSError:
            return 0

    def watch():
        while proc.poll() is None:
            audio, video = size(ms / 'PSP/AUDIO'), size(ms / 'PSP/VIDEO')
            free = shutil.disk_usage(ms).free if ms.exists() else free_min
            why = (f'PSP/AUDIO com {audio >> 20} MB' if audio > audio_max else
                   f'PSP/VIDEO com {video >> 20} MB' if video > video_max else
                   f'only {free >> 30} GB free on disk' if free < free_min else None)
            if why:
                print(f'ERROR dump_guard: {why} in {ms}; PPSSPP stopped', file=sys.stderr, flush=True)
                proc.kill()
                try:
                    (ms / 'dump-guard.txt').write_text(why + '\n')
                except OSError:
                    pass
                return
            time.sleep(every)
    t = threading.Thread(target=watch, daemon=True)
    t.start()
    return t


def default_emulator():
    """PPSSPPHeadless. The windowed app only when asked (--emulator, or
    NFSU2_WINDOWED=1): a test never opens a window on its own."""
    if HEADLESS.is_file():
        return str(HEADLESS)
    if os.environ.get('NFSU2_WINDOWED') == '1':
        return APP
    raise SystemExit(f'PPSSPPHeadless not built ({HEADLESS}): tools/setup_ppsspp_headless.sh, '
                     'or NFSU2_WINDOWED=1 for the windowed app')


def is_headless(emulator):
    return Path(emulator).name.startswith('PPSSPPHeadless')


# What the game writes beside itself (tests, measures, saves): never linked
# into a run's folder, or the run writes through the link into the dist
# shared by every session and the real PSP (tools/psp_run.py waits for the
# frame-log.bin there). boot.txt: the boot's steps; hostcmd.txt: the rescue
# channel (probe.c); bug-*.n2bug and repro-*: the bug rewind.
GAME_OUTPUTS = ('track-smoke.txt', 'smoke-test.txt', 'language.txt', 'frame-log.bin', 'marks.txt', 'probe.bin', 'probe-pass.bin',
                'probe-detail.txt', 'boot.txt', 'hostcmd.txt', 'repro.txt', 'repro-replay.txt', 'repro.n2bug',
                'card-bench.txt', 'open-bench.txt', 'race-hold.jsonl', 'bench-dir.txt', 'ms-runs.txt', 'ms-done.txt', 'outrun-engage.txt', 'outrun-near.txt',
                # main.c admission_log appends to it: linked, every run on
                # the Mac appended to the dist's own (24 MB on 14/10)
                'memory-admission.jsonl', 'alloc-sites.txt', 'outrun-engage.txt',
                # movie.c appends each movie's line; linked, the Mac's runs
                # appended to a real PSP session's frozen folder (16/10)
                '.movie-playback.log', 'boot-drive.txt', 'boot-drive.log', 'savedata-dialog.on')


def game_output(name):
    """True for a file the game writes (GAME_OUTPUTS, saves, frames, logs,
    stats, bug files): a run keeps its own, never the dist's."""
    # TAG-...: a Memory Stick run's files (ms-runs.txt, psp-viewer/main.c)
    return (name in GAME_OUTPUTS or name.endswith(('.sav', '.ppm', '.n2bug', '-log.csv', '-boot.txt', '-frame-log.bin', '-viewer-stats.json', '-card-bench.txt', '-open-bench.txt'))
            or name.startswith(('viewer-', 'track-stream-')))


def make_game(folder, skip=(), eboot=None):
    """folder/viewer with the build linked in (the dist's EBOOT, or eboot, or
    the one NFSU2_EBOOT names); returns that path."""
    game = Path(folder) / 'viewer'
    game.mkdir(parents=True, exist_ok=True)
    for p in DIST.iterdir():
        # Test outputs and saves belong to this run. Linking them can
        # overwrite another emulator's or the real PSP's measurements.
        if p.is_file() and p.name not in skip and not game_output(p.name):
            (game / p.name).unlink(missing_ok=True)
            (game / p.name).symlink_to(p)
    # NFSU2_EBOOT: another build than the dist's (sessions working side by
    # side each test their own without replacing the shared dist's)
    eboot = eboot or os.environ.get('NFSU2_EBOOT')
    if eboot and 'EBOOT.PBP' not in skip:
        (game / 'EBOOT.PBP').unlink(missing_ok=True)
        (game / 'EBOOT.PBP').symlink_to(Path(eboot).resolve())
    # the data's id for the screenshots' marker (tools/data_id.py)
    try:
        import data_id
        data_id.write(game, DIST)
    except Exception as e:   # never a test's failure
        print('data_id:', e)
    return game


def eboot_sfo(path):
    """The EBOOT's PARAM.SFO as a dict (numbers as int), {} if unreadable."""
    import struct
    try:
        with open(path, 'rb') as f:
            head = f.read(40)
            if head[:4] != b'\0PBP':
                return {}
            start, end = struct.unpack('<2I', head[8:16])
            f.seek(start)
            sfo = f.read(end - start)
        keys, data, n = struct.unpack('<3I', sfo[8:20])
        out = {}
        for i in range(n):
            ko, fmt, ln, _, do = struct.unpack('<HHIII', sfo[20 + i * 16:36 + i * 16])
            name = sfo[keys + ko:sfo.index(b'\0', keys + ko)].decode()
            raw = sfo[data + do:data + do + ln]
            out[name] = struct.unpack('<I', raw)[0] if fmt == 0x404 else raw.rstrip(b'\0').decode(errors='replace')
        return out
    except (OSError, ValueError, struct.error):
        return {}


def eboot_compact(path):
    """True for a build made with PSP_LARGE_MEMORY=0 (MEMSIZE 0 in its
    PARAM.SFO): the PSP-1000's. The runs take PPSSPP's PSPModel 0 for it
    unless PPSSPP_PSP_MODEL or psp_model says otherwise; the slim's larger
    memory had every compact matrix on the Mac pass what the PSP-1000 fails."""
    return eboot_sfo(path).get('MEMSIZE', 1) == 0


def run(game, config_line, frames, emulator=None, log=None, extra_config='', wait_frame=True, frameskip=0, psp_model=None, sound=False,
        calibrated=None, calib_stats=None, calib_env=None, dialog=False):
    """Write smoke-test.txt, run until the game quits (it writes its last
    frame and stats and exits when the test ends) or the deadline; returns
    the process log text. frames: the test length, for the deadline.
    frameskip (headless): draw one frame in frameskip+1, a number sharing
    no factor with the display buffers (2 in the menus, 3 on the street:
    6 keeps them all drawn), ~4x faster for runs judged on telemetry: 1.20
    charges no emulated time for drawing, so the game runs the same; the
    last frame captured may be up to 3*(frameskip+1) old.
    psp_model (headless): 0 the PSP-1000 (24 MB of user memory), 1 the slim
    (the default; PPSSPP_PSP_MODEL in the environment otherwise; a compact
    EBOOT, MEMSIZE 0, takes 0).
    sound (headless): record the mixed audio, a WAV in the folder's memory
    stick (PSP/AUDIO), from the game's first sound on.
    calibrated: a parameter file (or True: tools/ppsspp-calib.ini) runs the
    calibrated build, the PSP-3000's timing (README, "PPSSPP calibrado");
    calib_stats: where it writes the model's counters (JSON); calib_env:
    more of its variables (PPSSPP_CALIB_SET, _GELOG, _SECTIONS).
    dialog: the explicit saves' system dialog kept and answered Yes
    (dialog_emulator(), savedata-dialog.on); otherwise they save silently."""
    if calibrated:
        if not CALIBRATED.is_file():
            raise SystemExit(f'Calibrated PPSSPPHeadless has not been built ({CALIBRATED}): tools/setup_ppsspp_headless.sh --calib')
        emulator = str(CALIBRATED)
    if dialog:
        emulator = dialog_emulator()
        if not emulator:
            raise SystemExit('Dialog PPSSPPHeadless has not been built (tools/setup_ppsspp_headless.sh, PPSSPP_DIALOG_AUTO)')
    emulator = emulator or default_emulator()
    game = Path(game)
    dialog_game(game, dialog)
    (game / 'smoke-test.txt').write_text(config_line.rstrip('\n') + '\n')
    for f in ('viewer-frame.ppm', 'viewer-stats.json'):
        (game / f).unlink(missing_ok=True)
    log = Path(log) if log else game.parent / 'process.log'
    if is_headless(emulator):
        # emulated seconds plus boot; the host may be slower than real time
        cmd = [emulator, '-j', '--root=' + str(game), '--timeout=' + str(int(120 + frames / 10)), str(game / 'EBOOT.PBP')]
        deadline = 180 + frames / 8
        # the folder's own memory stick: its savedata survives a restart on
        # the same folder and no other run sees it
        (game.parent / 'memstick').mkdir(exist_ok=True)
        env = dict(os.environ, PPSSPP_FRAMESKIP=str(frameskip), PPSSPP_MEMSTICK=str(game.parent / 'memstick'))
        env.pop('PPSSPP_DIALOG_AUTO', None)
        if dialog:
            env['PPSSPP_DIALOG_AUTO'] = 'yes'
        if psp_model is None and 'PPSSPP_PSP_MODEL' not in os.environ and eboot_compact(game / 'EBOOT.PBP'):
            psp_model = 0   # a compact build (PSP_LARGE_MEMORY=0): the PSP-1000 it is for
        if psp_model is not None:
            env['PPSSPP_PSP_MODEL'] = str(psp_model)
        if calibrated:
            env['PPSSPP_CALIB'] = str(CALIB_PARAMS if calibrated is True else calibrated)
            if calib_stats:
                env['PPSSPP_CALIB_STATS'] = str(calib_stats)
            env.update({k: str(v) for k, v in (calib_env or {}).items()})
            # the model runs on the IR interpreter: ~2-3x the time of the JIT
            cmd[3] = '--timeout=' + str(int(360 + frames / 3))
            deadline = 480 + frames / 2.5
        if sound:
            env['PPSSPP_DUMP_AUDIO'] = '1'
            for w in (game.parent / 'memstick/PSP/AUDIO').glob('*.wav'):
                w.unlink()
    else:
        config = game.parent / 'test.ini'
        config.write_text(CONFIG.replace('[General]\n', '[General]\n' + extra_config, 1))
        cmd = [emulator, '-j', '--windowed', '--root=' + str(game), '--appendconfig=' + str(config),
               '--log=' + str(game.parent / 'ppsspp.log'), str(game / 'EBOOT.PBP')]
        deadline = 30 + frames / 20
        env = None
    # a slot in the Mac's queue for the emulator's run: the runs of every
    # session wait there instead of loading the Mac past its cores
    with mac_slots.slot(WEIGHT, 'ppsspp ' + ' '.join(config_line.split()[:3])), log.open('w') as out:
        proc = subprocess.Popen(cmd, cwd=game, stdout=out, stderr=subprocess.STDOUT, env=env)
        if env:
            dump_guard(proc, env['PPSSPP_MEMSTICK'])
        try:
            end = time.monotonic() + deadline
            while time.monotonic() < end and proc.poll() is None:
                cap = game / 'viewer-frame.ppm'
                if wait_frame and not is_headless(emulator) and cap.exists() and cap.stat().st_size >= FRAME_BYTES:
                    time.sleep(.3)
                    break
                time.sleep(.2)
        finally:
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    return log.read_text(errors='replace')


def copy_out(game, names, dest_dir, prefix=''):
    for n in names:
        if (Path(game) / n).exists():
            shutil.copy2(Path(game) / n, Path(dest_dir) / (prefix + n))
