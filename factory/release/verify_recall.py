"""Load an old VST3 host state in the installed plugin. Requires pedalboard 0.9.26."""
import json
import sys
from pathlib import Path
from math import isclose
from pedalboard import load_plugin

# Livecut and Tract Form sessions came from the pre-branding build at e1cb736.
# Pole Field's reference session came from its first 0.1.0 candidate (PR #28, 5833dfc).
fixtures = Path(__file__).with_name("fixtures")
slug, bundle = sys.argv[1:]
expected = json.loads((fixtures / "parameters.json").read_text())[slug]
plugin = load_plugin(bundle)
plugin.raw_state = (fixtures / (slug + ".vststate")).read_bytes()
for name, value in expected.items():
    actual = plugin.parameters[name].raw_value
    if not isclose(actual, value, abs_tol=1.e-6):
        raise ValueError(f"{name}: old session restored {actual}, expected {value}")
print("Restored previous VST3 session for", slug)
