#!/usr/bin/env python3
"""End-to-end checks of the launcher's self-update against a local fake GitHub.

Small fake packages hold the real launcher and a stand-in for the bundled
Python (`examples/stub_runtime.rs`), so no real release or ISO is involved.
Run locally through unittest (after `cargo build --example stub_runtime`), or
on another computer over SSH; the fake server stays on this one and is
reached through a reverse tunnel:

  python native/tests/test_launcher.py --helper H --stub S --platform windows-x64 \\
      --ssh 'vfx-note\\user@100.118.94.125' --windows --remote-dir C:/Users/user/nfsu2-launcher-test

The test sign-in goes to a separate entry of the password store
("NFSU2 PSP launcher (test server)"), removed again at the end.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fake_github import FakeGitHub

NATIVE = Path(__file__).resolve().parents[1]
TOOLS = ['ffmpeg', 'lame', 'cjpegli', 'movie_audio', 'eaxa2pcm', 'mt2pcm', 'xaencode']


def local_platform():
    machine = os.uname().machine if hasattr(os, 'uname') else 'AMD64'
    if sys.platform == 'darwin':
        return 'macos-arm64' if machine == 'arm64' else 'macos-x64'
    if sys.platform.startswith('linux'):
        return 'linux-x64'
    return 'windows-x64'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def make_bundle(parent, version, platform, helper, stub, tamper=False, protocol=2):
    """A package folder with the files the launcher checks."""
    windows = platform.startswith('windows')
    ext = '.exe' if windows else ''
    out = Path(parent) / f'NFSU2-Generator-{platform}'
    out.mkdir(parents=True)
    python = 'runtime/python/python.exe' if windows else 'runtime/python/bin/python3'
    files = {'generator.py': b'# test\n', 'build_game.py': b'# test\n', 'tools/native_runtime.py': b'# test\n',
             'web/index.html': b'<!doctype html>\n', 'web/app.js': b'\n', 'web/style.css': b'\n',
             'eboot/EBOOT.PBP': b'PBP', 'eboot/release.json': json.dumps(dict(version=version)).encode(),
             'native/launcher.json': (NATIVE / 'launcher.json').read_bytes()}
    files.update({f'runtime/bin/{t}{ext}': b'tool' for t in TOOLS})
    for name, data in files.items():
        (out / name).parent.mkdir(parents=True, exist_ok=True)
        (out / name).write_bytes(data)
    for src, name in ((stub, python), (helper, 'nfsu2-generator' + ext)):
        (out / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out / name)
        os.chmod(out / name, 0o755)
    listed = {p.relative_to(out).as_posix(): sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
    (out / 'bundle.json').write_text(json.dumps(dict(schema=1, launcher_protocol=protocol, generator_version=version, platform=platform,
                                                    python=python, files=listed), indent=2))
    if tamper:
        (out / 'web/app.js').write_bytes(b'changed after sealing\n')
    return out


def make_release(parent, version, platform, helper, stub, tamper=False, bad_checksum=False, protocol=2):
    """The ZIP and .sha256 attachments, as test_bundle.py writes them."""
    folder = make_bundle(Path(parent) / version, version, platform, helper, stub, tamper, protocol=protocol)
    # The package folder inside keeps its name; the release file has the neutral one.
    prefix = json.loads((NATIVE / 'launcher.json').read_text()).get('asset_prefix', 'NFSU2-Generator')
    zipped = folder.parent / f'{prefix}-{platform}.zip'
    with zipfile.ZipFile(zipped, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(folder.rglob('*')):
            if f.is_file():
                z.write(f, folder.name + '/' + f.relative_to(folder).as_posix())
    digest = '0' * 64 if bad_checksum else sha(zipped)
    checksum = zipped.with_suffix('.zip.sha256')
    checksum.write_text(f'{digest}  {zipped.name}\n')
    shutil.rmtree(folder)  # intermediate only; the test consumes the sealed ZIP
    return [zipped, checksum]


class Local:
    def __init__(self, base):
        self.base, self.windows = Path(base), os.name == 'nt'

    def put(self, folder):
        dest = self.base / folder.name
        shutil.rmtree(dest, ignore_errors=True)
        shutil.copytree(folder, dest)
        return str(dest)

    def join(self, *parts):
        return str(Path(*parts))

    def read(self, path):
        p = Path(path)
        return p.read_text() if p.is_file() else None

    def exists(self, path):
        return Path(path).exists()

    def run(self, exe, args, env, port):
        r = subprocess.run([exe, *args], env=dict(os.environ, **env), stdin=subprocess.DEVNULL,
                           capture_output=True, text=True, errors='replace', timeout=300)
        return r.returncode, r.stdout + r.stderr

    def interactive(self, exe, args, env, answers):
        # A real terminal exercises the default prompt, not --yes.
        import pty
        import select
        import time
        master, slave = pty.openpty()
        proc = subprocess.Popen([exe, *args], env=dict(os.environ, **env), stdin=slave,
                                stdout=slave, stderr=slave, close_fds=True)
        os.close(slave)
        chunks, sent, deadline = [], False, time.monotonic() + 40
        try:
            while time.monotonic() < deadline:
                if select.select([master], [], [], .1)[0]:
                    try:
                        data = os.read(master, 65536)
                    except OSError:
                        break
                    if not data:
                        break
                    chunks.append(data)
                    if not sent and b'[Y/n]' in b''.join(chunks):
                        os.write(master, answers)
                        sent = True
                if proc.poll() is not None:
                    break
            if proc.poll() is None:
                proc.kill()
            return proc.wait(timeout=5), b''.join(chunks).decode(errors='replace')
        finally:
            os.close(master)


class Ssh:
    def __init__(self, host, base, windows):
        self.host, self.base, self.windows = host, base.rstrip('/'), windows
        self.ssh = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15']

    def shell(self, command, port=None, timeout=900, stdout_only=False):
        tunnel = ['-R', f'{port}:127.0.0.1:{port}'] if port else []
        if self.windows:
            encoded = base64.b64encode(command.encode('utf-16le')).decode()
            command = 'powershell -NoProfile -NonInteractive -EncodedCommand ' + encoded
        r = subprocess.run(self.ssh + tunnel + [self.host, command], stdin=subprocess.DEVNULL,
                           capture_output=True, text=True, errors='replace', timeout=timeout)
        return r.returncode, r.stdout if stdout_only else r.stdout + r.stderr

    def put(self, folder):
        dest = f'{self.base}/{folder.name}'
        prep = (f"Remove-Item -Recurse -Force -ErrorAction SilentlyContinue '{dest}'; New-Item -ItemType Directory -Force '{self.base}' | Out-Null"
                if self.windows else f'rm -rf {shlex.quote(dest)} && mkdir -p {shlex.quote(self.base)}')
        self.shell(prep)
        archive = folder.parent / (folder.name + '.tar')
        subprocess.run(['tar', '-cf', archive, '-C', folder.parent, folder.name], check=True)
        target = f'{self.base}/{archive.name}'
        subprocess.run(['scp', '-q', '-o', 'BatchMode=yes', archive, f'{self.host}:{target}'], check=True, timeout=1800)
        code, out = self.shell(f"tar -xf '{target}' -C '{self.base}'; Remove-Item '{target}'" if self.windows
                               else f'tar -xf {shlex.quote(target)} -C {shlex.quote(self.base)} && rm {shlex.quote(target)}')
        if code:
            raise RuntimeError(out)
        return dest

    def join(self, *parts):
        return '/'.join(p.rstrip('/') for p in parts)

    def read(self, path):
        code, out = self.shell(f"if (Test-Path '{path}') {{ Get-Content -Raw '{path}' }} else {{ exit 3 }}" if self.windows
                               else f'cat {shlex.quote(path)}', stdout_only=True)
        return out if code == 0 else None

    def exists(self, path):
        return self.shell(f"if (Test-Path '{path}') {{ exit 0 }} else {{ exit 3 }}" if self.windows
                          else f'test -e {shlex.quote(path)}')[0] == 0

    def run(self, exe, args, env, port):
        if self.windows:
            sets = ''.join(f"$env:{k}='{v}'; " for k, v in env.items())
            quoted = ' '.join(f"'{a}'" for a in args)
            command = f"{sets}& '{exe}' {quoted}; exit $LASTEXITCODE"
        else:
            command = 'env ' + ' '.join(f'{k}={shlex.quote(v)}' for k, v in env.items()) + \
                      f' {shlex.quote(exe)} ' + ' '.join(map(shlex.quote, args)) + ' </dev/null'
        return self.shell(command, port)


def check_all(runner, helper, stub, platform, session_vault=False, log=print):
    """The update cycle: none newer, update, delegated start, refused
    packages, a version that does not start, sign-out. Returns a summary."""
    work = Path(tempfile.mkdtemp(prefix='nfsu2-launcher-'))
    fake = FakeGitHub(work).start()
    summary = {}
    try:
        rel = {v: make_release(work / 'releases', v, platform, helper, stub) for v in ('0.2.5', '0.2.7', '0.3.0')}
        rel['0.2.8'] = make_release(work / 'releases', '0.2.8', platform, helper, stub, bad_checksum=True)
        rel['0.2.9'] = make_release(work / 'releases', '0.2.9', platform, helper, stub, tamper=True)
        root = runner.put(make_bundle(work / 'root', '0.2.6', platform, helper, stub))
        exe = runner.join(root, 'nfsu2-generator' + ('.exe' if runner.windows else ''))
        env = dict(NFSU2_UPDATE_API=fake.url, NFSU2_UPDATE_WEB=fake.url, NFSU2_GITHUB_CLIENT_ID='test-client',
                   NFSU2_NO_BROWSER='1')
        if session_vault:
            env['NFSU2_VAULT'] = 'session'
        pointer_path = runner.join(root, '.updates', 'current.json')
        if isinstance(runner, Local):
            iso = Path(root).parent / 'my disc.iso'
            iso.write_bytes(b'private original disc')
            saved = Path(root) / 'output/PSP/SAVEDATA/player.sav'
            saved.parent.mkdir(parents=True)
            saved.write_bytes(b'private save')
            user_files = {str(iso): sha(iso), str(saved): sha(saved)}

        def run(args, expect=0, **extra):
            code, out = runner.run(exe, args, dict(env, **extra), fake.port)
            log(f'$ nfsu2-generator {" ".join(args)} -> {code}\n' + ''.join('    ' + l + '\n' for l in out.splitlines()))
            assert code == expect, f'exit {code}, expected {expect}'
            return out

        def pointer():
            text = runner.read(pointer_path)
            return json.loads(text) if text else None

        def release(*versions, uploader=None):
            fake.releases = []
            for v in versions:
                fake.publish('v' + v, rel[v], uploader=uploader)

        run(['doctor', '--no-update'])
        assert runner.exists(runner.join(root, '.updates')), 'first offline generation has no state folder'
        run(['logout'])
        if isinstance(runner, Local) and os.name != 'nt':
            before = len(fake.log)
            code, out = runner.interactive(exe, ['serve', '--no-browser'], env, b'n\n')
            log('Terminal: decline GitHub sign-in\n' + out)
            assert code == 0 and 'turn update checks on' in out, out
            out = run(['serve', '--no-browser'])
            assert 'Update checks are off' in out and 'Local interface:' in out, out
            assert not any(e['path'] == '/login/device/code' for e in fake.log[before:])
            summary['decline_login'] = 'no device request; generator opens; refusal remembered'
        release('0.2.5')
        out = run(['update', '--yes'])
        assert 'You have the newest version (v0.2.6)' in out
        summary['vault'] = 'password store' if 'kept in this computer' in out else 'session only'

        release('0.2.5', '0.2.7')
        for _ in range(100):
            fake.publish('invalid-tag', [])
        out = run(['update', '--yes'])
        assert 'Checksum verified' in out and 'Updated to v0.2.7 (from v0.2.6)' in out, out
        assert 'STUB version=0.2.7' in out, out
        if summary['vault'] == 'session only':
            assert 'GitHub sign-in continued for this launch.' in out, out
        p = pointer()
        assert p and p['version'] == '0.2.7' and p['state'] == 'confirmed', p
        storage = [e for e in fake.log if e['path'].startswith('/storage/')]
        assert storage and all(e['authorization'] is None for e in storage), 'sign-in sent to storage'
        summary['update'] = 'v0.2.6 -> v0.2.7 installed and confirmed'

        out = run(['serve', '--no-browser', '--no-update'])
        assert 'STUB version=0.2.7' in out and 'Local interface:' in out, out
        assert root.replace('\\', '/').lower() in out.replace('\\', '/').lower(), out
        summary['delegation'] = 'the extracted launcher starts v0.2.7'

        # Published order is not SemVer order. Page 2 carries the newest
        # usable version; a hostile Link URL must never receive credentials.
        release('0.2.7')
        for _ in range(100):
            fake.publish('invalid-tag', [])
        out = run(['update', '--yes'])
        assert 'newest version (v0.2.7)' in out, out
        assert any('page=2' in e['path'] for e in fake.log), 'second release page not read'
        summary['pagination'] = 'page 2 read through repository API; foreign Link ignored'

        release('0.2.7', '0.2.8')
        fake.storage_status = 503
        fake.storage_query = '?qa_signature=do-not-print-this-signed-query'
        out = run(['update', '--yes'])
        assert 'HTTP 503' in out and 'do-not-print-this-signed-query' not in out and '/storage/' not in out, out
        assert pointer()['version'] == '0.2.7'
        fake.storage_status = None
        fake.storage_query = ''
        out = run(['update', '--yes'])
        assert 'does not match the published checksum' in out, out
        assert pointer()['version'] == '0.2.7' and not runner.exists(runner.join(root, '.updates', '0.2.8'))
        release('0.2.7', '0.2.9')
        out = run(['update', '--yes'])
        assert "changed or corrupted" in out and "not installed" in out, out
        assert pointer()['version'] == '0.2.7' and not runner.exists(runner.join(root, '.updates', '0.2.9'))
        release('0.2.8')
        fake.size_delta = 1
        out = run(['update', '--yes'])
        assert 'ended early' in out, out
        assert pointer()['version'] == '0.2.7'
        fake.size_delta = 0
        release('0.2.7', '0.3.0', uploader='someone-else')
        out = run(['update', '--yes'])
        assert 'You have the newest version (v0.2.7)' in out, out
        summary['refused'] = 'wrong checksum, changed file, wrong size, other uploader'

        release('0.2.7', '0.3.0')
        out = run(['serve', '--no-browser', '--yes'], STUB_FAIL_VERSION='0.3.0')
        assert 'Version 0.3.0 did not open; going back' in out and 'STUB version=0.2.7' in out, out
        p = pointer()
        assert p['version'] == '0.2.7' and p['state'] == 'confirmed', p
        summary['rollback'] = 'v0.3.0 failed to open, v0.2.7 started in the same launch'
        out = run(['serve', '--no-browser', '--yes'], STUB_NO_READY_VERSION='0.3.0')
        assert 'did not answer' in out and 'going back' in out and 'STUB version=0.2.7' in out, out
        assert pointer()['version'] == '0.2.7'
        out = run(['update', '--yes'], STUB_FAIL_VERSION='0.3.0')
        assert 'STUB failing on purpose' in out and 'going back' in out, out
        assert pointer()['version'] == '0.2.7' and pointer()['state'] == 'confirmed'
        summary['unready_or_failed_probe'] = 'zero-exit UI without API and failed update probe both roll back'

        fake.api_status = 503
        out = run(['serve', '--no-browser', '--yes'])
        assert 'Update check skipped' in out and 'STUB version=0.2.7' in out, out
        fake.api_status = -1
        out = run(['serve', '--no-browser', '--yes'])
        assert 'GitHub could not be reached' in out and 'STUB version=0.2.7' in out, out
        fake.api_status = 404
        out = run(['login'], expect=1)
        assert 'cannot read the beta repository' in out, out
        fake.api_status = None
        fake.denied = True
        out = run(['login'], expect=1)
        assert 'cancelled on GitHub' in out, out
        fake.denied = False
        summary['unavailable_or_denied'] = '503 and disconnected API keep generator usable; denied login and missing repository access reported'

        if isinstance(runner, Local) and os.name != 'nt':
            # No stored credentials: the initial prompt is sign-in, then
            # the update prompt. Empty answers accept their displayed default.
            before = len([e for e in fake.log if e['path'] == '/login/device/code'])
            code, out = runner.interactive(exe, ['serve', '--no-browser'], env, b'\n\n')
            log('Terminal: accept the displayed default\n' + out)
            assert code == 0 and 'update? [Y/n]' in out and 'STUB version=0.3.0' in out, out
            if summary['vault'] == 'session only':
                assert 'GitHub sign-in continued for this launch.' in out, out
            starts = len([e for e in fake.log if e['path'] == '/login/device/code']) - before
            assert starts <= 1, 'session sign-in lost during version switch'
            assert all(sha(p) == digest for p, digest in user_files.items())
            summary['default_confirmation_and_data'] = 'terminal Enter updates; ISO and save bytes unchanged'

        out = run(['update'])
        assert 'Sign in from a terminal' in out or 'v0.3.0' in out, out
        out = run(['logout'])
        if summary['vault'] == 'password store':
            assert 'removed' in out, out
            out = run(['logout'])
        assert 'No GitHub sign-in is stored' in out, out
        storage = [e for e in fake.log if e['path'].startswith('/storage/')]
        assert storage and all(e['authorization'] is None for e in storage)
        summary['credential_boundary'] = f'{len(storage)} storage requests, none with Authorization; runtime receives no token environment'
        summary['logout'] = 'test sign-in removed'
        return summary
    finally:
        fake.stop()
        shutil.rmtree(work, ignore_errors=True)


def check_public(runner, helper, stub, platform, log=print):
    """A public release page updates with no sign-in at all."""
    work = Path(tempfile.mkdtemp(prefix='nfsu2-launcher-public-'))
    fake = FakeGitHub(work).start()
    fake.public = True
    try:
        fake.publish('v0.2.7', make_release(work / 'releases', '0.2.7', platform, helper, stub))
        root = runner.put(make_bundle(work / 'root', '0.2.6', platform, helper, stub))
        exe = runner.join(root, 'nfsu2-generator' + ('.exe' if runner.windows else ''))
        env = dict(NFSU2_UPDATE_API=fake.url, NFSU2_UPDATE_WEB=fake.url, NFSU2_GITHUB_CLIENT_ID='test-client',
                   NFSU2_NO_BROWSER='1', NFSU2_VAULT='session')
        code, out = runner.run(exe, ['update', '--yes'], env, fake.port)
        log(out)
        assert code == 0 and 'Updated to v0.2.7 (from v0.2.6)' in out, out
        code, out = runner.run(exe, ['update'], env, fake.port)
        log(out)
        assert code == 0 and 'You have the newest version (v0.2.7)' in out, out
        assert not any(e['path'] == '/login/device/code' for e in fake.log), 'a public page asked for a sign-in'
        assert all(e['authorization'] is None for e in fake.log), 'a credential was sent to a public page'
        return 'public release page: updated and checked again with no sign-in and no credential'
    finally:
        fake.stop()
        shutil.rmtree(work, ignore_errors=True)


def built(name):
    ext = '.exe' if os.name == 'nt' else ''
    for kind in ('debug', 'release'):
        p = NATIVE / 'target' / kind / (f'examples/{name}' if name == 'stub_runtime' else name)
        if Path(str(p) + ext).is_file():
            return Path(str(p) + ext)
    return None


class LauncherTest(unittest.TestCase):
    def test_settings_match_the_publisher(self):
        """The launcher accepts what publish.py uploads, from where it uploads."""
        settings = json.loads((NATIVE / 'launcher.json').read_text())
        publish = (NATIVE / 'publish.py').read_text()
        self.assertIn(f"UPDATES = '{settings['repository']}'", publish)
        self.assertIn(f"UPDATES_PREFIX = '{settings['asset_prefix']}'", publish)
        self.assertIn(f"ACCOUNT = '{settings['publisher']}'", publish)

    def test_update_page_gets_neutral_copies(self):
        sys.path.insert(0, str(NATIVE))
        import publish
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            package = tmp / 'NFSU2-Generator-linux-x64.zip'
            package.write_bytes(b'package bytes')
            (tmp / 'out').mkdir()
            names = [p.name for p in publish.updates_assets([package, tmp / 'notes.json'], tmp / 'out')]
            self.assertEqual(names, ['pack-linux-x64.zip', 'pack-linux-x64.zip.sha256'])
            self.assertEqual((tmp / 'out/pack-linux-x64.zip.sha256').read_text(),
                             sha(package) + '  pack-linux-x64.zip\n')
            self.assertEqual((tmp / 'out/pack-linux-x64.zip').read_bytes(), b'package bytes')

    def test_update_cycle(self):
        helper, stub = built('nfsu2-generator'), built('stub_runtime')
        if not helper or not stub:
            self.skipTest('build first: cargo build && cargo build --example stub_runtime')
        with tempfile.TemporaryDirectory() as base:
            check_all(Local(base), helper, stub, local_platform(), session_vault=True, log=lambda s: None)
        with tempfile.TemporaryDirectory() as base:
            check_public(Local(base), helper, stub, local_platform(), log=lambda s: None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--helper', type=Path, required=True)
    ap.add_argument('--stub', type=Path, required=True)
    ap.add_argument('--platform', default=local_platform())
    ap.add_argument('--ssh', help='run on this SSH host')
    ap.add_argument('--windows', action='store_true', help='the SSH host runs Windows')
    ap.add_argument('--remote-dir', help='working folder on the SSH host')
    ap.add_argument('--session-vault', action='store_true', help='keep the test sign-in out of the password store')
    ap.add_argument('--report', type=Path)
    args = ap.parse_args()
    if args.ssh:
        runner = Ssh(args.ssh, args.remote_dir, args.windows)
    else:
        base = tempfile.mkdtemp(prefix='nfsu2-launcher-run-')
        runner = Local(base)
    try:
        summary = check_all(runner, args.helper, args.stub, args.platform, args.session_vault)
        summary['public'] = check_public(runner, args.helper, args.stub, args.platform)
    finally:
        if args.ssh:
            runner.shell(f"Remove-Item -Recurse -Force -ErrorAction SilentlyContinue '{args.remote_dir}'" if args.windows
                         else f'rm -rf {shlex.quote(args.remote_dir)}')
        else:
            shutil.rmtree(base, ignore_errors=True)
    summary.update(platform=args.platform, host=args.ssh or 'local')
    print(json.dumps(summary, indent=2))
    if args.report:
        args.report.write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
