import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import native_runtime as runtime


class RuntimeTests(unittest.TestCase):
    def test_missing_bundle_never_compiles_or_uses_path(self):
        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.dict(os.environ, NFSU2_BUNDLE_ROOT=tmp),
            patch.object(runtime.subprocess, "run") as run,
        ):
            with self.assertRaises(RuntimeError):
                runtime.helper("xaencode", Path(tmp) / "output", [])
            run.assert_not_called()

    def test_packaged_tool_uses_absolute_path(self):
        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.dict(os.environ, NFSU2_BUNDLE_ROOT=tmp),
        ):
            exe = (
                Path(tmp)
                / "runtime/bin"
                / ("xaencode.exe" if os.name == "nt" else "xaencode")
            )
            exe.parent.mkdir(parents=True)
            exe.write_bytes(b"fixture")
            with patch.object(runtime.subprocess, "run") as run:
                self.assertEqual(
                    runtime.helper("xaencode", Path(tmp) / "output", []), exe.resolve()
                )
                run.assert_not_called()

    def test_resume_rejects_changed_native_runtime(self):
        import argparse
        import build_game

        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp).resolve()
            root = base / "tool"
            root.mkdir()
            source = root / "build_game.py"
            source.write_text("# unchanged source")
            manifest = root / "bundle.json"
            manifest.write_text('{"runtime":"first"}')
            args = argparse.Namespace(
                sem_filmes=False, jobs=4, aceitar_iso_desconhecida=False
            )
            console = argparse.Namespace(name="xbox")
            release = {"candidate": "fixture", "eboot_sha256": "fixed-game"}
            with (
                patch.object(build_game, "HERE", root),
                patch.object(build_game, "__file__", str(source)),
                patch.dict(os.environ, NFSU2_BUNDLE_ROOT=str(root)),
            ):
                first = build_game.build_identity("same-iso", console, args, release)
                output = base / "output"
                build_game.prepare_out(output, False, first)
                retained = output / "unique-result"
                retained.write_text("keep")
                manifest.write_text('{"runtime":"updated"}')
                second = build_game.build_identity("same-iso", console, args, release)
                with self.assertRaises(build_game.Failure):
                    build_game.prepare_out(output, False, second)
                self.assertEqual(retained.read_text(), "keep")

    @unittest.skipUnless(os.name == "nt", "Windows verbatim paths")
    def test_verbatim_paths_keep_tool_folder_protected(self):
        import generator

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve() / "tool"
            root.mkdir()
            iso = root.parent / "fixture.iso"
            iso.write_bytes(b"fixture")
            verbatim = Path("\\\\?\\" + str(root))
            self.assertEqual(runtime.local_path(verbatim), runtime.local_path(root))
            for output in [root, root / "tools/nested"]:
                with self.assertRaises(generator.UserError):
                    generator.validate_paths(str(iso), str(output), verbatim)

    @unittest.skipUnless(os.name == "nt", "Windows Job Object integration")
    def test_cancel_stops_windows_descendants(self):
        import ctypes as c
        from ctypes import wintypes as w
        import generator

        k = c.WinDLL("kernel32", use_last_error=True)
        k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        k.OpenProcess.restype = w.HANDLE
        k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
        k.WaitForSingleObject.restype = w.DWORD
        k.CloseHandle.argtypes = [w.HANDLE]
        k.CloseHandle.restype = w.BOOL
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "eboot").mkdir()
            (root / "eboot/release.json").write_text('{"candidate":"fixture"}')
            app = generator.Application(root)
            app.data["busy"] = True
            pid = root / "pid"
            errors = []
            code = "import subprocess,sys,time;from pathlib import Path;p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)']);Path(sys.argv[1]).write_text(str(p.pid));time.sleep(60)"

            def work():
                try:
                    app.run([sys.executable, "-c", code, str(pid)])
                except BaseException as exc:
                    errors.append(exc)

            t = threading.Thread(target=work)
            t.start()
            until = time.monotonic() + 10
            while not pid.exists() and time.monotonic() < until:
                time.sleep(0.03)
            self.assertTrue(pid.exists())
            handle = k.OpenProcess(0x100000, False, int(pid.read_text()))
            self.assertTrue(handle)
            try:
                app.cancel()
                t.join(10)
                self.assertFalse(t.is_alive())
                self.assertEqual(errors, [])
                self.assertEqual(
                    k.WaitForSingleObject(handle, 5000),
                    0,
                    "A descendant converter survived cancellation",
                )
            finally:
                k.CloseHandle(handle)


if __name__ == "__main__":
    unittest.main()
