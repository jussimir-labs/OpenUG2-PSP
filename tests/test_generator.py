"""Browser helper safety and lifecycle tests; no ISO conversion or installs."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import generator as g


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / 'tool'
        (self.root / 'eboot').mkdir(parents=True)
        (self.root / 'eboot/release.json').write_text(json.dumps({'candidate': 'fixture', 'known_issues': ['still-open']}))
        (self.root / 'web').mkdir()
        (self.root / 'web/index.html').write_text('<h1>Fixture</h1>')
        self.iso = self.base / 'my disc.iso'; self.iso.write_bytes(b'not a real game')
        self.out = self.base / 'output'
        self.app = g.Application(self.root)

class HelperTests(Fixture):
    def test_paths_spaces_preserved(self):
        disc, out = g.validate_paths(str(self.iso), str(self.out), self.root)
        self.assertEqual(disc, self.iso)
        self.assertEqual(out, self.out)

    def test_missing_iso_rejected(self):
        with self.assertRaises(g.UserError):
            g.validate_paths(str(self.base/'missing.iso'), str(self.out), self.root)

    def test_wrong_extension_rejected(self):
        f=self.base/'partial.crdownload';f.write_bytes(b'x')
        with self.assertRaises(g.UserError):g.validate_paths(str(f),str(self.out),self.root)

    def test_tool_or_iso_ancestor_rejected(self):
        for out in (self.root, self.base):
            with self.assertRaises(g.UserError):g.validate_paths(str(self.iso),str(out),self.root)

    def test_user_folder_not_deleted(self):
        self.out.mkdir();p=self.out/'unique.txt';p.write_text('keep')
        with self.assertRaises(g.UserError):g.validate_paths(str(self.iso),str(self.out),self.root)
        self.assertEqual(p.read_text(),'keep')

    def test_nested_saves_rejected_even_with_marker(self):
        (self.out/'nested/PSP/SAVEDATA').mkdir(parents=True)
        (self.out/'.nfsu2-psp-build').write_text('{}')
        with self.assertRaises(g.UserError):g.validate_paths(str(self.iso),str(self.out),self.root)
        self.assertTrue((self.out/'nested/PSP/SAVEDATA').is_dir())

    def test_existing_psp_folder_preserved(self):
        (self.out/'PSP').mkdir(parents=True);(self.out/'.nfsu2-psp-build').write_text('{}')
        with self.assertRaises(g.UserError):g.validate_paths(str(self.iso),str(self.out),self.root)
        self.assertTrue((self.out/'PSP').is_dir())

    def test_resume_marker_allowed(self):
        self.out.mkdir();(self.out/'.nfsu2-psp-build').write_text('{}')
        self.assertEqual(g.validate_paths(str(self.iso),str(self.out),self.root)[1],self.out)

    def test_resume_cannot_follow_directory_symlinks(self):
        self.out.mkdir();(self.out/'.nfsu2-psp-build').write_text('{}')
        external=self.base/'unique-user-folder';external.mkdir();(external/'unique.txt').write_text('keep')
        (self.out/'.trabalho').symlink_to(external,target_is_directory=True)
        with self.assertRaises(g.UserError):g.validate_paths(str(self.iso),str(self.out),self.root)
        self.assertEqual((external/'unique.txt').read_text(),'keep')

    def test_recursive_output_inside_source_tree_rejected(self):
        for part in ('tools', 'presets', 'art', 'web', 'sources'):
            with self.assertRaises(g.UserError):
                g.validate_paths(str(self.iso), str(self.root/part/'nested-output'), self.root)
        self.assertEqual(g.validate_paths(str(self.iso), str(self.root/'saida'), self.root)[1], self.root/'saida')

    def test_low_disk_rejected(self):
        with patch.object(g.shutil,'disk_usage',return_value=type('D',(),{'free':10**9})()):
            with self.assertRaises(g.UserError):g.validate_paths(str(self.iso),str(self.out),self.root)

    def test_progress_is_estimate_below_completion(self):
        self.app.log('3. Convertendo (80 etapas)')
        self.app.log('  [ 3/80]  25% cidade')
        s=self.app.state();self.assertEqual(s['stage'],'conversion');self.assertEqual(s['progress'],25);self.assertEqual(s['progress_kind'],'estimated')
        self.app.log('5. Conferindo contra a build de referência')
        self.assertIsNone(self.app.state()['progress'])

    def test_snapshot_does_not_expose_mutable_state(self):
        self.app.state()['logs'].append('other')
        self.assertEqual(self.app.state()['logs'],[])

    def work_fixture(self, code, report):
        self.app.data['job']=dict(iso=str(self.iso),output=str(self.out),source='xbox')
        self.app.data['busy']=True
        def runner(cmd,env=None):
            self.assertNotIn('--do-zero',cmd);self.assertNotIn('--sem-filmes',cmd)
            self.assertNotIn('--aceitar-iso-desconhecida',cmd)
            self.out.mkdir(exist_ok=True)
            if report is not None:(self.out/'relatorio.json').write_text(json.dumps(report))
            return code
        probe=subprocess.CompletedProcess([],0,json.dumps({'ready':True,'problems':[],'python':'3.12','platform':'test'}),'')
        with patch.object(g.subprocess,'run',return_value=probe),patch.object(g,'detect_source',return_value='xbox'),patch.object(self.app,'run',side_effect=runner):
            self.app.work('start')
        return self.app.state()

    def test_complete_requires_verified_report_and_keeps_known_issues(self):
        s=self.work_fixture(0,{'reference_verified':True,'known_issues':['still-open']})
        self.assertEqual(s['status'],'complete');self.assertEqual(s['result']['known_issues'],['still-open']);self.assertFalse(s['busy'])

    def test_mismatch_never_shows_success(self):
        self.assertEqual(self.work_fixture(2,{'reference_verified':False})['status'],'diagnostic')

    def test_missing_report_never_shows_success(self):
        self.assertEqual(self.work_fixture(0,None)['status'],'failed')

    def test_single_job(self):
        self.app.data['busy']=True
        with self.assertRaises(g.UserError):self.app.launch('prepare',{})

    @unittest.skipIf(os.name=='nt','POSIX process group test')
    def test_cancel_reaps_orphan_converter_group(self):
        pidfile=self.base/'child-pid'
        child="import time,signal;signal.signal(signal.SIGINT,signal.SIG_IGN);time.sleep(60)"
        parent="import subprocess,sys,time;from pathlib import Path;p=subprocess.Popen([sys.executable,'-c',sys.argv[2]],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);Path(sys.argv[1]).write_text(str(p.pid));time.sleep(60)"
        self.app.data['busy']=True
        result=[]
        t=threading.Thread(target=lambda:result.append(self.app.run([sys.executable,'-c',parent,str(pidfile),child])))
        t.start()
        until=time.monotonic()+5
        while not pidfile.exists() and time.monotonic()<until:time.sleep(.03)
        self.assertTrue(pidfile.exists())
        group=self.app.process_group
        self.app.cancel();t.join(8)
        self.assertFalse(t.is_alive());self.assertFalse(g.Application.group_alive(group));self.assertIsNone(self.app.process_group)


class HttpTests(Fixture):
    def setUp(self):
        super().setUp()
        self.server=g.ThreadingHTTPServer(('127.0.0.1',0),g.handler_for(self.app,'fixture-token'))
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        self.url='http://127.0.0.1:'+str(self.server.server_port)
        self.addCleanup(self.server.server_close);self.addCleanup(self.server.shutdown)

    def request(self,path,token='fixture-token',body=None,headers=None):
        h={'X-NFSU2-Token':token,**(headers or {})}
        return urlopen(Request(self.url+path,data=json.dumps(body).encode()if body is not None else None,headers=h),timeout=3)

    def test_api_needs_token(self):
        with self.assertRaises(HTTPError)as error:self.request('/api/state',token='wrong')
        self.assertEqual(error.exception.code,403)

    def test_foreign_origin_rejected(self):
        with self.assertRaises(HTTPError)as error:self.request('/api/state',headers={'Origin':'https://example.com'})
        self.assertEqual(error.exception.code,403)

    def test_foreign_host_rejected(self):
        with self.assertRaises(HTTPError)as error:self.request('/api/state',headers={'Host':'evil.example'})
        self.assertEqual(error.exception.code,403)

    def test_state_and_security_headers(self):
        with self.request('/api/state')as response:
            self.assertEqual(json.load(response)['status'],'idle');self.assertEqual(response.headers['Cache-Control'],'no-store')
            self.assertIn("frame-ancestors 'none'",response.headers['Content-Security-Policy'])

    def test_static_whitelist_no_filesystem_access(self):
        for path in ('/../eboot/release.json','/eboot/release.json','/%2e%2e/secret','/api/unknown'):
            with self.assertRaises(HTTPError)as error:self.request(path)
            self.assertEqual(error.exception.code,404)

    def test_invalid_picker_rejected_without_opening_dialog(self):
        with patch.object(g,'native_pick')as pick:
            with self.assertRaises(HTTPError):self.request('/api/pick',body={'kind':'shell'})
            pick.assert_not_called()

    def test_iso_upload_refused(self):
        with self.assertRaises(HTTPError)as error:self.request('/api/start',body={'padding':'x'*65537})
        self.assertEqual(error.exception.code,413)

    def test_shutdown_refused_while_busy(self):
        self.app.data['busy']=True
        with self.assertRaises(HTTPError)as error:self.request('/api/shutdown',body={})
        self.assertEqual(error.exception.code,400)


if __name__=='__main__':unittest.main()
