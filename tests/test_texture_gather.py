from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import n2formats


def unswizzle_loop(data, w, h, bpp):
    xs, ys = n2formats.morton_tables(w, h)
    out = bytearray(w * h * bpp)
    for y in range(h):
        for x in range(w):
            q = (xs[x] | ys[y]) * bpp
            out[(y * w + x) * bpp:(y * w + x + 1) * bpp] = data[q:q + bpp]
    return bytes(out)


def ps2_unswizzle8_loop(idx, w, h):
    out = bytearray(w * h)
    for y in range(h):
        block_row = (y & ~0xf) * w
        shift = (((y + 2) >> 2) & 1) * 4
        col = ((((y & ~3) >> 1) + (y & 1)) & 7) * w * 2
        byte = (y >> 1) & 1
        for x in range(w):
            out[y * w + x] = idx[block_row + (x & ~0xf) * 2 + col + ((x + shift) & 7) * 4 + byte + ((x >> 2) & 2)]
    return bytes(out)


class GatherTests(unittest.TestCase):
    # The gathered decoders must give the per-texel loops' bytes, with and
    # without numpy: the reference manifests hold the converted textures.
    def each_backend(self, check):
        for np in (n2formats.numpy, None):
            with self.subTest(numpy=np is not None):
                saved = n2formats.numpy
                n2formats.numpy = np
                n2formats.morton_order.cache_clear()
                n2formats.ps2_order8.cache_clear()
                try:
                    check()
                finally:
                    n2formats.numpy = saved
                    n2formats.morton_order.cache_clear()
                    n2formats.ps2_order8.cache_clear()

    def test_xbox_unswizzle(self):
        rnd = random.Random(1)
        def check():
            for w, h, bpp in ((4, 4, 1), (64, 16, 4), (16, 128, 1), (256, 256, 4)):
                data = bytes(rnd.randrange(256) for _ in range(w * h * bpp))
                self.assertEqual(bytes(n2formats.unswizzle(data, w, h, bpp)), unswizzle_loop(data, w, h, bpp))
        self.each_backend(check)

    def test_ps2_unswizzle8(self):
        rnd = random.Random(2)
        def check():
            for w, h in ((16, 4), (32, 64), (128, 16), (512, 512)):
                data = bytes(rnd.randrange(256) for _ in range(w * h))
                self.assertEqual(n2formats.ps2_unswizzle8(data, w, h), ps2_unswizzle8_loop(data, w, h))
        self.each_backend(check)

    def test_palette_lookup(self):
        rnd = random.Random(3)
        pal = bytes(rnd.randrange(256) for _ in range(1024))
        pixels = bytes(rnd.randrange(256) for _ in range(64 * 32))
        want = b"".join(pal[c * 4:c * 4 + 4] for c in pixels)
        def check():
            tex = n2formats.Texture("t", 0, 64, 32, 0x108, pixels, pal)
            self.assertEqual(bytes(tex.rgba()), want)
        self.each_backend(check)


if __name__ == "__main__":
    unittest.main()
