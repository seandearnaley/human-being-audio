"""Every product needs a saved VST3 session and independent parameter expectations."""
import json
import unittest
from pathlib import Path

from release import PRODUCTS


class RecallFixtures(unittest.TestCase):
    def test_every_studio_product_has_a_session_and_parameter_expectations(self):
        fixtures = Path(__file__).with_name("fixtures")
        parameters = json.loads((fixtures / "parameters.json").read_text())
        self.assertEqual(set(parameters), set(PRODUCTS))
        for slug in PRODUCTS:
            with self.subTest(product=slug):
                self.assertTrue((fixtures / f"{slug}.vststate").read_bytes())
                self.assertTrue(parameters[slug])
                for value in parameters[slug].values():
                    self.assertGreaterEqual(value, 0)
                    self.assertLessEqual(value, 1)
