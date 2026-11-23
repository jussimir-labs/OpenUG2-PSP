"""Actual diagnostic files, watchdog and resume preservation; no game data."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import build_game as bg
import generator_diagnostics as d
import racing_line


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name).resolve()

    def test_computer_report_is_unique_and_has_recipe_and_requested_fields(self):
        first = d.write_computer(self.root, {'version': 'test', 'iso_sha256': 'fixture'}, 8)
        second = d.write_computer(self.root, {'version': 'test'}, 1)
        self.assertNotEqual(first, second)
        report = json.loads((self.root / f'computer-{first}.json').read_text())
        self.assertEqual(report['jobs'], 8)
        self.assertEqual(report['build_identity']['iso_sha256'], 'fixture')
        self.assertIn('total_bytes', report['memory'])
        self.assertIn('available_bytes', report['memory'])
        self.assertIn('model', report['cpu'])
        self.assertIn('version', report['os'])
        self.assertGreater(report['output_disk']['free_bytes'], 0)
        self.assertFalse({'hostname', 'username', 'environ', 'network'} & report.keys())

    def test_unavailable_counters_are_unknown(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        registry = SimpleNamespace(HKEY_LOCAL_MACHINE=0, OpenKey=Mock(side_effect=OSError))
        with patch.object(d, 'command', side_effect=OSError), patch.object(d.Path, 'read_text', side_effect=OSError), \
             patch.object(d, 'windows_memory', side_effect=OSError), patch.dict(sys.modules, winreg=registry):
            self.assertEqual(d.memory(), {'total_bytes': None, 'available_bytes': None})
            self.assertIsNone(d.cpu_model())

    def test_windows_memory_layout_and_failed_api(self):
        # Exercise the actual Windows ABI structure through a fake DLL on any OS.
        import ctypes as c
        def fill(pointer):
            value = pointer._obj
            self.assertEqual(value.length, 64)
            value.total, value.available = 16 * 2**30, 3 * 2**30
            value.page_total, value.page_available = 32 * 2**30, 7 * 2**30
            return 1
        from types import SimpleNamespace as NS
        with patch.object(c, 'windll', NS(kernel32=NS(GlobalMemoryStatusEx=fill)), create=True):
            result = d.windows_memory()
            self.assertEqual(result['total_bytes'], 16 * 2**30)
            self.assertEqual(result['available_bytes'], 3 * 2**30)
        with patch.object(c, 'windll', NS(kernel32=NS(GlobalMemoryStatusEx=lambda _: 0)), create=True):
            with self.assertRaises(OSError):
                d.windows_memory()

    def test_trace_survives_failure_with_last_stage_and_heartbeat(self):
        with patch.dict(os.environ, NFSU2_DIAGNOSTICS_DIR=str(self.root), NFSU2_DIAGNOSTICS_ATTEMPT='test'), \
             patch.object(d, 'snapshot', return_value={'pid': os.getpid()}):
            with self.assertRaisesRegex(RuntimeError, 'fixture'):
                with d.EventTrace(4127, self.root, interval=.01, stack_after=10) as trace:
                    d.stage('initial-splines', points=20)
                    until = time.monotonic() + 2
                    while time.monotonic() < until:
                        rows = [json.loads(x) for x in next(self.root.rglob('*.jsonl')).read_text().splitlines()]
                        if any(x['kind'] == 'heartbeat' for x in rows):
                            break
                        time.sleep(.01)
                    raise RuntimeError('fixture')
        rows = [json.loads(x) for x in next(self.root.rglob('*.jsonl')).read_text().splitlines()]
        self.assertEqual(rows[-1]['kind'], 'failed')
        self.assertEqual(rows[-1]['stage'], 'initial-splines')
        self.assertTrue(any(x['kind'] == 'heartbeat' for x in rows))
        self.assertFalse(trace.thread.is_alive())
        self.assertIsNone(d.CURRENT.get())

    def test_spawned_workers_dump_stacks_and_preserve_all_events(self):
        script = self.root / 'probe.py'
        script.write_text('''import concurrent.futures, os, time
from pathlib import Path
import generator_diagnostics as d
def work(event):
    with d.EventTrace(event, Path(os.environ['NFSU2_DIAGNOSTICS_DIR']), interval=.05, stack_after=.05):
        d.stage('blocked-fixture')
        time.sleep(.2)
    return event
if __name__ == '__main__':
    with concurrent.futures.ProcessPoolExecutor(2, max_tasks_per_child=1) as pool:
        assert list(pool.map(work, [1, 2, 3])) == [1, 2, 3]
''')
        env = dict(os.environ, PYTHONPATH=str(ROOT / 'tools'), NFSU2_DIAGNOSTICS_DIR=str(self.root),
                   NFSU2_DIAGNOSTICS_ATTEMPT='spawn')
        result = subprocess.run([sys.executable, '-B', str(script)], env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        files = list(self.root.rglob('*.jsonl'))
        self.assertEqual(len(files), 3)
        pids = set()
        for file in files:
            rows = [json.loads(x) for x in file.read_text().splitlines()]
            self.assertEqual(rows[-1]['kind'], 'finished')
            pids.add(rows[0]['pid'])
            stacks = file.with_name(file.stem + '-stacks.txt').read_text()
            self.assertIn('blocked-fixture', stacks)
            self.assertIn('Timeout', stacks)
            self.assertIn('probe.py', stacks)
        self.assertEqual(len(pids), 3)

    def test_racing_lines_cap_inherited_blas_before_starting_workers(self):
        from concurrent.futures import ProcessPoolExecutor
        import struct

        variables = ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'VECLIB_MAXIMUM_THREADS',
                     'MKL_NUM_THREADS', 'BLIS_NUM_THREADS')
        (self.root / 'races.n2rc').write_bytes(struct.pack('<4s2I', b'N2RC', 1, 0))

        def pool(*args, **kwargs):
            self.assertEqual({key: os.environ[key] for key in variables}, dict.fromkeys(variables, '1'))
            return ProcessPoolExecutor(*args, **kwargs)

        with patch.dict(os.environ, {**dict.fromkeys(variables, '32'), 'NFSU2_DIAGNOSTICS_DIR': str(self.root)}), \
             patch.object(sys, 'argv', ['racing_line.py', '--out', str(self.root), '--jobs', '2']), \
             patch.object(racing_line, 'memory', return_value={'available_bytes': 64 << 30}), \
             patch('concurrent.futures.ProcessPoolExecutor', side_effect=pool) as started:
            racing_line.main()
        started.assert_called_once_with(2, max_tasks_per_child=1)

    def test_trace_unwritable_does_not_fail_conversion(self):
        file = self.root / 'not-a-directory'
        file.write_text('keep')
        with patch.dict(os.environ, NFSU2_DIAGNOSTICS_DIR=str(file)):
            with d.EventTrace(1, self.root):
                d.stage('fixture')
        self.assertEqual(file.read_text(), 'keep')

    def test_resume_keeps_work_and_previous_step_logs(self):
        work, logs = self.root / '.trabalho', self.root / 'registros'
        work.mkdir(); logs.mkdir()
        (work / 'fixture.py').write_text('print("new result")')
        (work / 'retained-data').write_bytes(b'original')
        old = logs / '31-racing-lines.log'
        old.write_text('old stalled attempt')
        identity = {'fixture': True}
        (self.root / '.nfsu2-psp-build').write_text(json.dumps(identity))
        bg.prepare_out(self.root, False, identity)
        bg.run_step(31, 'racing lines', 1, ['fixture.py'], work, logs, dict(os.environ))
        self.assertEqual(next(logs.glob('*previous*.log')).read_text(), 'old stalled attempt')
        self.assertIn('new result', old.read_text())
        self.assertIn('pid=', old.read_text())
        self.assertEqual((work / 'retained-data').read_bytes(), b'original')

    def test_fresh_and_update_archive_marker_work_logs_and_keep_earlier_history(self):
        identity = {'old': True}
        for fresh in (False, True):
            with self.subTest(fresh=fresh):
                out = self.root / str(fresh)
                (out / '.trabalho').mkdir(parents=True)
                (out / '.trabalho/unique').write_bytes(b'work')
                (out / 'registros').mkdir()
                (out / 'registros/31.log').write_bytes(b'old log')
                marker = out / '.nfsu2-psp-build'
                marker.write_text(json.dumps(identity))
                if fresh:
                    bg.prepare_out(out, True, {'new': True})
                else:
                    bg.preserve_attempt(out)
                saved = next((out / '.nfsu2-psp-history').iterdir())
                self.assertEqual((saved / '.trabalho/unique').read_bytes(), b'work')
                self.assertEqual((saved / 'registros/31.log').read_bytes(), b'old log')
                self.assertEqual(json.loads((saved / marker.name).read_text()), identity)
                bg.prepare_out(out, True, {'again': True})
                self.assertEqual((saved / '.trabalho/unique').read_bytes(), b'work')

    @unittest.skipIf(os.name == 'nt', 'POSIX symlink fixture')
    def test_history_link_is_rejected_without_moving_work(self):
        (self.root / '.trabalho').mkdir()
        external = self.root / 'external'; external.mkdir()
        (self.root / '.nfsu2-psp-history').symlink_to(external, target_is_directory=True)
        with self.assertRaises(bg.Failure):
            bg.preserve_attempt(self.root)
        self.assertTrue((self.root / '.trabalho').is_dir())
        self.assertEqual(list(external.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
