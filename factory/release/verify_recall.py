"""Load an old VST3 host state in the installed plugin. Requires pedalboard 0.9.26."""
import sys
from pathlib import Path
from math import isclose
from pedalboard import load_plugin

# Saved with the pre-branding VST3s from studio commit e1cb736. The non-default
# values exercise the effect's amplitude range and two independent synth macros.
expected = {"livecut-redux": {"max_amp": 0.89, "min_amp": 0.74},
            "tract-form": {"coupling": 0.42, "warp": 0.83}}
slug, bundle = sys.argv[1:]
plugin = load_plugin(bundle)
plugin.raw_state = (Path(__file__).parent / "fixtures" / (slug + ".vststate")).read_bytes()
for name, value in expected[slug].items():
    actual = plugin.parameters[name].raw_value
    if not isclose(actual, value, abs_tol=1.e-6):
        raise ValueError(f"{name}: old session restored {actual}, expected {value}")
print("Restored previous VST3 session for", slug)
