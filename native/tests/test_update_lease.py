"""Directed actual-launcher concurrency/refresh checks; local fake GitHub.

Call these functions with Local/Ssh (or the desktop Credential Manager runner).
They do not compile, publish, convert an ISO, or create an application.
"""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import tempfile
import time
import shutil
import os
import signal
import subprocess

from fake_github import FakeGitHub
from test_launcher import Local, make_bundle, make_release


def check_login_keeps_pending(runner, helper, stub, platform, log=print):
    """Administrative login never approves a pending, broken runtime."""
    if not isinstance(runner, Local):
        raise ValueError('Run this directed fixture with Local on the actual target OS.')
    with tempfile.TemporaryDirectory(prefix='nfsu2-login-pending-') as tmp:
        work = Path(tmp); fake = FakeGitHub(work).start()
        root = Path(runner.put(make_bundle(work / 'root', '0.2.6', platform, helper, stub)))
        for version in ('0.2.7', '0.2.8'):
            source = make_bundle(work / version, version, platform, helper, stub)
            shutil.copytree(source, root / '.updates' / version)
        pointer = root / '.updates/current.json'
        before = json.dumps(dict(schema=1, version='0.2.8', state='pending', previous='0.2.7', attempts=0, **{'from':'0.2.7'}))
        pointer.write_text(before)
        exe = str(root / ('nfsu2-generator.exe' if runner.windows else 'nfsu2-generator'))
        env = dict(NFSU2_UPDATE_API=fake.url, NFSU2_UPDATE_WEB=fake.url, NFSU2_GITHUB_CLIENT_ID='test-client',
                   NFSU2_NO_BROWSER='1', STUB_FAIL_VERSION='0.2.8')
        try:
            code, out = runner.run(exe, ['login'], env, fake.port)
            log(out); assert code == 0, out
            assert pointer.read_text() == before, 'login confirmed, cleaned, rolled back or counted a boot attempt'
            assert (root / '.updates/0.2.7/bundle.json').is_file()
            assert 'STUB version=' not in out, 'administrative login started a runtime'
            return dict(pending='unchanged despite broken pending probe', previous='preserved', credentials='not printed or passed to runtime')
        finally:
            try:
                code, out = runner.run(exe, ['logout'], env, fake.port)
                assert code == 0, out
                assert pointer.read_text() == before
            finally: fake.stop()


def touch(runner, path):
    if isinstance(runner, Local):
        Path(path).touch()
    else:
        command = f"[IO.File]::WriteAllText('{path}','')" if runner.windows else f"touch '{path}'"
        code, out = runner.shell(command)
        assert code == 0, out


def wait_for(predicate, explanation, seconds=12):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate(): return
        time.sleep(.05)
    raise AssertionError(explanation)


def check_concurrent_update(runner, helper, stub, platform, log=print):
    """B cannot touch A's partial, pointer or staging, during download OR UI."""
    if not isinstance(runner, Local):
        raise ValueError('Run this controlled process fixture with Local on the actual target OS.')
    with tempfile.TemporaryDirectory(prefix='nfsu2-lease-update-') as tmp:
        work = Path(tmp)
        fake = FakeGitHub(work).start()
        root = runner.put(make_bundle(work / 'root', '0.2.6', platform, helper, stub))
        exe = runner.join(root, 'nfsu2-generator' + ('.exe' if runner.windows else ''))
        release = runner.join(root, 'release-stub')
        marker = runner.join(root, 'release-stub.ready')
        pointer = runner.join(root, '.updates/current.json')
        env = dict(NFSU2_UPDATE_API=fake.url, NFSU2_UPDATE_WEB=fake.url, NFSU2_GITHUB_CLIENT_ID='test-client',
                   NFSU2_NO_BROWSER='1', NFSU2_VAULT='session', STUB_HOLD_READY=release)
        assets = make_release(work / 'releases', '0.2.7', platform, helper, stub)
        partial = runner.join(root, '.updates/downloads', assets[0].name + '.partial')
        fake.publish('v0.2.7', assets)
        fake.block_zip = True
        pool = ThreadPoolExecutor(max_workers=1)
        process = subprocess.Popen([exe, 'serve', '--yes', '--no-browser'], env=dict(os.environ, **env),
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, start_new_session=os.name != 'nt')
        def collect():
            output, _ = process.communicate(timeout=35)
            return process.returncode, output
        first = pool.submit(collect)
        try:
            assert fake.download_started.wait(12), 'A did not reach the ZIP barrier'
            wait_for(lambda: runner.exists(partial), 'A never created its partial')
            for args in (['update', '--yes'], ['serve', '--no-update'], ['login'], ['logout']):
                before = len(fake.log)
                state = runner.read(pointer)
                code, out = runner.run(exe, args, env, None)  # Reuse A's tunnel, no second listener.
                assert code == 76 and 'BUSY:' in out, out
                assert len(fake.log) == before, 'B made a network request'
                assert runner.read(pointer) == state and runner.exists(partial), 'B changed A state'
            fake.download_release.set()
            wait_for(lambda: runner.exists(marker), 'delegated runtime never reached READY')
            wait_for(lambda: (json.loads(runner.read(pointer) or '{}').get('state') == 'confirmed'), 'A was not confirmed')
            state = runner.read(pointer)
            before = len(fake.log)
            code, out = runner.run(exe, ['update', '--yes'], env, None)
            assert code == 76 and 'BUSY:' in out, out
            assert runner.read(pointer) == state and len(fake.log) == before
            identity = json.loads(runner.read(marker))
            assert identity['version'] == '0.2.7' and identity['lease_held']
            touch(runner, release)
            code, out = first.result(timeout=10)
            log(out); assert code == 0, out
            # One later process can acquire again after the whole family exits.
            code, out = runner.run(exe, ['doctor', '--no-update'], env, fake.port)
            log(out); assert code == 0, out
            return dict(two_instances='B refused during download and confirmed UI; no B requests or mutation',
                        delegated_runtime_identity=identity, released='later doctor acquired successfully')
        finally:
            fake.download_release.set()
            touch(runner, release)
            try: first.result(timeout=10)
            finally:
                if process.poll() is None:
                    if os.name != 'nt':
                        try: os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError: pass
                        try:
                            owned = json.loads(Path(marker).read_text())['pid']
                            os.killpg(owned, signal.SIGKILL)
                        except (OSError, ValueError): pass
                    process.kill()
                process.wait(timeout=5)
                pool.shutdown(wait=True)
                if process.stdout: process.stdout.close()
                fake.stop()


def check_refresh_retention(runner, helper, stub, platform, log=print):
    """Requires a real available test vault; does not substitute session mode."""
    with tempfile.TemporaryDirectory(prefix='nfsu2-refresh-retention-') as tmp:
        work = Path(tmp); fake = FakeGitHub(work).start()
        root = runner.put(make_bundle(work / 'root', '0.2.6', platform, helper, stub))
        exe = runner.join(root, 'nfsu2-generator' + ('.exe' if runner.windows else ''))
        env = dict(NFSU2_UPDATE_API=fake.url, NFSU2_UPDATE_WEB=fake.url, NFSU2_GITHUB_CLIENT_ID='test-client', NFSU2_NO_BROWSER='1')
        def run(args):
            code, out = runner.run(exe, args, env, fake.port)
            log(out); assert code == 0, out
            return out
        def starts(): return sum(r['path'] == '/login/device/code' for r in fake.log)
        try:
            fake.refresh_enabled = True
            run(['logout'])
            signed = run(['login'])
            if 'kept in this computer' not in signed:
                return dict(status='not qualified', reason='system password store unavailable; no persistence claim')
            before = starts()
            fake.refresh_status = 503
            fake.refresh_error = 'bad_refresh_token'  # HTTP5xx must beat this misleading body.
            unavailable = run(['update', '--yes'])
            assert 'saved GitHub sign-in was kept' in unavailable and starts() == before
            assert fake.refresh_requests == 1
            fake.refresh_status = 200; fake.refresh_error = None
            run(['update', '--yes'])
            assert starts() == before and fake.refresh_requests == 2, 'same saved refresh was not retried'
            run(['login'])  # Seed a newly expiring token for the permanent-error case.
            before = starts(); fake.refresh_error = 'bad_refresh_token'
            run(['update', '--yes'])
            assert starts() == before + 1, 'permanent rejection did not trigger Device Flow'
            return dict(status='passed', transient='503 kept vault; same refresh retried successfully',
                        permanent='explicit bad_refresh_token required a new Device Flow')
        finally:
            try: run(['logout'])
            finally: fake.stop()


class LeaseTest(__import__('unittest').TestCase):
    """Local runs with the built launcher (cargo build --bins --examples)."""
    def setUp(self):
        from test_launcher import built, local_platform
        self.helper, self.stub, self.platform = built('nfsu2-generator'), built('stub_runtime'), local_platform()
        if not self.helper or not self.stub:
            self.skipTest('build first: cargo build && cargo build --example stub_runtime')
        self.runner = Local(tempfile.mkdtemp(prefix='nfsu2-lease-run-'))
        self.addCleanup(shutil.rmtree, self.runner.base, True)

    def test_login_keeps_a_pending_version(self):
        check_login_keeps_pending(self.runner, self.helper, self.stub, self.platform, log=lambda s: None)

    def test_second_instance_is_refused_while_updating(self):
        check_concurrent_update(self.runner, self.helper, self.stub, self.platform, log=lambda s: None)


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Lease and refresh checks with the system password store.')
    ap.add_argument('--helper', type=Path, required=True)
    ap.add_argument('--stub', type=Path, required=True)
    ap.add_argument('--platform', required=True)
    ap.add_argument('--report', type=Path)
    a = ap.parse_args()
    base = tempfile.mkdtemp(prefix='nfsu2-lease-run-')
    try:
        result = {}
        for name, check in (('login_pending', check_login_keeps_pending), ('concurrent', check_concurrent_update),
                            ('refresh', check_refresh_retention)):
            result[name] = check(Local(base + '/' + name), a.helper, a.stub, a.platform, log=lambda s: None) or 'passed'
        result['platform'] = a.platform
        print(json.dumps(result, indent=2))
        if a.report:
            a.report.write_text(json.dumps(result, indent=2) + '\n')
    finally:
        shutil.rmtree(base, ignore_errors=True)
