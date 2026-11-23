import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import psp_iso as iso


def minimal_pbp(path):
    fields = {b"CATEGORY": b"MG\0", b"DISC_ID": b"TEST00001\0", b"TITLE": b"NFSU2 PSP v0.4.1-rc.21\0"}
    table, keys, values = bytearray(), bytearray(), bytearray()
    for name, value in fields.items():
        table += struct.pack("<HHIII", len(keys), 0x204, len(value), len(value), len(values))
        keys += name + b"\0"
        values += value
    key_at = 20 + len(table)
    sfo = struct.pack("<4s4I", b"\0PSF", 0x101, key_at, key_at + len(keys), len(fields)) + table + keys + values
    parts = [sfo, b"icon", b"video", b"", b"background", b"audio", b"\x7fELF-test", b""]
    offsets = [40 + sum(map(len, parts[:i])) for i in range(len(parts))]
    path.write_bytes(struct.pack("<4s9I", b"\0PBP", 0x10000, *offsets) + b"".join(parts))


def pack(path, items):
    index, data = bytearray(), bytearray()
    names = [n.encode("ascii") for n, _ in items]
    index_size = sum((10 + len(n) + 3) & ~3 for n in names)
    for (name, payload), encoded in zip(items, names):
        entry = struct.pack("<IIH", 16 + index_size + len(data), len(payload), len(encoded)) + encoded
        index += entry.ljust((len(entry) + 3) & ~3, b"\0")
        data += payload
    path.write_bytes(struct.pack("<4sIII", b"N2PK", 1, len(items), index_size) + index + data)


def game(root, many=0):
    g = root / "game"
    g.mkdir()
    minimal_pbp(g / "EBOOT.PBP")
    (g / "city.n2cy").write_bytes(b"c" * 5000)
    (g / "movie-EALOGO.n2mv").write_bytes(b"movie-test")
    (g / "career.sav").write_bytes(b"save stays out")
    for i in range(many):           # a SYSDIR over several sectors
        (g / f"track-{i:03d}.n2tk").write_bytes(bytes([i % 251]) * (i * 37 + 1))
    pack(g / "pack.n2pk", [("status-options.n2ui", b"packed-test"), ("hud.n2ui", b"hud")])
    return g


class PSPISOTest(unittest.TestCase):
    def test_alias_agrees_with_the_game(self):
        self.assertEqual(iso.disc_asset_name("movie-EALOGO.n2mv"), "n2_5dd1d8f0.bin")
        self.assertEqual(iso.disc_asset_name("MOVIE-EALOGO.N2MV"), "n2_5dd1d8f0.bin")
        self.assertEqual(iso.disc_asset_name("city.n2cy"), "city.n2cy")

    def test_export_reads_back_and_keeps_saves_out(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            g = game(root, many=120)
            out = root / "NFSU2-PSP.iso"
            assets, files, size = iso.export(g, out, when=1795000000)
            self.assertEqual(assets, 124)              # city, movie, 120 tracks, 2 packed
            self.assertEqual(size % 2048, 0)
            self.assertFalse(out.with_name(out.name + ".part").exists())
            raw = out.read_bytes()
            self.assertNotIn(b"save stays out", raw)
            self.assertIn(b"NFSU2 ISO v0.4.1-rc.21\0", raw)
            self.assertEqual(raw[16 * 2048 + 883:16 * 2048 + 915], iso.DISC_LABEL)
            # a second export with the same time is the same image
            again = root / "again.iso"
            iso.export(g, again, when=1795000000)
            self.assertEqual(raw, again.read_bytes())

    def test_verify_catches_a_bad_boot_name_and_xa(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            g = game(root)
            tree, _ = iso.disc_tree(g)
            out = root / "t.iso"
            digests = iso.write_iso(tree, out, 1795000000)
            iso.verify(out, digests)
            raw = bytearray(out.read_bytes())
            at = raw.index(b"EBOOT.BIN\0") + 9
            raw[at] = ord("*")
            bad = root / "bad.iso"
            bad.write_bytes(raw)
            with self.assertRaises(ValueError):
                iso.verify(bad, digests)
            raw[at] = 0
            xa = raw.index(b"XA", 23 * 2048)
            raw[xa:xa + 2] = b"XX"
            bad.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, "CD-XA"):
                iso.verify(bad, digests)
            raw[xa:xa + 2] = b"XA"
            city = raw.index(b"c" * 5000)
            raw[city] = ord("d")
            bad.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, "differs"):
                iso.verify(bad, digests)

    @unittest.skipUnless(shutil.which("mkisofs") and shutil.which("isoinfo"), "mkisofs/isoinfo for the comparison")
    def test_same_disc_as_mkisofs(self):
        """The files, sizes and layout tools/psp_iso_prototype.py gets from mkisofs; only times differ."""
        proto = Path(__file__).resolve().parents[2] / "tools/psp_iso_prototype.py"
        if not proto.is_file():
            self.skipTest("tools/psp_iso_prototype.py is outside the kit")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            g = game(root, many=120)
            ours, theirs = root / "ours.iso", root / "theirs.iso"
            iso.export(g, ours)
            subprocess.run([sys.executable, str(proto), str(g), str(theirs)], check=True, capture_output=True)

            def listing(p):
                names = subprocess.check_output(["isoinfo", "-i", str(p), "-f"], text=True).split()
                return {n: subprocess.run(["isoinfo", "-i", str(p), "-x", n], capture_output=True).stdout for n in names}
            self.assertEqual(listing(ours), listing(theirs))
            a, b = ours.read_bytes(), theirs.read_bytes()
            self.assertEqual(len(a), len(b))
            differ = {i // 2048 for i in range(len(a)) if a[i] != b[i]}
            self.assertLessEqual(differ, {16, 18} | set(range(23, 23 + 8)))   # dates, mkisofs's note, record dates


if __name__ == "__main__":
    unittest.main()
