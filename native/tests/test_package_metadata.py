"""Package metadata policy; optional actual helper checks never execute an ISO."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

NATIVE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(NATIVE))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_bundle
import test_bundle
from test_launcher import make_bundle, Local, local_platform
from fake_github import FakeGitHub

PLATFORM = local_platform()
EXE = 'nfsu2-generator.exe' if os.name == 'nt' else 'nfsu2-generator'


class MetadataPolicy(unittest.TestCase):
    def test_seal_and_archive_omit_only_exact_finder_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'fixture'; root.mkdir()
            (root / 'eboot').mkdir()
            (root / 'eboot/release.json').write_text('{"candidate":"fixture"}')
            (root / 'lowercase').mkdir()
            for name in ['.DS_Store', 'eboot/.DS_Store', 'lowercase/.ds_store', 'keep.bin']:
                (root / name).write_bytes(b'fixture')
            for name in ['.tools/private-log', '.updates/launcher.lock']:
                f = root / name; f.parent.mkdir(exist_ok=True); f.write_bytes(b'private state')
            with patch.object(build_bundle.subprocess, 'check_output', return_value='0' * 40):
                manifest = build_bundle.seal(root, 'fixture', 'python', {})
            self.assertEqual(manifest['launcher_protocol'], 2)
            self.assertNotIn('.DS_Store', manifest['files'])
            self.assertNotIn('eboot/.DS_Store', manifest['files'])
            self.assertIn('lowercase/.ds_store', manifest['files'])
            self.assertIn('keep.bin', manifest['files'])
            self.assertNotIn('.tools/private-log', manifest['files'])
            self.assertNotIn('.updates/launcher.lock', manifest['files'])
            # Resealing an existing QA composition must not hash its own old
            # bundle.json and create an impossible self-referential digest.
            with patch.object(build_bundle.subprocess, 'check_output', return_value='0' * 40):
                manifest = build_bundle.seal(root, 'fixture', 'python', {})
            self.assertNotIn('bundle.json', manifest['files'])
            (root / 'native-qa.json').write_text('{}')
            # Old manifests can still contain metadata; new archives remove
            # those entries as well as omitting the corresponding ZIP members.
            manifest['files']['.DS_Store'] = hashlib.sha256(b'fixture').hexdigest()
            manifest['files']['.updates/launcher.lock'] = hashlib.sha256(b'private state').hexdigest()
            (root / 'bundle.json').write_text(json.dumps(manifest))
            archive = test_bundle.archive_package(root)
            with zipfile.ZipFile(archive) as z:
                self.assertFalse(any(Path(n).name == '.DS_Store' for n in z.namelist()))
                archived = json.loads(z.read('fixture/bundle.json'))
                self.assertNotIn('.DS_Store', archived['files'])
                self.assertNotIn('.updates/launcher.lock', archived['files'])
                self.assertFalse(any(Path(n).parts[1] in {'.tools', '.updates'} for n in z.namelist()))
                self.assertIn('fixture/lowercase/.ds_store', z.namelist())
            self.assertEqual((root / '.updates/launcher.lock').read_bytes(), b'private state')
            self.assertEqual((root / '.tools/private-log').read_bytes(), b'private state')

    @unittest.skipUnless(os.environ.get('NFSU2_TEST_HELPER') and os.environ.get('NFSU2_TEST_STUB'), 'actual platform binaries required')
    def test_native_metadata_and_required_files_stay_strict(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = make_bundle(Path(tmp), '0.2.6', PLATFORM, os.environ['NFSU2_TEST_HELPER'], os.environ['NFSU2_TEST_STUB'])
            exe = root / EXE
            env = dict(os.environ, NFSU2_UPDATES='0', NFSU2_NO_BROWSER='1')
            def doctor():
                return subprocess.run([exe, 'doctor', '--no-update'], env=env, capture_output=True, text=True, timeout=10)
            metadata = root / 'runtime/python/.DS_Store'
            metadata.write_bytes(b'new OS metadata')
            self.assertEqual(doctor().returncode, 0)
            metadata.write_bytes(b'recreated OS metadata')
            self.assertEqual(doctor().returncode, 0)
            manifest_path = root / 'bundle.json'
            manifest = json.loads(manifest_path.read_text())
            original = json.loads(manifest_path.read_text())
            rel = metadata.relative_to(root).as_posix()
            manifest['files'][rel] = hashlib.sha256(b'old metadata').hexdigest()
            manifest_path.write_text(json.dumps(manifest))
            out = doctor()
            self.assertNotEqual(out.returncode, 0)
            self.assertIn('changed or corrupted', out.stderr)
            # A manifest choosing metadata as its required Python has NO bypass.
            manifest['python'] = rel
            manifest_path.write_text(json.dumps(manifest))
            out = doctor()
            self.assertNotEqual(out.returncode, 0)
            self.assertIn('changed or corrupted', out.stderr)
            manifest_path.write_text(json.dumps(original))
            victim = root / ('runtime/bin/xaencode.exe' if os.name == 'nt' else 'runtime/bin/xaencode')
            original_converter = victim.read_bytes()
            victim.write_bytes(original_converter + b'changed')
            out = doctor()
            self.assertNotEqual(out.returncode, 0)
            self.assertIn('changed or corrupted', out.stderr)
            victim.write_bytes(original_converter)
            runtime = root / original['python']
            runtime.write_bytes(runtime.read_bytes() + b'changed')
            out = doctor()
            self.assertNotEqual(out.returncode, 0)
            self.assertIn('changed or corrupted', out.stderr)

    @unittest.skipUnless(os.environ.get('NFSU2_TEST_HELPER') and os.environ.get('NFSU2_TEST_STUB'), 'actual platform binaries required')
    def test_stage_accepts_only_regular_finder_metadata_extra(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            fake = FakeGitHub(work).start()
            try:
                base = make_bundle(work / 'base', '0.2.6', PLATFORM, os.environ['NFSU2_TEST_HELPER'], os.environ['NFSU2_TEST_STUB'])
                runner = Local(work / 'installed'); runner.base.mkdir()
                installed = Path(runner.put(base))
                env = dict(NFSU2_UPDATE_API=fake.url, NFSU2_UPDATE_WEB=fake.url, NFSU2_GITHUB_CLIENT_ID='test-client', NFSU2_NO_BROWSER='1', NFSU2_VAULT='session')
                for version, extra in [('0.2.7', '.DS_Store'), ('0.2.8', 'unlisted.bin')]:
                    folder = make_bundle(work / version, version, PLATFORM, os.environ['NFSU2_TEST_HELPER'], os.environ['NFSU2_TEST_STUB'])
                    code, out = runner.run(str(folder / EXE), ['doctor', '--no-update'], env, None)
                    self.assertEqual(code, 0, out)
                    self.assertTrue((folder / '.updates/launcher.lock').is_file())
                    (folder / '.tools').mkdir()
                    (folder / '.tools/private-log').write_bytes(b'private operational state')
                    (folder / 'native-qa.json').write_text('{}')
                    zipped = test_bundle.archive_package(folder)
                    with zipfile.ZipFile(zipped) as z:
                        self.assertFalse(any(Path(n).parts[1] in {'.tools', '.updates'} for n in z.namelist()))
                    self.assertTrue((folder / '.updates/launcher.lock').is_file())
                    self.assertTrue((folder / '.tools/private-log').is_file())
                    with zipfile.ZipFile(zipped, 'a', zipfile.ZIP_DEFLATED) as z:
                        z.writestr(folder.name + '/runtime/python/' + extra, b'OS metadata or unlisted payload')
                    sidecar = zipped.with_suffix('.zip.sha256')
                    sidecar.write_text(hashlib.sha256(zipped.read_bytes()).hexdigest() + '  ' + zipped.name + '\n')
                    fake.releases = []; fake.publish('v' + version, [zipped, sidecar])
                    code, out = runner.run(str(installed / EXE), ['update', '--yes'], env, fake.port)
                    self.assertEqual(code, 0, out)
                    selected = json.loads((installed / '.updates/current.json').read_text())
                    self.assertEqual(selected['version'], '0.2.7', out)
                    if version == '0.2.8': self.assertIn('unlisted file', out)
            finally:
                fake.stop()


if __name__ == '__main__':
    unittest.main()
