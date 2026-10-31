"""The steps' environment keeps the converters inside the work folder: an
output folder inside a git repository (the developers' reports/) must not
lead them to the repository's main checkout and its PS2 disc. No ISO."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import build_game as bg
import ps2_buttons

PROBE = 'import sys; sys.path.insert(0, "tools"); import ps2_buttons; print(ps2_buttons.main_checkout()); print(ps2_buttons.PS2_GAME)'


@unittest.skipUnless(shutil.which('git'), 'git is not installed')
class StepEnvTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name).resolve() / 'repo'
        self.repo.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.repo)], check=True)
        (self.repo / ps2_buttons.DISC).mkdir(parents=True)   # the developer's PS2 disc beside the code
        # the generator's work folder under the repository, the tools copied in as build_game does
        self.work = self.repo / 'reports/gen/xbox/.trabalho'
        (self.work / 'tools').mkdir(parents=True)
        for name in ('ps2_buttons.py', 'n2formats.py'):
            shutil.copy2(ROOT / 'tools' / name, self.work / 'tools' / name)

    def probe(self, source):
        env = bg.step_env(self.work, argparse.Namespace(name=source))
        out = subprocess.run([sys.executable, '-c', PROBE], cwd=self.work, env=env, capture_output=True, text=True, check=True)
        return out.stdout.split('\n')[:2]

    def test_xbox_steps_do_not_find_the_repository_disc(self):
        main, game = self.probe('xbox')
        self.assertEqual(main, 'None')
        self.assertFalse(Path(game).is_dir(), game)   # so the port's own pad icons (ps2_buttons.require)
        self.assertFalse(Path(game).is_relative_to(self.repo / ps2_buttons.DISC))

    def test_ps2_steps_use_the_extracted_disc(self):
        _, game = self.probe('ps2')
        self.assertEqual(Path(game), self.work / 'game/ps2/NFSUNDER')

    def test_git_stops_at_the_work_folder(self):
        env = bg.step_env(self.work, argparse.Namespace(name='xbox'))
        r = subprocess.run(['git', '-C', str(self.work), 'rev-parse', '--git-common-dir'], env=env, capture_output=True)
        self.assertNotEqual(r.returncode, 0)
        r = subprocess.run(['git', '-C', str(self.work / 'tools'), 'rev-parse', '--git-common-dir'], env=env, capture_output=True)
        self.assertNotEqual(r.returncode, 0)


if __name__ == '__main__':
    unittest.main()
