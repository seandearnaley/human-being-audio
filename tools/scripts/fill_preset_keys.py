#!/usr/bin/env python3
"""Adds any parameter a preset file is missing, at its default, keeping what is there.

Presets write every key out so they do not shift when a default changes. When a
parameter is added to core/include/tf/params.h, run this to bring the bank up to date:

    build/engine/tools/tf-render --dump-defaults > defaults.json
    tools/scripts/fill_preset_keys.py defaults.json presets/*.json

`quality` is a machine setting rather than part of a sound, so presets leave it out.
"""
import json
import sys

SKIP = ("name", "category", "quality")
defaults = json.load(open(sys.argv[1]))
for path in sys.argv[2:]:
    preset = json.load(open(path))
    added = [key for key in defaults if key not in preset and key not in SKIP]
    for key in added:
        preset[key] = defaults[key]
    with open(path, "w") as out:
        json.dump(preset, out, indent=2, ensure_ascii=False)
        out.write("\n")
    if added:
        print(f"{path}: added {', '.join(added)}")
