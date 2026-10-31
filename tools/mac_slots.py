#!/usr/bin/env python3
"""A queue for the Mac's CPU, shared by every session and worktree.

  with mac_slots.slot(1, 'ppsspp race-flow'):   # in Python
      ...
  tools/mac_slots.py run --weight 4 --label convert -- <command>   # in shell
  tools/mac_slots.py status

Many sessions test and convert at once on the Mac (15 cores); launched
straight away they loaded it to 3x its cores. Each heavy piece of work
(a PPSSPPHeadless run, a racing line, a city tile, a PCSX2 run) asks here
for a slot of its weight (the cores it keeps busy) and waits until the Mac
has room:
- first come, first served: the oldest request still waiting goes in
  first. While it does not fit (a big one), the first behind it that
  fits may pass it (backfill), but only during its first 120 s of waiting
  (NFSU2_MAC_SLOT_BACKFILL_MAX; 0: never): after that nobody passes, and
  the cores freed up are kept for it;
- it goes in when the CPU busy now (all processes, measured over the last
  second from the kernel's tick counters, not the load average, which lags
  by a minute), plus the weights let in during the last 15 s (maybe
  still starting, not yet in the measure; NFSU2_MAC_SLOT_GRACE), plus its
  own weight, stay within the limit: NFSU2_MAC_CPU_LIMIT of the cores
  (0.90 by default);
- the memory does not hold anyone back (the user's call, 2026-10-21: the
  memory available and the swap kept stalling the work). A floor can still
  be asked for: the memory available (free, speculative, inactive and
  purgeable pages) kept at NFSU2_MAC_MIN_FREE_GB plus NFSU2_MAC_GB_PER_CORE
  a core of the new and recent weights, a request counting at most 4 cores
  (NFSU2_MAC_GB_WEIGHT_CAP); both are 0 by default. With a floor, the cores
  freed up are kept for the oldest, the memory is not: past its 120 s, an
  oldest that lacks only memory is passed by the first behind it that fits
  (with NFSU2_MAC_SLOT_BACKFILL_MAX 0, still never);
- an emergency brake only: while macOS is at critical memory pressure
  (kern.memorystatus_vm_pressure_level 4) or the disk the swap lives on has
  less than NFSU2_MAC_MIN_DISK_GB (25) GB free, no request goes in, not
  even with nothing running; what runs goes on, nothing is stopped. Each
  request it holds, and each one it lets go after, is a line in the
  queue's brake.log;
- when no request is running, the oldest one goes in whatever the load
  (other programs, unregistered work): the queue never stalls;
- a waiting request that has not looked at the queue for NFSU2_MAC_SLOT_STALE
  (10) s (its "beat", written at each look) holds nobody back: its process
  lives but is stuck elsewhere (on 2026-10-16 the head blocked in an
  os.open for 8 min, the Mac at 2.5 of 15 cores, 18 requests behind it).
  It keeps its place and takes it back once it looks again.
Once in, the process drops to nice 15 (NFSU2_MAC_SLOT_NICE), and what it
starts inherits it: the Mac stays responsive to its user. The exception is
slot(..., lower=False), for the PS2 (tools/pcsx2_run.py): it keeps its
priority, to take the CPU from the PPSSPP runs while it runs.

The requests live in the main checkout's reports/mac-slots/ (one file per
request: pid, weight, label, worktree, times), found through git as the
PSP's lock is (tools/psp_run.py), so all worktrees share them. A request
holds a lock on its file while it lives: one whose process died is removed
by the next one that looks.

Off outside macOS (the Deck queues its tests with its own run.lock), with
NFSU2_MAC_SLOTS=0, and inside a command run here (NFSU2_MAC_SLOT_HELD:
its weight already covers what it starts, so its own slots would only
count twice). Waiting messages go to stderr, at most one every 30 s per
process.
"""
import argparse
import contextlib
import ctypes
try:
    import fcntl
except ImportError:  # Windows native bundles do not use the Mac scheduling queue.
    fcntl = None
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
HELD = 'NFSU2_MAC_SLOT_HELD'


def main_checkout():
    """The repository's main checkout (the same for every worktree)."""
    try:
        common = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                capture_output=True, text=True, timeout=5).stdout.strip()
        if common:
            return Path(common).parent
    except (OSError, subprocess.SubprocessError):
        pass
    return ROOT


DIR = Path(os.environ['NFSU2_MAC_SLOTS_DIR']) if os.environ.get('NFSU2_MAC_SLOTS_DIR') else main_checkout() / 'reports/mac-slots'
CORES = float(os.environ.get('NFSU2_MAC_SLOTS_CORES') or os.cpu_count() or 1)
LIMIT = float(os.environ.get('NFSU2_MAC_CPU_LIMIT', '0.90'))
GRACE = float(os.environ.get('NFSU2_MAC_SLOT_GRACE', '15'))   # s a newcomer counts on top of the measure
MIN_FREE = float(os.environ.get('NFSU2_MAC_MIN_FREE_GB', '0'))
GB_PER_CORE = float(os.environ.get('NFSU2_MAC_GB_PER_CORE', '0'))
MIN_DISK = float(os.environ.get('NFSU2_MAC_MIN_DISK_GB', '25'))   # GB free on the swap's disk under which the brake holds
GB_WEIGHT_CAP = float(os.environ.get('NFSU2_MAC_GB_WEIGHT_CAP', '4'))   # cores a request counts for the memory
NICE = int(os.environ.get('NFSU2_MAC_SLOT_NICE', '15'))
BACKFILL_MAX = float(os.environ.get('NFSU2_MAC_SLOT_BACKFILL_MAX', '120'))   # s the oldest may be passed
STALE = float(os.environ.get('NFSU2_MAC_SLOT_STALE', '10'))   # s without a look: a waiting request skipped
POLL = .25
_local = threading.local()
_said = [0.0]


def capacity():
    return LIMIT * CORES


def enabled():
    return fcntl is not None and (sys.platform == 'darwin' or bool(os.environ.get('NFSU2_MAC_SLOTS_DIR'))) \
        and os.environ.get('NFSU2_MAC_SLOTS', '1') != '0' and not os.environ.get(HELD)


# --- the CPU busy now, in cores --------------------------------------------

def _ticks_darwin():
    lib = ctypes.CDLL('/usr/lib/libSystem.dylib')
    lib.mach_host_self.restype = ctypes.c_uint
    host = lib.mach_host_self()

    def ticks():
        buf, count = (ctypes.c_uint * 4)(), ctypes.c_uint(4)   # HOST_CPU_LOAD_INFO: user, system, idle, nice
        if lib.host_statistics(host, 3, buf, ctypes.byref(count)):
            raise OSError('host_statistics')
        return [buf[0] + buf[1] + buf[3], buf[2]]
    return ticks


def _ticks_linux():
    v = [int(x) for x in Path('/proc/stat').read_text().split('\n', 1)[0].split()[1:]]
    return [sum(v) - v[3] - v[4], v[3] + v[4]]   # busy, idle (with iowait)


_ticks = None


def ticks():
    """[busy, idle] ticks since boot, all cores together."""
    global _ticks
    if _ticks is None:
        _ticks = _ticks_darwin() if sys.platform == 'darwin' else _ticks_linux
    return _ticks()


def _share(a, b):
    busy, idle = ((y - x) % 2 ** 32 for x, y in zip(a, b))
    return busy / max(1, busy + idle) * CORES


def busy_cores():
    """The cores busy over the last second or so, all processes together.
    The sample is shared (DIR/cpu.json): the next caller measures from the
    last one's ticks instead of sleeping for its own."""
    if os.environ.get('NFSU2_MAC_SLOTS_FAKE_BUSY'):   # the tests: a file with the cores busy
        try:
            return float(Path(os.environ['NFSU2_MAC_SLOTS_FAKE_BUSY']).read_text())
        except (OSError, ValueError):
            return 0.0
    DIR.mkdir(parents=True, exist_ok=True)
    cache = DIR / 'cpu.json'
    now = time.time()
    try:
        last = json.loads(cache.read_text())
    except (OSError, ValueError):
        last = {}
    if 0 <= now - last.get('t', 0) < 1:
        return last['busy']
    try:
        t = ticks()
        if 1 <= now - last.get('t', 0) < 5:
            busy = _share(last['ticks'], t)
        else:   # no recent sample: measure over 0.3 s
            time.sleep(.3)
            t0, t, now = t, ticks(), time.time()
            busy = _share(t0, t)
    except (OSError, ValueError, KeyError, TypeError):
        return os.getloadavg()[0]
    try:
        tmp = cache.with_name(f'.cpu-{os.getpid()}-{threading.get_ident()}')
        tmp.write_text(json.dumps({'t': now, 'ticks': t, 'busy': busy}))
        tmp.replace(cache)
    except OSError:
        pass
    return busy


_mem = [0.0, 0.0]   # when, GB


def free_gb():
    """GB available without swapping: free, speculative, inactive and
    purgeable pages (vm_stat; a second old at most)."""
    if os.environ.get('NFSU2_MAC_SLOTS_FAKE_FREE'):   # the tests: a file with the GB
        try:
            return float(Path(os.environ['NFSU2_MAC_SLOTS_FAKE_FREE']).read_text())
        except (OSError, ValueError):
            return 99.0
    if time.time() - _mem[0] < 1:
        return _mem[1]
    try:
        vm = subprocess.run(['vm_stat'], capture_output=True, text=True, timeout=5).stdout
        page = int(vm.split('page size of ')[1].split()[0])
        pages = {l.split(':')[0]: int(l.split(':')[1].strip(' .')) for l in vm.splitlines()[1:] if ':' in l}
        gb = sum(pages.get(k, 0) for k in ('Pages free', 'Pages speculative', 'Pages inactive', 'Pages purgeable')) * page / 2 ** 30
    except (OSError, subprocess.SubprocessError, IndexError, ValueError):
        gb = 99.0   # no vm_stat (not a Mac): the CPU alone decides
    _mem[:] = time.time(), gb
    return gb


_brake = [0.0, None]   # when, why


def _fake(name):
    """The tests' value for the brake (a file named by the variable), None without."""
    if not os.environ.get(name):
        return None
    try:
        return float(Path(os.environ[name]).read_text())
    except (OSError, ValueError):
        return None


def pressure_level():
    """macOS's memory pressure (kern.memorystatus_vm_pressure_level: 1
    normal, 2 warning, 4 critical); 0 where unknown."""
    v = _fake('NFSU2_MAC_SLOTS_FAKE_PRESSURE')
    if v is not None:
        return int(v)
    try:
        lib = ctypes.CDLL('/usr/lib/libSystem.dylib')
        out, size = ctypes.c_int(0), ctypes.c_size_t(4)
        if lib.sysctlbyname(b'kern.memorystatus_vm_pressure_level', ctypes.byref(out), ctypes.byref(size), None, 0) == 0:
            return out.value
    except (OSError, AttributeError):
        pass
    return 0


def disk_free_gb():
    """GB free on the disk the swap lives on (/System/Volumes/VM, the
    APFS container's free space)."""
    v = _fake('NFSU2_MAC_SLOTS_FAKE_DISK')
    if v is not None:
        return v
    for p in ('/System/Volumes/VM', '/'):
        try:
            st = os.statvfs(p)
            return st.f_bavail * st.f_frsize / 2 ** 30
        except OSError:
            continue
    return 1e9


def brake():
    """Why the emergency brake holds the queue now, None when it does not
    (a second old at most)."""
    now = time.time()
    if now - _brake[0] < 1 and not os.environ.get('NFSU2_MAC_SLOTS_FAKE_PRESSURE') and not os.environ.get('NFSU2_MAC_SLOTS_FAKE_DISK'):
        return _brake[1]
    why = None
    if pressure_level() >= 4:
        why = 'critical memory pressure'
    else:
        d = disk_free_gb()
        if d < MIN_DISK:
            why = f'disk has {d:.1f} GB free (minimum {MIN_DISK:g})'
    _brake[:] = now, why
    return why


def brake_log(text):
    try:
        with open(DIR / 'brake.log', 'a') as f:
            f.write(f'{time.strftime("%Y-%m-%d %H:%M:%S")} {text}\n')
    except OSError:
        pass


def need_gb(recent, weight):
    """The GB that must stay available to let `weight` in, with `recent`
    let in within the GRACE."""
    return MIN_FREE + GB_PER_CORE * (recent + min(weight, GB_WEIGHT_CAP))


def lower_priority():
    """This process (and what it starts from now on) to nice NICE, if it
    runs above that."""
    try:
        if os.getpriority(os.PRIO_PROCESS, 0) < NICE:
            os.setpriority(os.PRIO_PROCESS, 0, NICE)
    except OSError:
        pass


# --- the requests -----------------------------------------------------------

@contextlib.contextmanager
def _queue_lock():
    """The whole queue, while one request looks at it and maybe goes in."""
    DIR.mkdir(parents=True, exist_ok=True)
    fd = os.open(DIR / '.lock', os.O_RDWR | os.O_CREAT, 0o644)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        yield
    finally:
        os.close(fd)


def scan():
    """The live requests, oldest first; removes those whose process died
    (nobody holds their file's lock any more)."""
    out = []
    for p in sorted(DIR.glob('[0-9]*.json')) if DIR.is_dir() else ():
        try:
            fd = os.open(p, os.O_RDONLY)
        except OSError:
            continue
        try:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                try:
                    out.append(dict(json.loads(os.pread(fd, 65536, 0)), name=p.name))
                except ValueError:
                    pass
            else:
                p.unlink(missing_ok=True)
        finally:
            os.close(fd)
    return out


class Request:
    """One request in the queue: its file, locked while it lives."""

    def __init__(self, weight=1, label=None):
        DIR.mkdir(parents=True, exist_ok=True)
        self.weight = float(weight)
        self.label = label or Path(sys.argv[0]).name
        self.info = {'pid': os.getpid(), 'weight': self.weight, 'label': self.label, 'tree': ROOT.name,
                     'queued': time.time(), 'admitted': None}
        self.info['beat'] = self.info['queued']
        self.braked = None   # the brake's reason it was last held for (brake.log)
        name = f'{time.time_ns():020d}-{os.getpid()}-{threading.get_ident() % 10 ** 6}.json'
        tmp = DIR / f'.new-{name}'
        self.fd = os.open(tmp, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o644)
        fcntl.flock(self.fd, fcntl.LOCK_EX)
        self._write()
        tmp.rename(DIR / name)   # seen by the others only once locked
        self.path = DIR / name

    def _write(self):
        data = json.dumps(self.info).encode()
        os.pwrite(self.fd, data, 0)   # then cut: never an empty file to a reader outside the queue's lock (status)
        os.ftruncate(self.fd, len(data))

    def try_admit(self):
        """(True, None) once in; else (False, why): its place in the queue
        and the CPU it saw."""
        with _queue_lock():
            now = time.time()
            self.info['beat'] = now
            self._write()
            entries = scan()
            # the waiting that still look at the queue (a request of an older
            # mac_slots, with no beat, counts as looking)
            waiting = [e for e in entries if e['admitted'] is None
                       and (e['name'] == self.path.name or now - e.get('beat', now) < STALE)]
            running = [e for e in entries if e['admitted'] is not None]
            place = next((k for k, e in enumerate(waiting) if e['name'] == self.path.name), 0)
            why = {'place': place + 1, 'waiting': len(waiting), 'running': len(running)}
            head = waiting[0] if waiting else None
            held = brake()
            if held:
                why['brake'] = held
                if self.braked != held:
                    brake_log(f'emergency brake ({held}): "{self.label}" held ({self.info["tree"]}, pid {self.info["pid"]}, weight {self.weight:g}; {len(running)} running jobs continue)')
                    self.braked = held
                return False, why
            if self.braked:
                brake_log(f'emergency brake released: "{self.label}" ({self.info["tree"]}, pid {self.info["pid"]}) returns to the queue')
                self.braked = None
            if place and not running:
                return False, why   # the oldest goes in first (with nothing running, it will)
            if running:
                busy = busy_cores()
                recent = sum(e['weight'] for e in running if now - e['admitted'] < GRACE)
                free = free_gb()

                def cpu_fits(weight):
                    return busy + recent + weight <= capacity()

                def fits(weight):
                    return cpu_fits(weight) and free >= need_gb(recent, weight)
                why.update(busy=busy, recent=recent, free=free, need=need_gb(recent, self.weight),
                           reserved=not place and now - self.info['queued'] >= BACKFILL_MAX and not cpu_fits(self.weight))
                # past its 120 s the oldest keeps the cores freed up (not
                # the memory: see the top)
                if place and now - head['queued'] >= BACKFILL_MAX and (BACKFILL_MAX <= 0 or not cpu_fits(head['weight'])):
                    return False, why
                if not fits(self.weight):
                    return False, why
                # passing the oldest: only while it does not fit, and only
                # the first behind it that does
                if place and (fits(head['weight']) or next(e for e in waiting[1:] if fits(e['weight']))['name'] != self.path.name):
                    return False, why
            self.info['admitted'] = time.time()
            self._write()
            return True, None

    def wait(self, lower=True):
        t0 = time.time()
        while True:
            ok, why = self.try_admit()
            if ok:
                if lower:
                    lower_priority()
                if time.time() - t0 >= 10:
                    say(f'"{self.label}" admitted after {time.time() - t0:.0f} s in the queue')
                return
            if why.get('brake'):
                say(f'"{self.label}" held by the emergency brake ({why["brake"]}); {why["running"]} running jobs continue')
                time.sleep(4 * POLL)
                continue
            cpu = (f'CPU {why["busy"]:.1f} + {why["recent"]:g} newly admitted + {self.weight:g} of {capacity():.1f} cores'
                   + (f', {why["free"]:.1f} GB free of {why["need"]:.1f} required' if why['need'] > 0 else '') if 'busy' in why else f'{why["running"]} running')
            if why.get('reserved'):
                cpu += '; no more backfilling'
            say(f'"{self.label}" waiting for a slot (position {why["place"]} of {why["waiting"]}; {cpu})')
            time.sleep(POLL if why['place'] == 1 else 4 * POLL)

    def release(self):
        self.path.unlink(missing_ok=True)
        os.close(self.fd)


def say(text):
    """stderr, at most once every 30 s per process."""
    now = time.time()
    if now - _said[0] >= 30 and not os.environ.get('NFSU2_MAC_SLOTS_QUIET'):
        _said[0] = now
        print(f'mac_slots: {text}', file=sys.stderr, flush=True)


@contextlib.contextmanager
def slot(weight=1, label=None, lower=True):
    """Wait for room for `weight` cores on the Mac, keep it while inside.
    Nothing when the queue is off (see the module's doc) or inside another
    slot of the same thread. lower=False: the process keeps its priority."""
    if not enabled() or getattr(_local, 'held', False):
        yield
        return
    req = Request(weight, label)
    try:
        req.wait(lower)
        _local.held = True
        yield
    finally:
        _local.held = False
        req.release()


def call(label, fn, *args, weight=1):
    """fn(*args) inside a slot: for process pools, one slot per task
    (functools.partial(mac_slots.call, 'label', fn) pickles)."""
    with slot(weight, label):
        return fn(*args)


def status():
    entries = scan()
    now = time.time()
    running = [e for e in entries if e['admitted'] is not None]
    return {'cores': CORES, 'capacity': capacity(), 'busy': busy_cores(), 'free': free_gb(),
            'pressure': pressure_level(), 'disk': disk_free_gb(), 'brake': brake(),
            'recent': sum(e['weight'] for e in running if now - e['admitted'] < GRACE),
            'running': running, 'waiting': [e for e in entries if e['admitted'] is None]}


def room(weight=1):
    """(the Mac would take `weight` cores now with nobody waiting, status)"""
    s = status()
    return (not s['waiting'] and not s['brake'] and s['busy'] + s['recent'] + weight <= s['capacity']
            and s['free'] >= need_gb(s['recent'], weight)), s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run', help='run a command with a CPU slot')
    r.add_argument('--weight', type=float, default=1, help='cores used by the command (default: 1)')
    r.add_argument('--label', help='queue label (default: the command)')
    r.add_argument('command', nargs=argparse.REMAINDER, help='command, after --')
    sub.add_parser('status', help='CPU, limit and queue')
    a = ap.parse_args()
    if a.cmd == 'status':
        s = status()
        floor = f'minimum {MIN_FREE:g} + {GB_PER_CORE:g} per core' if MIN_FREE or GB_PER_CORE else 'does not hold the queue'
        print(f'CPU busy {s["busy"]:.1f} of {s["cores"]:g} cores; limit {s["capacity"]:.1f} '
              f'({LIMIT:.0%}); newly admitted {s["recent"]:g}; available memory {s["free"]:.1f} GB '
              f'({floor}); memory pressure {s["pressure"]} (4: critical); disk free {s["disk"]:.0f} GB '
              f'(brake below {MIN_DISK:g}); queue at {DIR}')
        if s['brake']:
            print(f'EMERGENCY BRAKE: {s["brake"]}; no new admissions; running jobs continue (log at {DIR / "brake.log"})')
        now = time.time()
        for e in s['running'] + s['waiting']:
            state = f'running for {now - e["admitted"]:.0f} s' if e['admitted'] else f'waiting for {now - e["queued"]:.0f} s'
            if not e['admitted'] and now - e.get('beat', now) >= STALE:
                state += f' (stalled for {now - e["beat"]:.0f} s, backfilling allowed)'
            print(f'  {state:22} weight {e["weight"]:<4g} pid {e["pid"]:<6} {e["tree"]}: {e["label"]}')
        return 0
    command = a.command[1:] if a.command[:1] == ['--'] else a.command
    if not command:
        ap.error('missing command (after --)')
    with slot(a.weight, a.label or ' '.join(Path(c).name if k == 0 else c for k, c in enumerate(command[:3]))):
        lower_priority()   # also when the queue is off: the command at nice 15 all the same
        return subprocess.run(command, env=dict(os.environ, **{HELD: '1'})).returncode


if __name__ == '__main__':
    sys.exit(main())
