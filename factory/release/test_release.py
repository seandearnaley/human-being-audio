import json
from pathlib import Path
import tempfile
import unittest

import release


class ReleaseRecordTests(unittest.TestCase):
    def test_manifest_binds_all_three_archives_to_the_source_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            product = release.product("tract-form")
            for filename in product["assets"].values():
                (directory / filename).write_bytes(filename.encode())
            release.record(product, directory, "a" * 40)
            record = json.loads((directory / "release.json").read_text())
            self.assertEqual(record["source"], "a" * 40)
            self.assertEqual(set(record["downloads"]), {"macos", "windows", "linux"})
            for row in record["downloads"].values():
                self.assertEqual(len(row["sha256"]), 64)
                self.assertEqual(row["bytes"], len(row["file"]))
                self.assertIn(row["file"], (directory / "SHA256SUMS").read_text())

    def test_missing_package_cannot_produce_release_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(FileNotFoundError):
                release.record(release.product("livecut-redux"), Path(tmp), "a" * 40)
            self.assertFalse((Path(tmp) / "release.json").exists())

if __name__ == "__main__":
    unittest.main()

class ProductNotesTests(unittest.TestCase):
    def test_notes_bind_current_product_to_exact_source(self):
        sha = "f" * 40
        for slug in release.PRODUCTS:
            item = release.product(slug)
            text = release.notes(item, sha)
            self.assertIn(item["name"], text)
            self.assertIn(item["version"], text)
            self.assertIn(f"/tree/{sha}/products/{slug}", text)
            self.assertIn("SHA256SUMS", text)
