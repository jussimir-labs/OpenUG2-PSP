from pathlib import Path
import hashlib
import json
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import arch_tolerance as t
import build_game


def movie(frames=10, samples=4410, payload=b"j" * 1000):
    return struct.pack("<4s9I", b"N2MV", 3, 15, 480, 272, frames, 44100, samples, 40, 40 + 8 * frames) + payload


def pack(entries):
    names = sorted(entries)
    index = b"".join(struct.pack("<IIH", 0, 0, len(n)) + n.encode() + b"\0" * (-(10 + len(n)) % 4) for n in names)
    at = (16 + len(index) + 15) & ~15
    offsets = []
    for n in names:
        offsets.append(at)
        at = (at + len(entries[n]) + 15) & ~15
    index = b"".join(struct.pack("<IIH", o, len(entries[n]), len(n)) + n.encode() + b"\0" * (-(10 + len(n)) % 4)
                     for o, n in zip(offsets, names))
    out = bytearray(b"N2PK" + struct.pack("<III", 1, len(names), len(index)) + index)
    for o, n in zip(offsets, names):
        out += b"\0" * (o - len(out)) + entries[n]
    return bytes(out)


class EquivalenceTests(unittest.TestCase):
    def test_rounded_movie_is_equivalent(self):
        ref = t.describe("movie-A.n2mv", movie())
        self.assertTrue(t.equivalent("movie-A.n2mv", movie(payload=b"k" * 1000), ref))

    def test_movie_with_other_frames_or_size_is_not(self):
        ref = t.describe("movie-A.n2mv", movie())
        self.assertFalse(t.equivalent("movie-A.n2mv", movie(frames=11), ref))
        self.assertFalse(t.equivalent("movie-A.n2mv", movie(samples=4000), ref))
        self.assertFalse(t.equivalent("movie-A.n2mv", movie(payload=b"j" * 900), ref))

    def test_exact_size_kinds(self):
        ref = t.describe("music.mp3", b"a" * 100)
        self.assertTrue(t.equivalent("music.mp3", b"b" * 100, ref))
        self.assertFalse(t.equivalent("music.mp3", b"b" * 101, ref))

    def test_vinyl_catalogue_rows_must_match(self):
        def vinyls(name=b"STRIPE", blob=b"z" * 500):
            row = struct.pack("<24s24s2BH2I", name, b"", 2, 1, 0, 72, len(blob))
            return struct.pack("<4s2I", b"N2VC", 1, 1) + row + blob
        ref = t.describe("vinyls-RX8.n2vc", vinyls())
        self.assertTrue(t.equivalent("vinyls-RX8.n2vc", vinyls(blob=b"y" * 500), ref))
        self.assertFalse(t.equivalent("vinyls-RX8.n2vc", vinyls(name=b"STRIPF"), ref))

    def test_magazine_table_must_match(self):
        def mags(offset=40, image=b"i" * 64):
            return struct.pack("<4sII", b"N2MG", 1, 1) + b"MAG1\0\0\0\0" + struct.pack("<B3x", 1) + struct.pack("<B3xI", 0, offset) + image
        ref = t.describe("magazines.n2mg", mags())
        self.assertTrue(t.equivalent("magazines.n2mg", mags(image=b"j" * 64), ref))
        self.assertFalse(t.equivalent("magazines.n2mg", mags(offset=44), ref))

    def test_data_id_and_mipmap_header(self):
        self.assertTrue(t.equivalent("data-id.txt", b"0519\n", t.describe("data-id.txt", b"b23c\n")))
        self.assertFalse(t.equivalent("data-id.txt", b"zzzz\n", t.describe("data-id.txt", b"b23c\n")))
        mips = lambda size, count: struct.pack("<4s3I", b"N2MP", 1, size, count) + b"m" * 32
        self.assertTrue(t.equivalent("track-L4RC.n2mp", mips(412000, 9), t.describe("track-L4RC.n2mp", mips(411952, 9))))
        self.assertFalse(t.equivalent("track-L4RC.n2mp", mips(412000, 8), t.describe("track-L4RC.n2mp", mips(412000, 9))))

    def test_other_kinds_must_be_identical(self):
        self.assertIsNone(t.kind("races.n2rc"))
        self.assertFalse(t.equivalent("races.n2rc", b"x", {"size": 1, "shape": None}))
        self.assertFalse(t.equivalent("movie-A.n2mv", movie(), None))

    def test_pack_entries(self):
        ref = t.describe("pack.n2pk", pack({"a.n2ui": b"1234", "c.mp3": b"abcd"}))
        self.assertEqual(set(ref["entries"]), {"a.n2ui", "c.mp3"})
        self.assertTrue(t.equivalent("pack.n2pk", pack({"a.n2ui": b"1234", "c.mp3": b"wxyz"}), ref))
        self.assertFalse(t.equivalent("pack.n2pk", pack({"a.n2ui": b"1235", "c.mp3": b"abcd"}), ref))
        self.assertFalse(t.equivalent("pack.n2pk", pack({"a.n2ui": b"1234", "c.mp3": b"abcde"}), ref))
        self.assertFalse(t.equivalent("pack.n2pk", pack({"a.n2ui": b"1234"}), ref))

    def test_compare_reports_equivalent_files(self):
        with tempfile.TemporaryDirectory() as d:
            game, manifest = Path(d) / "game", Path(d) / "m.json"
            game.mkdir()
            (game / "movie-A.n2mv").write_bytes(movie())
            (game / "races.n2rc").write_bytes(b"r")
            files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in game.iterdir()}
            manifest.write_text(json.dumps({"files": files}))
            subprocess_args = [sys.executable, str(Path(t.__file__)), "describe", str(game), str(manifest)]
            import subprocess
            subprocess.run(subprocess_args, check=True, capture_output=True)
            (game / "movie-A.n2mv").write_bytes(movie(payload=b"k" * 1000))
            self.assertEqual(build_game.compare(game, manifest), (["races.n2rc"], ["movie-A.n2mv"], [], [], []))
            (game / "races.n2rc").write_bytes(b"s")
            self.assertEqual(build_game.compare(game, manifest)[2], ["races.n2rc"])


if __name__ == "__main__":
    unittest.main()
