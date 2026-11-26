import json
import tempfile
import unittest
from pathlib import Path

import native


class NativeQualityTests(unittest.TestCase):
    products = ["livecut-redux", "tract-form", "pole-field", "future-instrument"]

    def test_new_product_inherits_checks_and_shared_changes_reach_every_consumer(self):
        self.assertEqual(native.affected(self.products, ["products/future-instrument/engine/new.cpp"]), ["future-instrument"])
        self.assertEqual(native.affected(self.products, ["modules/dsp/include/hba/dsp/gain.h"]), self.products)
        self.assertEqual(native.affected(self.products, ["web/src/example.ts"]), [])
        self.assertEqual(native.affected(self.products, [".clang-tidy"]), self.products)

    def test_editor_probe_and_research_formats_trigger_their_consumers(self):
        self.assertEqual(native.affected(self.products, ["factory/release/standalone-linux.py"]), self.products)
        self.assertEqual(native.plan_checks(self.products, ["research/idea.cpp"])["jobs"], ["format"])

    def test_header_checks_consumers_and_source_change_only_checks_its_translation_unit(self):
        sources = {"products/p/src/a.cpp", "products/p/src/b.cpp", "modules/dsp/tests/Gain.cpp"}
        self.assertEqual(native.lint_inputs("p", sources, ["products/p/src/a.cpp"]), {"products/p/src/a.cpp"})
        self.assertEqual(native.lint_inputs("p", sources, ["products/p/src/a.h"]), sources)
        self.assertEqual(native.lint_inputs("p", sources, ["modules/dsp/include/gain.h"]), sources)
        self.assertEqual(native.lint_inputs("p", sources, ["products/other/src/a.h"]), set())

    def test_docs_only_and_web_changes_do_not_wait_for_an_unselected_plugin(self):
        self.assertEqual(native.plan_checks(self.products, ["README.md"])["jobs"], [])
        self.assertEqual(native.plan_checks(self.products, ["web/src/index.ts"])["jobs"], ["website"])
        self.assertEqual(native.plan_checks(self.products, ["products/pole-field/docs/releases/2.md"])["jobs"], ["factory"])
        plan = native.plan_checks(self.products, ["products/future-instrument/core/new.cpp"])
        self.assertEqual(plan["products"], ["future-instrument"])
        self.assertEqual(plan["macos"], ["future-instrument"])
        self.assertEqual(plan["windows"], ["future-instrument"])
        self.assertEqual(set(plan["jobs"]), {"ubuntu", "format", "realtime", "windows", "macos"})

    def test_modules_and_macos_workflow_changes_trigger_dedicated_jobs(self):
        mod_plan = native.plan_checks(self.products, ["modules/core/include/hba/core/preset_json.h"])
        self.assertIn("modules", mod_plan["jobs"])
        self.assertIn("macos", mod_plan["jobs"])
        self.assertIn("livecut", mod_plan["jobs"])

        mac_plan = native.plan_checks(self.products, [".github/workflows/macos-product.yml"])
        self.assertEqual(mac_plan["jobs"], ["macos"])
        self.assertEqual(mac_plan["macos"], ["tract-form", "pole-field", "future-instrument"])

    def test_unreachable_or_zero_base_falls_back_to_full_build(self):
        self.assertIsNone(native.changed_files(""))
        self.assertIsNone(native.changed_files("0" * 40))
        self.assertIsNone(native.changed_files("deadbeef" * 5))

    def test_editor_index_reads_the_real_commands_for_a_new_catalog_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            product = root / "products" / "new"
            build = product / "build" / "dev"
            build.mkdir(parents=True)
            command = dict(directory=str(build), file=str(product / "engine.cpp"), arguments=["clang++", "-c", "engine.cpp"])
            vendor = dict(command, file=str(product / "libs/JUCE/vendor.cpp"))
            generated = dict(command, file=str(build / "BinaryData.cpp"))
            (build / "compile_commands.json").write_text(json.dumps([command, vendor, generated, command]))
            commands, indexed = native.editor_commands(root, ["new"])
            self.assertEqual(commands, [command])
            self.assertEqual(indexed, ["new"])
            commands_avail, indexed_avail = native.editor_commands(root, ["new", "missing"], skip_missing=True)
            self.assertEqual(commands_avail, [command])
            self.assertEqual(indexed_avail, ["new"])
            with self.assertRaises(FileNotFoundError):
                native.editor_commands(root, ["missing"])
