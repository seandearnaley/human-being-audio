# /// script
# requires-python = ">=3.10"
# dependencies = ["Pillow"]
# ///
"""Check the actual artwork loaded by the two installers and About screens."""
from pathlib import Path
import unittest

from PIL import Image


class StudioArtworkTests(unittest.TestCase):
    def test_native_artwork_preserves_the_mark(self):
        assets = {"mark.png": ((192, 192), 192), "welcome.png": ((150, 180), 112),
                  "header.bmp": ((150, 57), 46), "welcome.bmp": ((164, 314), 118)}
        for filename, (size, mark_width) in assets.items():
            with self.subTest(filename=filename), Image.open(Path(__file__).with_name(filename)) as image:
                self.assertEqual(image.size, size)
                rgba = image.convert("RGBA")
                x, y = image.width // 2, image.height // 2
                offset = round(mark_width * 0.22)
                blue = rgba.getpixel((x - offset, y - offset))
                warm = rgba.getpixel((x + offset, y + offset))
                cut = rgba.getpixel((x, y))
                self.assertEqual((blue[3], warm[3], cut[3]), (255, 255, 255))
                self.assertGreater(blue[2], blue[0] + 30)
                self.assertGreater(warm[0], blue[0] + 12)
                self.assertLess(max(cut[:3]), 45)
                corner = rgba.getpixel((0, 0))
                if filename.endswith(".png"):
                    self.assertEqual(corner[3], 0)
                else:
                    self.assertEqual(image.mode, "RGB")


if __name__ == "__main__":
    unittest.main()
