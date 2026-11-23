"""Local generator diagnostics, using only the bundled Python standard library.

No hostname, user name, network addresses, environment dump or file contents.
Missing platform counters are null, never zero invented as a measurement.
"""
from contextvars import ContextVar
from datetime import datetime, timezone
import faulthandler
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import threading
import time
import uuid

CURRENT = ContextVar('generator_event_trace', default=None)


def attempt_id():
    return datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:12]


def utc():
    return datetime.now(timezone.utc).isoformat(timespec='milliseconds')


def command(args):
    return subprocess.run(args, capture_output=True, text=True, timeout=2, check=True).stdout.strip()


def windows_memory():
    import ctypes as c

    class Status(c.Structure):
        _fields_ = [('length', c.c_uint32), ('load', c.c_uint32)] + [
            (k, c.c_uint64) for k in ('total', 'available', 'page_total', 'page_available',
                                     'virtual_total', 'virtual_available', 'extended')]
    value = Status()
    value.length = c.sizeof(value)
    if not c.windll.kernel32.GlobalMemoryStatusEx(c.byref(value)):
        raise OSError('GlobalMemoryStatusEx failed')
    return {'total_bytes': value.total, 'available_bytes': value.available,
            'commit_limit_bytes': value.page_total, 'commit_available_bytes': value.page_available}


def memory():
    try:
        if os.name == 'nt':
            return windows_memory()
        if sys.platform == 'darwin':
            text = command(['vm_stat'])
            page = int(text.split('page size of ')[1].split()[0])
            values = {k.strip(): int(v.strip().rstrip('.'))
                      for k, v in (line.split(':', 1) for line in text.splitlines()[1:] if ':' in line)}
            available = sum(values.get(k, 0) for k in ('Pages free', 'Pages inactive', 'Pages purgeable')) * page
            return {'total_bytes': int(command(['sysctl', '-n', 'hw.memsize'])),
                    'available_bytes': available, 'available_basis': 'free + inactive + purgeable'}
        values = {line.split(':')[0]: int(line.split()[1]) * 1024
                  for line in Path('/proc/meminfo').read_text().splitlines() if len(line.split()) >= 2}
        return {'total_bytes': values.get('MemTotal'), 'available_bytes': values.get('MemAvailable')}
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        return {'total_bytes': None, 'available_bytes': None}


def cpu_model():
    try:
        if os.name == 'nt':
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
                return winreg.QueryValueEx(key, 'ProcessorNameString')[0].strip()
        if sys.platform == 'darwin':
            return command(['sysctl', '-n', 'machdep.cpu.brand_string'])
        for line in Path('/proc/cpuinfo').read_text().splitlines():
            if line.startswith('model name'):
                return line.split(':', 1)[1].strip()
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    return None


def snapshot(output):
    try:
        disk = shutil.disk_usage(output)
        disk_info = {'total_bytes': disk.total, 'free_bytes': disk.free}
    except OSError:
        disk_info = {'total_bytes': None, 'free_bytes': None}
    return {'at_utc': utc(), 'pid': os.getpid(), 'process_cpu_seconds': round(time.process_time(), 3),
            'memory': memory(), 'output_disk': disk_info}


def computer(output):
    return dict(snapshot(output), schema=1, os={'system': platform.system(), 'release': platform.release(),
                'version': platform.version(), 'architecture': platform.machine()},
                cpu={'model': cpu_model(), 'logical_processors': os.cpu_count()},
                python={'version': platform.python_version(), 'implementation': platform.python_implementation()})


def write_computer(logs, identity, jobs):
    """A unique report before extraction, including the recipe's marker snapshot."""
    ident = attempt_id()
    logs.mkdir(parents=True, exist_ok=True)
    report = dict(computer(logs), attempt=ident, jobs=jobs, build_identity=identity)
    (logs / f'computer-{ident}.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return ident


def stage(name, **details):
    trace = CURRENT.get()
    if trace is not None:
        trace.stage(name, **details)


class EventTrace:
    """One owner per worker and one stack dump per long stage.

    On Windows, the sampling thread takes the report when it can run. The
    native watchdog on other platforms can also dump while Python is blocked.
    """
    def __init__(self, event, output, interval=30, stack_after=120):
        self.event, self.output = event, output
        self.interval, self.stack_after = interval, stack_after
        self.stop = threading.Event()
        self.lock = threading.RLock()
        self.stream = self.stacks = self.thread = None
        self.timer = False
        # faulthandler's timed dump crashed spawned workers on Windows and
        # pool workers on Linux (SIGSEGV in half the runs of the test on
        # SteamOS); only macOS keeps it, the others dump from the sampler.
        self.native_timer = sys.platform == 'darwin'
        self.stack_pending = False
        self.started = time.monotonic()
        self.current_stage = 'worker-start'
        self.stage_started = self.started

    def record(self, kind, **details):
        try:
            with self.lock:
                if self.stream is None:
                    return
                data = dict(snapshot(self.output), event=self.event, kind=kind, stage=self.current_stage,
                            elapsed_seconds=round(time.monotonic() - self.started, 3),
                            stage_seconds=round(time.monotonic() - self.stage_started, 3), **details)
                self.stream.write(json.dumps(data) + '\n')
                self.stream.flush()
        except (OSError, ValueError):
            pass  # counters/logging must not change the generated game

    def stage(self, name, **details):
        with self.lock:
            self.current_stage = name
            self.stage_started = time.monotonic()
            self.record('stage', **details)
            if self.stacks is not None:
                try:
                    # C watchdog, single dump for this stage. PID/event are in
                    # the filename; stage and time immediately precede the dump.
                    if self.timer:
                        faulthandler.cancel_dump_traceback_later()
                    self.stacks.write(f'\n{utc()} event={self.event} pid={os.getpid()} stage={name}\n')
                    self.stacks.flush()
                    if self.native_timer:
                        faulthandler.dump_traceback_later(self.stack_after, file=self.stacks, repeat=False)
                        self.timer = True
                    else:
                        self.stack_pending = True
                except (OSError, ValueError, RuntimeError):
                    pass

    def sample(self):
        while not self.stop.wait(self.interval):
            self.record('heartbeat')
            with self.lock:
                age = time.monotonic() - self.stage_started
                if self.stack_pending and self.stacks is not None and age >= self.stack_after:
                    self.stack_pending = False
                    try:
                        # The timed native dump can crash a spawned Windows
                        # worker. Dump synchronously from the Python sampler.
                        self.stacks.write(f'Timeout ({age:.3f}s)!\n')
                        self.stacks.flush()
                        faulthandler.dump_traceback(file=self.stacks)
                    except (OSError, ValueError, RuntimeError):
                        pass

    def __enter__(self):
        self.token = CURRENT.set(self)
        folder = os.environ.get('NFSU2_DIAGNOSTICS_DIR')
        if not folder:
            return self
        try:
            root = Path(folder) / 'racing-lines' / os.environ.get('NFSU2_DIAGNOSTICS_ATTEMPT', 'standalone')
            root.mkdir(parents=True, exist_ok=True)
            stem = f'event-{self.event}-pid-{os.getpid()}-{uuid.uuid4().hex[:8]}'
            self.stream = (root / (stem + '.jsonl')).open('x', encoding='utf-8')
            self.stacks = (root / (stem + '-stacks.txt')).open('x', encoding='utf-8')
            self.stage('worker-start')
            self.thread = threading.Thread(target=self.sample, name='generator-diagnostics', daemon=True)
            self.thread.start()
            print(f'Racing line event {self.event}: started pid={os.getpid()} at {utc()}', flush=True)
        except OSError as exc:
            print(f'Racing line diagnostics unavailable: {type(exc).__name__}', flush=True)
        return self

    def __exit__(self, kind, value, tb):
        self.stop.set()
        if self.thread is not None:
            self.thread.join(5)
        if self.timer:
            faulthandler.cancel_dump_traceback_later()
        self.record('failed' if kind else 'finished', error_type=kind.__name__ if kind else None)
        # A slow/unavailable OS counter must not leave a sampler writing to a
        # closed file; the same lock serializes closing and a late sample.
        with self.lock:
            for f in (self.stream, self.stacks):
                if f is not None:
                    try:
                        f.close()
                    except OSError:
                        pass
            self.stream = self.stacks = None
        CURRENT.reset(self.token)
        return False
