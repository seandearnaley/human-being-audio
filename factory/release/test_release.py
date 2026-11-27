import json
from pathlib import Path
import tempfile
import unittest

import release
import verify_product


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


class ProductIdentityTests(unittest.TestCase):
    def test_derived_vst3_class_ids_match_historical_reference_bundles(self):
        expected = {
            "livecut-redux": ("ABCDEF019182FAEB5364726E4C766374", "ABCDEF011234ABCD5364726E4C766374"),
            "tract-form": ("ABCDEF019182FAEB48626D755472666D", "ABCDEF011234ABCD48626D755472666D"),
            "pole-field": ("ABCDEF019182FAEB48626D75506C6664", "ABCDEF011234ABCD48626D75506C6664"),
            "cutline": ("ABCDEF019182FAEB48626D7543746C6E", "ABCDEF011234ABCD48626D7543746C6E"),
            "torus-glide": ("ABCDEF019182FAEB48626D755472676C", "ABCDEF011234ABCD48626D755472676C"),
            "strike-body": ("ABCDEF019182FAEB48626D7553746264", "ABCDEF011234ABCD48626D7553746264"),
            "lineage": ("ABCDEF019182FAEB48626D754C6E6731", "ABCDEF011234ABCD48626D754C6E6731"),
            "datum": ("ABCDEF019182FAEB48626D7544746D31", "ABCDEF011234ABCD48626D7544746D31"),
        }
        for slug, pair in expected.items():
            self.assertEqual(verify_product.class_ids(slug), pair)


if __name__ == "__main__":
    unittest.main()
