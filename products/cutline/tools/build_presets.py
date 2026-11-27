#!/usr/bin/env python3
"""Builds the factory presets from the factory patterns.

Usage, from products/cutline:  python3 tools/build_presets.py

Writes one preset per file in patterns/ (the pattern in scene A, default parameters) and the five
MIDI Slicer presets, replacing everything in presets/. Run it again whenever a pattern changes;
tests/core/FactoryPresetTests.cpp fails if presets/ and patterns/ drift apart.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATTERNS = ROOT / "patterns"
PRESETS = ROOT / "presets"

CATEGORY = {
    "Amen": "Breaks", "Think": "Breaks", "Electro": "Breaks",
    "Neuro": "Neuro", "Halftime": "Neuro",
    "UKG": "Club", "Footwork": "Club", "Stepper": "Club", "Trap": "Club", "Drill": "Club", "Afro": "Club",
}

# The first bar of each 2.0 style's slicer mask: accents are X, other stabs x.
SLICER_GATE = {
    "Amen": "X..x..x...X...x.", "Think": "X.....x...X...x.", "Neuro": "X..x...x..X.x...",
    "Halftime": "X.....x....X....", "UKG": "X..x..X...x...X.", "Footwork": "X..x..X...x.X...",
    "Stepper": "x.Xx..X.x.Xx..X.", "Electro": "X..x..X...X.x...", "Drill": "X.xx..X..x.X..x.",
    "Afro": "X..x..X...x.X...", "Trap": "X......x..X...x.",
}

# The five 2.0 slicer presets, ported. Swing is 0.76 x the 2.0 Groove amount where the 2.0 pocket was
# MPC Swing (the two curves differ by that factor), and 0 otherwise.
SLICER_PRESETS = [
    ("slicer-ukg-chord-stabs", "Slicer: UKG Chord Stabs", "UKG",
     {"slicerStyle": "Stab Chop", "swing": 0.41, "gateLen": 0.38, "strumMs": 10.0, "slicerLatch": True}),
    ("slicer-liquid-strum", "Slicer: Liquid Strum", "Think",
     {"slicerStyle": "Strum / Flam", "swing": 0.24, "gateLen": 0.74, "strumMs": 22.0, "slicerLatch": True}),
    ("slicer-break-arp", "Slicer: Break Arp", "Amen",
     {"slicerStyle": "Break Arp", "swing": 0.20, "gateLen": 0.44, "strumMs": 8.0, "slicerLatch": True}),
    ("slicer-neuro-bass-stab", "Slicer: Neuro Bass + Stab", "Neuro",
     {"slicerStyle": "Bass / Stab", "gateLen": 0.52, "strumMs": 12.0, "slicerLatch": True}),
    ("slicer-live-clip-mangler", "Slicer: Live Clip Re-Slicer", "Drill",
     {"slicerStyle": "Clip Slice", "swing": 0.23, "gateLen": 0.62, "strumMs": 10.0}),
]


def load_patterns():
    patterns = {}
    for path in sorted(PATTERNS.glob("*.json")):
        data = json.loads(path.read_text())
        patterns[data["name"]] = data
    return patterns


def bank(pattern, gate):
    scene = {key: value for key, value in pattern.items() if key not in ("format", "name")}
    return {"scenes": [scene, {}, {}, {}], "slicer": {"length": 16, "steps": gate}}


def write(slug, preset):
    (PRESETS / f"{slug}.json").write_text(json.dumps(preset, indent=2, ensure_ascii=False) + "\n")


def main():
    patterns = load_patterns()
    missing = sorted(set(patterns) - set(CATEGORY))
    if missing:
        raise SystemExit(f"no category for pattern(s): {', '.join(missing)}")
    PRESETS.mkdir(exist_ok=True)
    for old in PRESETS.glob("*.json"):
        old.unlink()
    for name, pattern in patterns.items():
        write(name.lower(), {"format": 3, "name": name, "category": CATEGORY[name],
                             "bank": bank(pattern, SLICER_GATE[name])})
    for slug, name, source, params in SLICER_PRESETS:
        preset = {"format": 3, "name": name, "category": "MIDI Slicer", "engineMode": "MIDI Slicer"}
        preset.update(params)
        preset["bank"] = bank(patterns[source], SLICER_GATE[source])
        write(slug, preset)
    print(f"wrote {len(patterns) + len(SLICER_PRESETS)} presets to {PRESETS}")


if __name__ == "__main__":
    main()
