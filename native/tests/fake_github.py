#!/usr/bin/env python3
"""A local stand-in for the parts of GitHub the launcher uses.

Serves the device-code sign-in, the release list of the beta repository and
release assets (through a redirect to a storage path, as GitHub does), from a
folder of files. Every request is logged, so a test can check that the
sign-in is never sent to the storage path. Only for tests on 127.0.0.1.
"""
import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import secrets
import socket
import threading
from urllib.parse import parse_qs, urlsplit

# The repository the shipped launcher reads (native/launcher.json).
REPOSITORY = json.loads((Path(__file__).resolve().parents[1] / 'launcher.json').read_text())['repository']


class FakeGitHub:
    def __init__(self, folder, port=0, publisher='jussimir'):
        self.folder = Path(folder)
        self.publisher = publisher
        self.releases = []          # [{'tag': 'v0.2.7', 'files': [Path, ...], 'prerelease': True}]
        self.tokens = set()
        self.polls = {}
        self.log = []
        self.lock = threading.Lock()
        self.denied = False
        self.public = False         # a public repository: no sign-in needed to read it
        self.api_status = None
        self.size_delta = 0
        self.block_zip = False
        self.download_started = threading.Event()
        self.download_release = threading.Event()
        self.refresh_enabled = False
        self.refresh_tokens = set()
        self.refresh_status = 200
        self.refresh_error = None
        self.refresh_requests = 0
        self.storage_status = None
        self.storage_query = ''
        self.server = ThreadingHTTPServer(('127.0.0.1', port), self.handler())
        self.port = self.server.server_address[1]
        self.url = f'http://127.0.0.1:{self.port}'

    def publish(self, tag, files, prerelease=True, uploader=None):
        with self.lock:
            self.releases.append(dict(tag=tag, files=[Path(f) for f in files], prerelease=prerelease,
                                      uploader=uploader or self.publisher))

    def assets(self):
        out, n = {}, 1
        for r in self.releases:
            for f in r['files']:
                out[n] = (r, f)
                n += 1
        return out

    def release_json(self):
        assets = self.assets()
        listing = []
        for r in reversed(self.releases):
            listing.append(dict(
                tag_name=r['tag'], name=r['tag'] + ' - Test', draft=False, prerelease=r['prerelease'],
                html_url=f'{self.url}/{REPOSITORY}/releases/tag/{r["tag"]}',
                assets=[dict(id=i, name=f.name, size=f.stat().st_size + (self.size_delta if f.suffix == '.zip' else 0), state='uploaded',
                             uploader=dict(login=r['uploader']))
                        for i, (rr, f) in assets.items() if rr is r]))
        return listing

    def handler(self):
        fake = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def record(self):
                with fake.lock:
                    fake.log.append(dict(method=self.command, path=self.path,
                                         authorization=self.headers.get('Authorization')))

            def send(self, code, body=b'', content_type='application/json', headers=()):
                if isinstance(body, (dict, list)):
                    body = json.dumps(body).encode()
                self.send_response(code)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(body)))
                for k, v in headers:
                    self.send_header(k, v)
                self.end_headers()
                self.wfile.write(body)

            def authorized(self):
                auth = self.headers.get('Authorization', '')
                return auth.startswith('Bearer ') and auth[7:] in fake.tokens

            def do_POST(self):
                self.record()
                length = int(self.headers.get('Content-Length', 0))
                form = {k: v[0] for k, v in parse_qs(self.rfile.read(length).decode()).items()}
                if form.get('client_id') != 'test-client':
                    return self.send(401, dict(error='incorrect_client_credentials'))
                if self.path == '/login/device/code':
                    code = secrets.token_hex(8)
                    fake.polls[code] = 0
                    return self.send(200, dict(device_code=code, user_code='TEST-1234',
                                               verification_uri=f'{fake.url}/login/device',
                                               expires_in=900, interval=1))
                if self.path == '/login/oauth/access_token':
                    if fake.denied:
                        return self.send(200, dict(error='access_denied'))
                    if form.get('grant_type') == 'refresh_token':
                        fake.refresh_requests += 1
                        if fake.refresh_status != 200:
                            return self.send(fake.refresh_status, dict(error=fake.refresh_error or 'temporarily_unavailable'))
                        if fake.refresh_error or form.get('refresh_token') not in fake.refresh_tokens:
                            return self.send(200, dict(error=fake.refresh_error or 'bad_refresh_token'))
                        token = 'test-' + secrets.token_hex(12)
                        fake.tokens.add(token)
                        return self.send(200, dict(access_token=token, refresh_token=form['refresh_token'], expires_in=3600))
                    code = form.get('device_code')
                    if code not in fake.polls:
                        return self.send(200, dict(error='expired_token'))
                    fake.polls[code] += 1
                    if fake.polls[code] < 2:
                        return self.send(200, dict(error='authorization_pending'))
                    token = 'test-' + secrets.token_hex(12)
                    fake.tokens.add(token)
                    del fake.polls[code]
                    reply = dict(access_token=token, token_type='bearer', scope='')
                    if fake.refresh_enabled:
                        refresh = 'test-refresh-' + secrets.token_hex(8)
                        fake.refresh_tokens.add(refresh)
                        reply.update(refresh_token=refresh, expires_in=1)
                    return self.send(200, reply)
                self.send(404, dict(message='Not Found'))

            def do_GET(self):
                self.record()
                path = urlsplit(self.path).path
                base = f'/repos/{REPOSITORY}/'
                if path.startswith('/storage/'):
                    if fake.storage_status:
                        return self.send(fake.storage_status, dict(message='Storage unavailable'))
                    asset = fake.assets().get(int(path.rsplit('/', 1)[1]))
                    if not asset:
                        return self.send(404)
                    if fake.block_zip and asset[1].suffix == '.zip':
                        body = asset[1].read_bytes()
                        split = min(65536, max(1, len(body) - 1))
                        self.send_response(200)
                        self.send_header('Content-Length', str(len(body)))
                        self.end_headers()
                        self.wfile.write(body[:split]); self.wfile.flush()
                        fake.download_started.set()
                        if not fake.download_release.wait(15):
                            self.close_connection = True
                            return
                        self.wfile.write(body[split:])
                        return
                    return self.send(200, asset[1].read_bytes(), 'application/octet-stream')
                if not path.startswith(base):
                    return self.send(404, dict(message='Not Found'))
                if not self.headers.get('Authorization'):
                    if not fake.public:
                        # GitHub hides a private repository from anonymous readers.
                        return self.send(404, dict(message='Not Found'))
                elif not self.authorized():
                    return self.send(401, dict(message='Bad credentials'))
                rest = path[len(base):]
                if rest == 'releases':
                    if fake.api_status == -1:
                        self.close_connection = True
                        self.connection.shutdown(socket.SHUT_RDWR)
                        self.connection.close()
                        return
                    if fake.api_status:
                        return self.send(fake.api_status, dict(message='Test unavailable'))
                    query = parse_qs(urlsplit(self.path).query)
                    page, size = int(query.get('page', ['1'])[0]), int(query.get('per_page', ['100'])[0])
                    listing = fake.release_json()
                    end = page * size
                    headers = [('Link', f'<https://untrusted.invalid/?page={page + 1}>; rel="next"')] if end < len(listing) else []
                    return self.send(200, listing[(page-1)*size:end], headers=headers)
                if rest.startswith('releases/assets/'):
                    asset_id = int(rest.rsplit('/', 1)[1])
                    if asset_id not in fake.assets() or self.headers.get('Accept') != 'application/octet-stream':
                        return self.send(404, dict(message='Not Found'))
                    return self.send(302, b'', headers=[('Location', f'{fake.url}/storage/{asset_id}' + (fake.storage_query or '?signature=x'))])
                self.send(404, dict(message='Not Found'))

        return Handler

    def start(self):
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        return self

    def stop(self):
        self.server.shutdown()
        self.server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('release', nargs='*', help='TAG=FILE[,FILE...]')
    args = parser.parse_args()
    fake = FakeGitHub('.', args.port)
    for spec in args.release:
        tag, files = spec.split('=', 1)
        fake.publish(tag, files.split(','))
    print(fake.url, flush=True)
    fake.server.serve_forever()


if __name__ == '__main__':
    main()
