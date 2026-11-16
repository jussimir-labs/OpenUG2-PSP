"""The kit's EBOOT without the game's art, and the card's EBOOT with the art from the player's disc."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import pbp

KIT = ROOT / 'eboot/EBOOT.PBP'


def bare():
    """The kit's executable without any XMB art (the kit may carry its reviewed XMB, pbp.KIT_XMB)."""
    return pbp.strip(KIT.read_bytes())


def png(w, h, value=0):
    """A real w x h RGBA PNG of one colour."""
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    rows = b''.join(b'\0' + bytes([value, value, value, 255]) * w for _ in range(h))
    return (pbp.PNG + chunk(b'IHDR', struct.pack('>2I5B', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


class KitEbootTests(unittest.TestCase):
    def test_kit_eboot_matches_release_and_has_no_art(self):
        data = KIT.read_bytes()
        release = json.loads((ROOT / 'eboot/release.json').read_text())
        self.assertEqual(hashlib.sha256(data).hexdigest(), release['eboot_sha256'])
        self.assertTrue(pbp.art_of(data) == [] or pbp.kit_xmb(data), pbp.art_of(data))
        parts = pbp.sections(data)
        self.assertTrue(parts['PARAM.SFO'] and parts['DATA.PSP'])

    def test_art_in_and_out_keeps_the_executable(self):
        kit = bare()
        card = pbp.with_art(kit, png(144, 80, 10), png(480, 272, 20))
        a, b = pbp.sections(kit), pbp.sections(card)
        for name in ('PARAM.SFO', 'DATA.PSP', 'DATA.PSAR'):
            self.assertEqual(a[name], b[name])
        self.assertEqual(b['ICON0.PNG'], png(144, 80, 10))
        self.assertEqual(b['PIC1.PNG'], png(480, 272, 20))
        self.assertEqual([n for n, _ in pbp.art_of(card)], ['ICON0.PNG', 'PIC1.PNG'])
        self.assertEqual(pbp.strip(card), kit)

    def test_wrong_sizes_and_non_pbp_are_refused(self):
        kit = bare()
        with self.assertRaises(ValueError):
            pbp.with_art(kit, png(480, 272), png(144, 80))
        with self.assertRaises(ValueError):
            pbp.sections(b'not a pbp' * 8)

    def test_package_puts_the_dist_art_in_the_eboot(self):
        import ms_package
        with tempfile.TemporaryDirectory() as tmp:
            KIT_BARE = Path(tmp) / 'EBOOT-bare.PBP'
            KIT_BARE.write_bytes(bare())
            dist = Path(tmp) / 'dist'
            dist.mkdir()
            (dist / 'icon0.png').write_bytes(png(144, 80, 30))
            (dist / 'pic1.png').write_bytes(png(480, 272, 40))
            (dist / 'fixture.n2ph').write_bytes(b'N2PH fixture')
            ms_package.make(Path(tmp) / 'out', dist, KIT_BARE, [], patch_bars=False, pack=True, release=True, xmb_art=True)
            game = Path(tmp) / 'out/PSP/GAME/NFSU2VIEW'
            # the art also stays in the data: the saves' icon (savedata.c reads icon0.png)
            self.assertEqual(pbp.pack_entry(game, 'icon0.png'), png(144, 80, 30))
            self.assertEqual(pbp.card_problems(game, KIT_BARE), [])
            card = pbp.sections((game / 'EBOOT.PBP').read_bytes())
            self.assertEqual(card['PIC1.PNG'], png(480, 272, 40))
            # another folder's art is not this one's
            (dist / 'pic1.png').write_bytes(png(480, 272, 41))
            ms_package.make(Path(tmp) / 'other', dist, KIT_BARE, [], patch_bars=False, pack=True, release=True, xmb_art=False)
            self.assertIn('PIC1.PNG is not the folder\'s pic1.png', pbp.card_problems(Path(tmp) / 'other/PSP/GAME/NFSU2VIEW', KIT_BARE))

    def test_kit_xmb_is_kept_and_only_the_reviewed_one_passes(self):
        import ms_package
        data = KIT.read_bytes()
        with self.assertRaises(ValueError):   # any other art is not the reviewed kit XMB
            pbp.with_xmb(bare(), {'ICON0.PNG': png(144, 80, 1), 'ICON1.PMF': b'x', 'PIC1.PNG': png(480, 272, 2), 'SND0.AT3': b'y'})
        if not pbp.kit_xmb(data):
            self.skipTest('this kit carries no XMB')
        with tempfile.TemporaryDirectory() as tmp:
            dist = Path(tmp) / 'dist'
            dist.mkdir()
            (dist / 'icon0.png').write_bytes(png(144, 80, 30))
            (dist / 'pic1.png').write_bytes(png(480, 272, 40))
            ms_package.make(Path(tmp) / 'out', dist, KIT, [], patch_bars=False, pack=True, release=True, xmb_art=True)
            game = Path(tmp) / 'out/PSP/GAME/NFSU2VIEW'
            self.assertEqual((game / 'EBOOT.PBP').read_bytes(), data)   # the kit's XMB, not the disc's
            self.assertEqual(pbp.card_problems(game, KIT), [])

    def test_package_without_the_art_stops(self):
        import ms_package
        with tempfile.TemporaryDirectory() as tmp:
            dist = Path(tmp) / 'dist'
            dist.mkdir()
            (dist / 'icon0.png').write_bytes(png(144, 80))
            kit = Path(tmp) / 'EBOOT-bare.PBP'
            kit.write_bytes(bare())   # (a kit without its own XMB takes the disc's, and needs both pictures)
            with self.assertRaises(SystemExit):
                ms_package.make(Path(tmp) / 'out', dist, kit, [], patch_bars=False, release=True, xmb_art=True)


if __name__ == '__main__':
    unittest.main()
