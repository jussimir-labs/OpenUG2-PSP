import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import ps2_buttons


class OwnIconTests(unittest.TestCase):
    def test_own_icons_cover_every_prompt(self):
        icons = ps2_buttons.own_icons()
        self.assertEqual(set(icons), set(ps2_buttons.NAMES) | {"PSP_L", "PSP_R"})
        for icon in icons.values():
            self.assertEqual((icon.w, icon.h, len(icon.pixels)), (32, 32, 32 * 32 * 4))

    def test_own_icons_are_identical_on_every_platform(self):
        # Xbox-only players get these in their HUD files; the reference
        # manifest holds their bytes, so every CPU must draw the same ones.
        icons = ps2_buttons.own_icons()
        digest = hashlib.sha256(b"".join(icons[n].pixels for n in sorted(icons))).hexdigest()
        self.assertEqual(digest, "789b34cb99eedb4b2a0249b1ce51905c34aa75af1fa8d38bb7bc64ef4abb38d6")

    def test_without_ps2_disc_own_icons_stand_in(self):
        with tempfile.TemporaryDirectory() as empty:
            icons = ps2_buttons.require(Path(empty))
        self.assertEqual(set(icons), set(ps2_buttons.own_icons()))


if __name__ == "__main__":
    unittest.main()
