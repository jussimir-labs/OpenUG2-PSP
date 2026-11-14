"""The steps' graph: order, stamps, the update's plan and the scheduler. No ISO."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import build_game as bg
import step_graph as sg

D = 'dist/PSP/GAME/NFSU2VIEW/'


def node(name, reads=(), writes=(), lists=(), code=('tools/x.py',), game=True, removes=()):
    return dict(name=name, commands=[], code=list(code), absent_code=[], game=game, reads=list(reads),
                writes=list(writes), removes=list(removes), lists=list(lists), programs=[])


# city -> races (reads city) ; audio alone ; hud lists the dist ; bounds rewrites city in place
NODES = [node('city', writes=[D + 'city.n2cy', D + 'city.json'], code=['tools/city.py']),
         node('races', reads=[D + 'city.n2cy', D + 'city.json'], writes=[D + 'races.n2rc'], code=['tools/races.py']),
         node('audio', writes=[D + 'music.mp3'], code=['tools/audio.py']),
         node('bounds', reads=[D + 'city.n2cy'], writes=[D + 'city.n2cy'], code=['tools/bounds.py']),
         node('lines', reads=[D + 'city.n2cy', D + 'races.n2rc'], writes=[D + 'lines.n2rl'], code=['tools/lines.py']),
         node('hud', lists=['dist/PSP/GAME/NFSU2VIEW'], writes=[D + 'hud.n2ui'], code=['tools/hud.py'])]


def graph():
    return sg.Graph({n['name']: n for n in NODES}, [n['name'] for n in NODES])


class GraphTests(unittest.TestCase):
    def test_native_trunk_code_invalidates_and_output_restores(self):
        data=sg.load(ROOT/'manifests/steps.json')
        garage=next(n for n in data['steps'] if n['name']=='garage')
        g=sg.Graph({'garage':garage},['garage'])
        before=g.stamps(lambda _: 'old',[garage['commands'][0]],'runtime','disc')
        for changed in ('tools/carparts_anim.py','tools/convert_nis.py'):
            after=g.stamps(lambda p:'new' if p==changed else 'old',[garage['commands'][0]],'runtime','disc')
            self.assertNotEqual(before,after,changed)
        name='trunk-anim.n2ta';path=D+name
        self.assertEqual(g.final[path],0)
        self.assertEqual(g.final[D+'trunk-anim.json'],0)
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=root/name;source.write_bytes(b'N2TA fixture bytes')
            work=root/'work'
            bg.restore(0,g,{'rerun':{},'cached':{},'files':{name:(source,0,None)}},work)
            self.assertEqual((work/path).read_bytes(),source.read_bytes())

    def test_restore_keeps_what_the_step_removes_and_writes_again(self):
        """An update that reuses track L4RD: its bar-4402.n2cy (convert_city.py
        unlinks it, then writes it) restored and left in place."""
        data=sg.load(ROOT/'manifests/steps.json')
        track=next(n for n in data['steps'] if n['name']=='track L4RD')
        path=D+'bar-4402.n2cy'
        self.assertIn(path,track['writes']);self.assertIn(path,track['removes'])
        g=sg.Graph({'track L4RD':track},['track L4RD'])
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=root/'bar-4402.n2cy';source.write_bytes(b'N2CY barrier fixture')
            work=root/'work'
            stale=work/D/'stale.n2cy';stale.parent.mkdir(parents=True);stale.write_bytes(b'old')
            node=dict(track,removes=list(track['removes'])+[D+'stale.n2cy'])
            g=sg.Graph({'track L4RD':node},['track L4RD'])
            bg.restore(0,g,{'rerun':{},'cached':{},'files':{'bar-4402.n2cy':(source,0,None)}},work)
            self.assertEqual((work/path).read_bytes(),source.read_bytes())
            self.assertFalse(stale.exists())

    def test_native_trunk_passes_existing_package_filters(self):
        import ms_package as mp
        import ppsspp_run
        name='trunk-anim.n2ta'
        self.assertFalse(mp.release_skip(name))
        self.assertFalse(mp.legacy(name))
        self.assertFalse(ppsspp_run.game_output(name))
        self.assertTrue(mp.packable(name,260))
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);p=root/name;p.write_bytes(b'N2TA fixture bytes')
            self.assertEqual(bg.package_files(root)[name],(p,0,None))

    def test_order(self):
        g = graph()
        self.assertEqual(g.preds[1], {0})            # races reads city's
        self.assertEqual(g.preds[2], set())          # audio: free
        self.assertEqual(g.preds[3], {0, 1})         # bounds rewrites what races read (after it)
        self.assertEqual(g.preds[4], {1, 3})         # lines reads bounds' city
        self.assertEqual(g.preds[5], {0, 1, 2, 3, 4})   # hud lists the dist: after every writer
        self.assertEqual(g.inputs[4][D + 'city.n2cy'], 3)
        self.assertEqual(g.final[D + 'city.n2cy'], 3)

    def test_code_hash_ignores_crlf_of_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            lf, crlf, png = Path(tmp) / 'a.py', Path(tmp) / 'b.py', Path(tmp) / 'c.png'
            lf.write_bytes(b'x = 1\ny = 2\n'), crlf.write_bytes(b'x = 1\r\ny = 2\r\n')
            png.write_bytes(b'\x89PNG\r\n\x1a\n\0\0\0\rIHDR')
            self.assertEqual(sg.code_sha256(lf), sg.code_sha256(crlf))
            self.assertEqual(sg.code_sha256(lf), sg.sha256(lf))
            self.assertEqual(sg.code_sha256(png), sg.sha256(png))   # binary: as it is

    def test_previous_stamps_by_package(self):
        g = graph()
        code = {'tools/city.py': 'a', 'tools/races.py': 'b', 'tools/audio.py': 'c', 'tools/bounds.py': 'd',
                'tools/lines.py': 'e', 'tools/hud.py': 'f'}
        crlf = {p: h.upper() for p, h in code.items()}   # the Windows package's CRLF files
        table = dict(schema=2, candidate='old', inputs=code, code=code,
                     commands={'ps2': {'movies': [[n, ['x']] for n in g.names], 'none': []}},
                     bundles=[dict(platform='macos-arm64', bundle_sha256='m', runtime='rt', inputs={}),
                              dict(platform='windows-x64', bundle_sha256='w', runtime='rt', inputs=crlf)])
        source = type('Source', (), {'name': 'ps2'})()
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / 'old.json').write_text(json.dumps(table))
            saved, bg.UPDATE_FROM = bg.UPDATE_FROM, Path(tmp)
            try:
                win = dict(candidate='old', tools_sha256=bg.identity_hash(dict(crlf, **{'native-bundle.json': 'w'})))
                stamps, why = bg.previous_stamps(Path(tmp), win, g, source, True, 'ps2:1')
                self.assertIsNone(why)
                self.assertEqual(stamps, g.stamps(code.get, [['x']] * len(g.names), 'rt', 'ps2:1'))
                lf_marker = dict(win, tools_sha256=bg.identity_hash(dict(code, **{'native-bundle.json': 'w'})))
                self.assertIsNone(bg.previous_stamps(Path(tmp), lf_marker, g, source, True, 'ps2:1')[0])
                # the published version of that generation: the marker's, else its table's
                self.assertIsNone(bg.previous_version(win))
                (Path(tmp) / 'old.json').write_text(json.dumps(dict(table, version='0.1.1')))
                self.assertEqual(bg.previous_version(win), '0.1.1')
                self.assertIsNone(bg.previous_version(lf_marker))
                self.assertEqual(bg.previous_version(dict(win, version='0.2.0')), '0.2.0')
            finally:
                bg.UPDATE_FROM = saved

    def test_glob_only_its_names(self):
        nodes = NODES[:5] + [node('career', writes=[D + 'career.n2cr'], code=['tools/career.py'])]
        nodes[5]['globs'] = [D + 'races*.n2rc']
        g = sg.Graph({n['name']: n for n in nodes}, [n['name'] for n in nodes])
        self.assertEqual(g.preds[5], {1})            # the races' writer only, not the whole dist's
        self.assertEqual(g.inputs[5], {D + 'races.n2rc': 1})

    @unittest.skipIf(sys.platform == 'win32', 'the step tracer (NFSU2_STEP_TRACE) is a maintainers\' tool, '
                     'run on macOS and Linux to build manifests/steps.json')
    def test_tracer_globs(self):
        import os
        import subprocess
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(os.path.realpath(tmp)) / 'dist'
            d.mkdir()
            (d / 'a.n2rc').write_bytes(b''), (d / 'b.txt').write_bytes(b'')
            log = d.parent / 'trace.log'
            env = dict(os.environ, NFSU2_TRACE_LOG=str(log), NFSU2_TRACE_ROOTS=str(d.parent),
                       PYTHONPATH=str(ROOT / 'tools' / 'step_trace'))
            code = ('import glob, os, pathlib, sys; d = sys.argv[1];'
                    'print(sorted(p.name for p in pathlib.Path(d).glob("*.n2rc")), glob.glob(d + "/*.txt"),'
                    'sorted(os.listdir(d)))')
            out = subprocess.run([sys.executable, '-c', code, str(d)], env=env, capture_output=True, text=True,
                                 check=True).stdout
            # glob joins with the system's separator, and the printed list is a repr (on
            # Windows, its backslashes doubled)
            self.assertEqual(out.strip(), f"['a.n2rc'] {[os.path.join(str(d), 'b.txt')]} ['a.n2rc', 'b.txt']")
            events = [line.split('\t')[::2] for line in log.read_text().splitlines() if line[0] in 'gl']
            self.assertEqual(events, [['g', f'{d}/*.n2rc'], ['g', f'{d}/*.txt'], ['l', str(d)]])

    def test_stamps_follow_producers(self):
        g = graph()
        code = {'tools/city.py': 'a', 'tools/races.py': 'b', 'tools/audio.py': 'c', 'tools/bounds.py': 'd',
                'tools/lines.py': 'e', 'tools/hud.py': 'f'}
        cmds = [['x']] * len(NODES)
        one = g.stamps(code.get, cmds, 'rt', 'ps2:1')
        two = g.stamps(dict(code, **{'tools/races.py': 'B'}).get, cmds, 'rt', 'ps2:1')
        self.assertEqual([a == b for a, b in zip(one, two)], [True, False, True, True, False, False])
        self.assertNotEqual(one, g.stamps(code.get, cmds, 'rt', 'ps2:2'))
        self.assertNotEqual(one, g.stamps(code.get, cmds, 'other', 'ps2:1'))

    def test_template(self):
        self.assertEqual(sg.template(['tools/a.py', '--jobs', '4', '--game', 'game/ps2/NFSUNDER'], 'game/ps2/NFSUNDER'),
                         ['tools/a.py', '--game', '{game}'])
        self.assertEqual(sg.template([('glob', 'tools/b.py', '--if-missing')]), ['glob', 'tools/b.py', '--if-missing'])


class PlanTests(unittest.TestCase):
    def test_actual_update_plan_reuses_unchanged_steps(self):
        """Real planner/reference hashes and restore, with tiny synthetic files."""
        import argparse
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp); out=base/'output'; game=out/'PSP/GAME/NFSU2VIEW'
            game.mkdir(parents=True)
            for name in ['EBOOT.PBP','city.n2cy','races.n2rc','music.mp3','lines.n2rl','hud.n2ui']:
                (game/name).write_bytes(('old '+name).encode())
            refs=base/'reference'; (refs/'manifests').mkdir(parents=True)
            manifest=refs/'manifests/fixture-ps2.json'
            manifest.write_text(json.dumps({'files':{p.name:sg.sha256(p) for p in game.iterdir()}}))
            old=dict(source='ps2',iso_sha256='aa',movies=True,candidate='fixture',tools_sha256='old-tools')
            g=graph(); before=['stamp-'+name for name in g.names]; after=before.copy(); after[2]='changed-audio'
            state=out/bg.STATE; state.parent.mkdir(parents=True,exist_ok=True)
            state.write_text(json.dumps(dict(tools_sha256='old-tools',disc='ps2:aa',stamps=dict(zip(g.names,before)))))
            with patch.object(bg,'HERE',refs):
                plan, why=bg.update_plan(out,old,g,after,argparse.Namespace(name='ps2'),
                                         argparse.Namespace(skip_movies=False),'ps2:aa')
            self.assertIsNone(why); self.assertEqual(set(plan['rerun']),{2})
            work=base/'work'
            for i in range(len(g.names)):
                if i not in plan['rerun']: bg.restore(i,g,plan,work)
            self.assertEqual((work/D/'races.n2rc').read_bytes(),b'old races.n2rc')
            self.assertFalse((work/D/'music.mp3').exists(),'changed audio is not reused')
            self.assertEqual((game/'music.mp3').read_bytes(),b'old music.mp3','previous game unchanged')

    def plan(self, changed, files):
        g = graph()
        rerun = {i: 'changed' for i in changed}
        restorable = lambda p: p.startswith(D) and p[len(D):] in files   # noqa: E731
        moved = True
        while moved:
            moved = False
            for i in sorted(rerun):
                for p, j in g.inputs[i].items():
                    if j not in rerun and not (restorable(p) and g.final.get(p) == j):
                        rerun[j] = 'input'
                        moved = True
        return sorted(rerun)

    def test_rerun_needs_unrestorable_and_overwritten(self):
        pkg = {'city.n2cy', 'races.n2rc', 'music.mp3', 'lines.n2rl', 'hud.n2ui'}   # no city.json (not packaged)
        self.assertEqual(self.plan({2}, pkg), [2])                 # audio alone
        self.assertEqual(self.plan({4}, pkg), [4])                 # lines: city and races restored
        # races again: it needs city's own city.n2cy (bounds rewrote the packaged one) and city.json
        self.assertEqual(self.plan({1}, pkg), [0, 1])

    def test_plan_through_update_plan(self):
        """update_plan with a real folder: package files, pack entries, the previous stamps from STATE."""
        g = graph()
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            game = out / 'PSP/GAME/NFSU2VIEW'
            game.mkdir(parents=True)
            (game / 'EBOOT.PBP').write_bytes(b'x')
            (game / 'city.n2cy').write_bytes(b'city')
            name = b'music.mp3'
            index = struct.pack('<IIH', 32, 3, len(name)) + name + b'\0' * (-(10 + len(name)) % 4)
            (game / 'pack.n2pk').write_bytes(b'N2PK' + struct.pack('<III', 1, 1, len(index)) + index + b'\0' * 4 + b'mp3')
            files = bg.package_files(game)
            self.assertEqual(files['music.mp3'][1:], (32, 3))
            work = out / 'w'
            plan = dict(rerun={1: 'changed'}, files=files, cached={})
            bg.restore(2, g, plan, work)
            self.assertEqual((work / D / 'music.mp3').read_bytes(), b'mp3')


class SchedulerTests(unittest.TestCase):
    def test_parallel_respects_order(self):
        g = graph()
        todo = [(n['name'], 1, ['tools/x.py']) for n in NODES]
        started, ended, lock = {}, {}, threading.Lock()

        def run_one(i):
            with lock:
                started[i] = time.monotonic()
            time.sleep(0.05)
            with lock:
                ended[i] = time.monotonic()
            return 0.05
        bg.say = lambda *a: None
        bg.run_steps(todo, g, 4, set(), run_one)
        for i, ps in enumerate(g.preds):
            for j in ps:
                self.assertLessEqual(ended[j], started[i])
        self.assertLess(started[2], ended[0])    # audio beside city

    def test_failure_stops(self):
        todo = [(n['name'], 1, ['tools/x.py']) for n in NODES]
        bg.say = lambda *a: None

        def run_one(i):
            if i == 0:
                raise bg.Failure('boom')
            return 0.0
        with self.assertRaises(bg.Failure):
            bg.run_steps(todo, graph(), 4, set(), run_one)


class UpdateRefusals(unittest.TestCase):
    """update_plan says why an output cannot be updated (then: a full generation)."""
    def setUp(self):
        import argparse
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name)
        game = self.out / 'PSP/GAME/NFSU2VIEW'
        game.mkdir(parents=True)
        (game / 'EBOOT.PBP').write_bytes(b'x')
        self.old = dict(source='ps2', iso_sha256='aa', movies=True, candidate='nope', tools_sha256='t')
        self.args = argparse.Namespace(skip_movies=False)
        self.source = argparse.Namespace(name='ps2')

    def why(self, **change):
        g = graph()
        plan, why = bg.update_plan(self.out, dict(self.old, **change), g, ['s'] * len(NODES), self.source, self.args, 'ps2:aa')
        self.assertIsNone(plan)
        return why

    def test_reasons(self):
        self.assertIn('used xbox', self.why(source='xbox'))
        self.assertIn('different ISO', self.why(iso_sha256='bb'))
        self.assertIn('movie option', self.why(movies=False))
        self.assertIn('does not recognize', self.why())     # no STATE, no update-from table for it
        (self.out / 'PSP/GAME/NFSU2VIEW/EBOOT.PBP').unlink()
        self.assertIn('incomplete', self.why())

    def test_previous_generation(self):
        with self.assertRaises(bg.Failure):
            bg.previous_generation(self.out)            # no marker
        (self.out / '.nfsu2-psp-build').write_text('{}')
        self.assertEqual(bg.previous_generation(self.out), {})
        (self.out / '.anterior').mkdir()
        (self.out / 'PSP').rename(self.out / '.anterior/PSP')   # an update stopped half way
        bg.previous_generation(self.out)
        self.assertTrue((self.out / 'PSP/GAME/NFSU2VIEW').is_dir())


if __name__ == '__main__':
    unittest.main()
