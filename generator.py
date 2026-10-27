#!/usr/bin/env python3
"""Local browser interface. No ISO upload and no changes to the PSP/card.

The existing build_game.py remains the single conversion recipe. This helper
only selects files, checks prerequisites and supervises that command.
"""
import argparse
import base64
import copy
import hmac
import json
import mimetypes
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit
import webbrowser

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
import native_runtime
from process_control import WindowsJob, lock_file
sys.dont_write_bytecode = True


class UserError(Exception):
    pass


def python_runtime(root=ROOT):
    if native_runtime.bundled():
        return sys.executable
    for candidate in (root / '.venv/bin/python', shutil.which('python3.12'),
                      shutil.which('python3.13'), shutil.which('python3'), sys.executable):
        if candidate and Path(candidate).is_file():
            return str(candidate)
    return sys.executable


def validate_paths(iso, output, root=ROOT):
    root = root.resolve()
    if not isinstance(iso, str) or not isinstance(output, str) or not iso.strip() or not output.strip():
        raise UserError('Choose your ISO and output folder.')
    disc, out = Path(iso).expanduser().resolve(), Path(output).expanduser().resolve()
    if not disc.is_file() or disc.suffix.lower() != '.iso':
        raise UserError('Choose a complete US Underground 2 .iso file.')
    if out == root or out in root.parents or disc == out or out in disc.parents:
        raise UserError('Choose an output folder separate from the tool and ISO.')
    if root in out.parents and out.relative_to(root).parts[0] not in {'output', 'saida'}:
        raise UserError('Inside the tool, use output (or the existing saida folder). Otherwise choose an external output folder.')
    if out.exists():
        if not out.is_dir():
            raise UserError('The output must be a folder.')
        if any(out.iterdir()) and not (out / '.nfsu2-psp-build').is_file():
            raise UserError('This folder already contains files. Choose a new folder; nothing will be deleted.')
        if (out / 'PSP').exists():
            raise UserError('This output already contains a PSP folder. Keep it and choose a new output.')
        if any(p.is_symlink() and p.is_dir() for p in out.rglob('*')):
            raise UserError('The output contains a linked folder. Choose a new output to keep external files untouched.')
        if any(out.rglob('SAVEDATA')) or any(out.rglob('*.sav')):
            raise UserError('The output contains saves. Choose another folder; your saves will be preserved.')
    parent = out
    while not parent.exists():
        parent = parent.parent
    if shutil.disk_usage(parent).free < 12 * 10**9:
        raise UserError('The destination needs at least 12 GB free for conversion.')
    return disc, out


def probe(root=ROOT):
    """Runs in the conversion interpreter, not the UI thread."""
    sys.path.insert(0, str(root))
    try:
        import build_game as builder
        problems, _ = builder.preflight(argparse.Namespace(sem_filmes=False, cjpegli=None))
        try:
            builder.validate_release()
        except Exception as exc:
            problems.append(str(exc))
        def friendly(problem):
            if native_runtime.bundled():
                return 'An included converter or library is missing. Extract the complete native download again. Details: ' + problem
            if problem.startswith('faltam módulos do Python:'):
                return 'Python conversion libraries need setup. Use Prepare this computer.'
            if problem.startswith('falta o cjpegli'):
                return 'The movie encoder needs setup. All 28 movies are included in this workflow.'
            if problem.startswith('falta o programa '):
                return 'The ' + problem.split()[3] + ' program is missing. Setup can install it where supported.'
            if problem.startswith('falta um compilador C'):
                return 'A C compiler is required. Install the system developer tools, then prepare again.'
            return problem
        return dict(ready=not problems, problems=[friendly(p) for p in problems], details=problems, platform=sys.platform,
                    python=sys.version.split()[0])
    except Exception as exc:
        return dict(ready=False, problems=[str(exc)], platform=sys.platform,
                    python=sys.version.split()[0])


def detect_source(iso, requested, root=ROOT):
    sys.path.insert(0, str(root))
    from sources import SOURCES
    if requested not in ('auto', 'xbox', 'ps2'):
        raise UserError('Invalid source.')
    source = SOURCES.get(requested) if requested != 'auto' else next(
        (s for s in SOURCES.values() if s.recognise(iso)), None)
    if not source or not source.available or not source.recognise(iso):
        raise UserError('The image was not recognized as a supported Xbox/PS2 source. Full validation takes place before conversion.')
    return source.name


def native_pick(kind):
    """An OS picker avoids copying a multi-GB ISO into a browser upload."""
    folder = kind == 'output'
    wsl = bool(os.environ.get('WSL_DISTRO_NAME'))
    if sys.platform == 'darwin':
        action = 'choose folder' if folder else 'choose file'
        script = 'POSIX path of (' + action + ' with prompt "NFSU2 PSP — ' + ('Choose an output parent folder' if folder else 'Select your ISO') + '")'
        result = subprocess.run(['osascript', '-e', script], capture_output=True, text=True)
    elif os.name == 'nt' or wsl:
        klass = 'FolderBrowserDialog' if folder else 'OpenFileDialog'
        prop = 'SelectedPath' if folder else 'FileName'
        script = 'Add-Type -AssemblyName System.Windows.Forms; $d=New-Object System.Windows.Forms.' + klass + '; '
        if not folder:
            script += '$d.Filter="ISO (*.iso)|*.iso"; '
        script += 'if($d.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK){[Console]::Write($d.' + prop + ')}'
        encoded = base64.b64encode(script.encode('utf-16le')).decode('ascii')
        result = subprocess.run(['powershell.exe', '-NoProfile', '-STA', '-EncodedCommand', encoded], capture_output=True, text=True)
    elif shutil.which('zenity'):
        args = ['zenity', '--file-selection', '--title=NFSU2 PSP']
        if folder:
            args.append('--directory')
        result = subprocess.run(args, capture_output=True, text=True)
    elif shutil.which('kdialog'):
        result = subprocess.run(['kdialog', '--getexistingdirectory' if folder else '--getopenfilename', str(Path.home())], capture_output=True, text=True)
    else:
        raise UserError('A file picker is unavailable. Paste the full path in the field. On Linux, zenity or kdialog provide a picker.')
    if result.returncode or not result.stdout.strip():
        return None
    path = result.stdout.strip()
    if wsl:
        path = subprocess.check_output(['wslpath', '-u', path], text=True).strip()
    return str(Path(path) / 'NFSU2-PSP') if folder else path


def open_local(path):
    if sys.platform == 'darwin':
        subprocess.Popen(['open', str(path)])
    elif os.environ.get('WSL_DISTRO_NAME'):
        target = subprocess.check_output(['wslpath', '-w', str(path)], text=True).strip()
        subprocess.Popen(['explorer.exe', target])
    elif os.name == 'nt':
        os.startfile(str(path))
    else:
        subprocess.Popen(['xdg-open', str(path)])


class Application:
    def __init__(self, root=ROOT):
        self.root, self.lock = root, threading.RLock()
        self.process = None
        self.process_group = None
        self.windows_job = None
        self.cancelled = threading.Event()
        self.started = None
        self.duration = None
        self.data = dict(status='idle', stage='preparation', progress=None, progress_kind='none',
                         message='Choose your ISO to get started.', logs=[], job={}, preflight=None,
                         result=None, busy=False, can_resume=False, default_output=str(root / 'output'))
        self.data['build'] = json.loads((root / 'eboot/release.json').read_text())
        self.log_path = root / '.tools/browser-generator.log'

    def update(self, **values):
        with self.lock:
            self.data.update(values)

    def state(self):
        with self.lock:
            data = copy.deepcopy(self.data)
            data['elapsed'] = self.duration if self.duration is not None else round(time.monotonic() - self.started) if self.started else 0
            return data

    def log(self, line):
        line = line.rstrip('\r\n')
        if not line:
            return
        with self.lock:
            self.data['logs'].append(line)
            self.data['logs'] = self.data['logs'][-2000:]
        self.log_path.parent.mkdir(exist_ok=True)
        with self.log_path.open('a', encoding='utf-8') as f:
            f.write(line + '\n')
        stages = {'1': 'iso', '2': 'extraction', '3': 'conversion', '4': 'conversion', '5': 'verification'}
        match = re.match(r'^([1-5])\. ', line)
        if match:
            self.update(stage=stages[match[1]], progress=None, progress_kind='none', message={'1':'Checking the ISO…','2':'Extracting source files…','3':'Converting game files…','4':'Assembling the PSP folder…','5':'Verifying output identity…'}[match[1]])
        match = re.search(r'\[\s*(\d+)\s*/\s*(\d+)\]\s*(\d+)%\s*(.+)', line)
        if match:
            self.update(stage='conversion', progress=min(99, int(match[3])), progress_kind='estimated', message='Converting files (step ' + match[1] + ' of ' + match[2] + ')')

    def launch(self, action, values):
        if action not in ('check', 'start', 'prepare'):
            raise UserError('Invalid action.')
        with self.lock:
            if self.data['busy']:
                raise UserError('A job is already running. Wait or cancel it before starting another.')
            if action != 'prepare':
                disc, out = validate_paths(values.get('iso'), values.get('output'), self.root)
                source = values.get('source', 'auto')
                if source not in ('auto', 'xbox', 'ps2'):
                    raise UserError('Invalid source.')
                self.data['job'] = dict(iso=str(disc), output=str(out), source=source)
            self.cancelled.clear()
            self.started = time.monotonic()
            self.duration = None
            self.data.update(status='preparing' if action == 'prepare' else 'checking', busy=True,
                             stage='preparation', message='Checking requirements…', progress=None,
                             progress_kind='none', result=None, logs=[])
            threading.Thread(target=self.work, args=(action,), daemon=True).start()

    def run(self, command, env=None):
        if self.cancelled.is_set():
            return 130
        command = (['nice', '-n', '15'] if os.name != 'nt' else []) + command
        proc = subprocess.Popen(command, cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, text=True, encoding='utf-8', errors='replace',
                                start_new_session=os.name != 'nt', env=env)
        if os.name == 'nt':
            try:
                self.windows_job = WindowsJob(proc)
            except BaseException:
                proc.kill(); proc.wait(); raise
        with self.lock:
            self.process = proc
            self.process_group = proc.pid
        if self.cancelled.is_set():
            self.signal_process(signal.SIGINT)
        try:
            for line in proc.stdout:
                self.log(line)
            return proc.wait()
        finally:
            if self.windows_job:
                try:
                    self.windows_job.stop()
                finally:
                    self.windows_job.close(); self.windows_job = None
            elif self.cancelled.is_set():
                self.stop_group(proc.pid)
            with self.lock:
                self.process = None
                self.process_group = None

    @staticmethod
    def group_alive(group):
        if os.name == 'nt':
            return False
        result = subprocess.run(['ps', '-A', '-o', 'pgid=', '-o', 'stat='], capture_output=True, text=True)
        if result.returncode:
            try:
                os.killpg(group, 0)
                return True
            except ProcessLookupError:
                return False
        return any(len(parts) >= 2 and parts[0] == str(group) and not parts[1].startswith('Z')
                   for parts in (line.split() for line in result.stdout.splitlines()))

    @classmethod
    def stop_group(cls, group):
        if os.name == 'nt':
            return
        for sig, seconds in ((signal.SIGTERM, 1), (signal.SIGKILL, 3)):
            try:
                os.killpg(group, sig)
            except ProcessLookupError:
                return
            end = time.monotonic() + seconds
            while cls.group_alive(group) and time.monotonic() < end:
                time.sleep(.05)
            if not cls.group_alive(group):
                return
        raise UserError('A converter is still running. Do not start another conversion until it has exited.')

    def work(self, action):
        try:
            if action == 'prepare' and not native_runtime.bundled():
                self.update(message='Checking the included runtime and converters.' if native_runtime.bundled() else 'Preparing converters. The first setup may take several minutes.')
                env = dict(os.environ, NFSU2_SETUP_APPROVED='1', JPEGLI_JOBS='4')
                code = self.run(['sh', str(self.root / 'setup.sh')], env)
                if code:
                    raise UserError('Setup did not finish. Check the log for missing system requirements.')
            py = python_runtime(self.root)
            check = subprocess.run([py, str(self.root / 'generator.py'), '--probe'], cwd=self.root,
                                   stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=90)
            try:
                preflight = json.loads(check.stdout.strip().splitlines()[-1])
            except (ValueError, IndexError):
                raise UserError('Could not check Python: ' + check.stderr[-1500:])
            self.update(preflight=preflight)
            if not preflight['ready']:
                for problem in preflight.get('details', preflight['problems']):
                    self.log(problem)
                raise UserError('The included tools are incomplete. Extract the full native download again.' if native_runtime.bundled() else 'Your computer needs setup. Check the requirements below and use Prepare this computer.')
            if action == 'prepare':
                self.update(status='idle', message='Included converters are ready. Choose your ISO and generate the game.')
                return
            job = self.state()['job']
            source = detect_source(Path(job['iso']), job['source'], self.root)
            job['source'] = source
            self.update(job=job)
            if self.cancelled.is_set():
                return
            if action == 'check':
                self.update(status='idle', stage='iso', message='Requirements are ready and the source was identified. Full ISO validation happens during generation.')
                return
            out = Path(job['output'])
            validate_paths(job['iso'], str(out), self.root)
            self.update(status='running', stage='iso', message='Checking your ISO…')
            timestamp = time.time()
            code = self.run([py, '-u', str(self.root / 'build_game.py'), job['iso'], '--saida', str(out),
                             '--fonte', source, '--jobs', '4'])
            report = None
            path = out / 'relatorio.json'
            if path.is_file() and path.stat().st_mtime >= timestamp - 1:
                report = json.loads(path.read_text())
            self.update(result=report)
            if self.cancelled.is_set():
                self.update(status='cancelled', message='Interrupted. Working files were preserved; use the same ISO and output to resume.')
            elif code == 0 and report and report.get('reference_verified') is True:
                self.update(status='complete', stage='complete', progress=100, progress_kind='steps',
                            message='The PSP folder was generated and file identity verified. Known beta limitations still apply.')
            elif code == 2 or (code == 0 and report):
                self.update(status='diagnostic', stage='verification', progress=None, progress_kind='none',
                            message='Generation ended with reference differences. Check the report; this output has not passed identity verification.')
            else:
                raise UserError('Generation did not finish. Check the last log message. Working files were preserved.')
        except Exception as exc:
            self.log(str(exc))
            self.update(status='cancelled' if self.cancelled.is_set() else 'failed', message=str(exc))
        finally:
            if self.cancelled.is_set():
                self.update(status='cancelled', message='Interrupted. No user folder was deleted; compatible steps can be resumed.')
            job = self.state()['job']
            self.update(can_resume=bool(job.get('output') and (Path(job['output']) / '.nfsu2-psp-build').is_file() and not (Path(job['output']) / 'PSP').exists()))
            if self.started:
                self.duration = round(time.monotonic() - self.started)
            if self.process_group is None:
                self.update(busy=False)

    def signal_process(self, sig):
        with self.lock:
            proc, group = self.process, self.process_group
        if group:
            try:
                if self.windows_job:
                    self.windows_job.stop()
                elif os.name != 'nt':
                    os.killpg(group, sig)
                else:
                    proc.terminate()
            except ProcessLookupError:
                pass

    def cancel(self):
        if not self.state()['busy']:
            return
        self.cancelled.set()
        self.update(status='cancelling', message='Stopping safely; wait for the converters to exit…')
        self.signal_process(signal.SIGINT)
        group = self.process_group
        if os.name == 'nt':
            return  # TerminateJobObject has already stopped the complete tree.
        def finish():
            for sig in (signal.SIGTERM, signal.SIGKILL):
                time.sleep(5)
                if self.cancelled.is_set() and self.process_group == group:
                    self.signal_process(sig)
        threading.Thread(target=finish, daemon=True).start()


def handler_for(app, token):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass  # Never put session tokens, paths or ISO metadata in HTTP logs.

        def send(self, code, value, content_type='application/json; charset=utf-8'):
            data = json.dumps(value, ensure_ascii=False).encode() if isinstance(value, (dict, list)) else value
            self.send_response(code)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Referrer-Policy', 'no-referrer')
            self.send_header('Content-Security-Policy', "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
            self.end_headers()
            self.wfile.write(data)

        def allowed_host(self):
            return self.headers.get('Host') == '127.0.0.1:' + str(self.server.server_port)

        def authorized(self):
            origin = self.headers.get('Origin')
            return self.allowed_host() and (not origin or origin == 'http://' + self.headers['Host']) and hmac.compare_digest(self.headers.get('X-NFSU2-Token', ''), token)

        def do_GET(self):
            path = urlsplit(self.path).path
            if not self.allowed_host():
                self.send(403, dict(error='Invalid host.')); return
            if path.startswith('/api/'):
                if not self.authorized():
                    self.send(403, dict(error='Reopen the launcher to start a local session.')); return
                if path == '/api/state':
                    self.send(200, app.state())
                elif path == '/api/log':
                    self.send(200, '\n'.join(app.state()['logs']).encode(), 'text/plain; charset=utf-8')
                else:
                    self.send(404, dict(error='Not found.'))
                return
            files = {'/': app.root / 'web/index.html', '/index.html': app.root / 'web/index.html',
                     '/app.js': app.root / 'web/app.js', '/style.css': app.root / 'web/style.css'}
            if path in ('/ptbr/manual-reportar-bugs.pdf', '/docs/bug-reporting-guide-EN.pdf'):
                files[path] = app.root / path.lstrip('/')
            if path in ('/ptbr/LIMITACOES.md', '/KNOWN_ISSUES.md'):
                files[path] = app.root / path.lstrip('/')
            file = files.get(path)
            if not file or not file.is_file():
                self.send(404, dict(error='Not found.')); return
            self.send(200, file.read_bytes(), (mimetypes.guess_type(file.name)[0] or 'application/octet-stream'))

        def do_POST(self):
            if not self.authorized():
                self.send(403, dict(error='Invalid local session.')); return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if size < 0 or size > 65536:
                    self.send(413, dict(error='Request too large. ISOs are not uploaded to this server.')); return
                values = json.loads(self.rfile.read(size) or '{}')
                if not isinstance(values, dict):
                    raise UserError('Invalid request.')
                path = urlsplit(self.path).path
                if path == '/api/pick':
                    kind = values.get('kind')
                    if kind not in ('iso', 'output'):
                        raise UserError('Invalid picker.')
                    picked = native_pick(kind)
                    self.send(200, dict(path=picked, cancelled=picked is None)); return
                if path in ('/api/check', '/api/start', '/api/prepare'):
                    app.launch(path.rsplit('/', 1)[1], values)
                elif path == '/api/cancel':
                    app.cancel()
                elif path == '/api/shutdown':
                    if app.state()['busy']:
                        raise UserError('Cancel the active job and wait before closing the helper.')
                    self.send(200, dict(ok=True))
                    threading.Thread(target=self.server.shutdown, daemon=True).start()
                    return
                elif path == '/api/open-output':
                    state = app.state()
                    if state['status'] not in ('complete', 'diagnostic', 'failed', 'cancelled') or not state['job'].get('output'):
                        raise UserError('There is no output folder to open yet.')
                    out = Path(state['job']['output'])
                    if not out.is_dir():
                        raise UserError('The output folder has not been created yet.')
                    open_local(out)
                else:
                    self.send(404, dict(error='Not found.')); return
                self.send(200, dict(ok=True))
            except (UserError, ValueError) as exc:
                self.send(400, dict(error=str(exc)))
            except Exception:
                self.send(500, dict(error='This action could not be completed. Check the local log.'))
    return Handler


def open_browser(url):
    if os.environ.get('WSL_DISTRO_NAME'):
        subprocess.Popen(['explorer.exe', url])
    else:
        webbrowser.open(url)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--no-browser', action='store_true')
    parser.add_argument('--port', type=int, default=0)
    args = parser.parse_args()
    if args.probe:
        result = probe(); print(json.dumps(result, ensure_ascii=False)); return 0 if result['ready'] else 1
    config = ROOT / '.tools'
    config.mkdir(exist_ok=True)
    lease = (config / 'browser-generator.lock').open('a+')
    if os.name != 'nt':
        os.chmod(lease.name, 0o600)
    try:
        lock_file(lease)
    except BlockingIOError:
        lease.seek(0)
        previous = json.loads(lease.read())
        if not args.no_browser:
            open_browser(previous['url'])
        print('NFSU2_GENERATOR_URL=' + previous['url'], flush=True)
        print('The generator is already open on this computer.'); return 0
    token = secrets.token_hex(32)
    app = Application()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), handler_for(app, token))
    url = 'http://127.0.0.1:' + str(server.server_port) + '/#token=' + token
    lease.seek(0); lease.truncate(); lease.write(json.dumps(dict(url=url, pid=os.getpid()))); lease.flush()
    print('NFSU2_GENERATOR_URL=' + url, flush=True)
    print('NFSU2 PSP — local generator opened in your browser. Keep this window open during conversion.', flush=True)
    if not args.no_browser:
        open_browser(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        app.cancel()
        while app.state()['busy']:
            time.sleep(.1)
    finally:
        server.server_close()
        lease.close()


if __name__ == '__main__':
    raise SystemExit(main())
