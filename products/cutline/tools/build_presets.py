#!/usr/bin/env python3
"""Builds the factory presets from the factory patterns, and reports how busy every scene is.

Usage, from products/cutline:
    python3 tools/build_presets.py           # rewrite presets/ and print the density report
    python3 tools/build_presets.py --report  # print the report only

Every drum preset fills all four scenes from patterns/ (A main groove, B a variation, C a breakdown, D the
peak) and leaves Fill Off. The five MIDI Slicer presets keep one scene. The manifest below is the only place
that says which pattern goes where; tests/core/FactoryPresetTests.cpp fails if presets/ and patterns/ drift
apart, so run this again whenever a pattern changes.

Nobody can listen while this content is made, so the report prints note-ons per bar for every scene at
Density 0%, 50% and 100%, and flags scenes that are empty or busier than the factory targets.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATTERNS = ROOT / "patterns"
PRESETS = ROOT / "presets"

DEFAULT_GATE = "x..x..x...x...x."

# The first bar of each 2.0 style's slicer mask: accents are X, other stabs x. Keyed by the pattern in scene A.
SLICER_GATE = {
    "Amen": "X..x..x...X...x.", "Think": "X.....x...X...x.", "Neuro": "X..x...x..X.x...",
    "Halftime": "X.....x....X....", "UKG": "X..x..X...x...X.", "Footwork": "X..x..X...x.X...",
    "Stepper": "x.Xx..X.x.Xx..X.", "Electro": "X..x..X...X.x...", "Drill": "X.xx..X..x.X..x.",
    "Afro": "X..x..X...x.X...", "Trap": "X......x..X...x.",
}

# slug, name, category, parameters, scenes A (main), B (variation), C (breakdown), D (peak).
DRUM_PRESETS = [
    ("rollers", "Rollers", "Drum & Bass", {"swing": 0.1},
     ["Roller", "Two-Step", "Halftime", "Roller Peak"]),
    ("neuro", "Neuro", "Drum & Bass", {},
     ["Neuro", "Neuro Var", "Halftime Var", "Neuro Peak"]),
    ("jump-up", "Jump Up", "Drum & Bass", {},
     ["Jump Up", "Dancefloor", "Halftime", "Jump Up Peak"]),
    ("liquid", "Liquid", "Drum & Bass", {"swing": 0.18},
     ["Liquid", "Think", "Minimal", "Roller Peak"]),
    ("techstep", "Techstep", "Drum & Bass", {},
     ["Techstep", "Two-Step", "Minimal", "Neuro Peak"]),
    ("halftime", "Halftime", "Halftime", {"swing": 0.2},
     ["Halftime", "Halftime Var", "Dubstep", "Neuro Peak"]),
    ("amen", "Amen", "Jungle", {"swing": 0.08},
     ["Amen", "Amen Var", "Jungle Dub", "Amen Rinse"]),
    ("think", "Think", "Jungle", {"swing": 0.12},
     ["Think", "Think Var", "Jungle Dub", "Amen"]),
    ("breaks", "Breaks", "Breaks", {"swing": 0.12},
     ["Breaks", "Electro", "Minimal", "Amen Var"]),
    ("ukg", "UK Garage", "Club", {"swing": 0.41},
     ["UKG", "UKG Var", "Minimal", "House"]),
    ("afro", "Afro House", "Club", {"swing": 0.2},
     ["Afro", "House", "Stepper", "Techno"]),
    ("footwork", "Footwork", "Club", {},
     ["Footwork", "Footwork Var", "Dubstep", "Amen Var"]),
    ("bass-140", "Bass 140", "Bass Music", {"swing": 0.1},
     ["Dubstep", "Trap", "Stepper", "Drill"]),
    # Strike Body is one voice per instance: these patterns mute the lane that would retrigger the wrong drum.
    ("strike-body-neuro-snare", "Strike Body: Neuro Snare", "Strike Body", {"noteMap": "Strike Body"},
     ["SB Neuro Snare", "SB Neuro Snare Var", "SB Snare Halftime", "SB Snare Rush"]),
    ("strike-body-amen-ghosts", "Strike Body: Amen Ghosts", "Strike Body", {"noteMap": "Strike Body", "swing": 0.08},
     ["SB Amen Ghosts", "SB Amen Ghosts Var", "SB Snare Halftime", "SB Snare Rush"]),
    ("strike-body-kick-engine", "Strike Body: Kick Engine", "Strike Body", {"noteMap": "Strike Body"},
     ["SB Kick", "SB Kick Var", "SB Kick Halftime", "SB Kick Peak"]),
    ("strike-body-metallic-poly", "Strike Body: Metallic Poly", "Strike Body", {"noteMap": "Strike Body"},
     ["SB Metal Poly", "SB Metal Poly Var", "SB Snare Halftime", "SB Metal Peak"]),
]

# The five 2.0 slicer presets, ported. Swing is 0.76 x the 2.0 Groove amount where the 2.0 pocket was
# MPC Swing (the two curves differ by that factor), and 0 otherwise. One scene: the slicer follows its kick.
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

# Report targets, in note-ons per bar. Strike Body drives a single drum, so it is only checked for silence and
# for excess.
BUSY_AT_HALF = 22
BUSY_AT_FULL = 30
SPARSE_AT_HALF = 8

LAYERS = ("core", "groove", "detail")


def load_patterns():
    patterns = {}
    for path in sorted(PATTERNS.glob("*.json")):
        data = json.loads(path.read_text())
        if data["name"] in patterns:
            raise SystemExit(f"two patterns are called {data['name']!r}")
        patterns[data["name"]] = data
    return patterns


def scene(pattern):
    return {key: value for key, value in pattern.items() if key not in ("format", "name", "category")}


def bank(scenes, gate):
    return {"scenes": scenes, "slicer": {"length": 16, "steps": gate}}


def note_ons_per_bar(pattern):
    """Note-ons in each bar at Density 0%, 50% and 100%, the way resolveBar counts them.

    At those three values Density needs no ranking: 0% is Core, 50% adds every Groove step, 100% adds every
    Detail step. Ratchet hits and flams count; muted lanes, lanes silenced by a solo and steps hidden by a lane's
    loop length do not.
    """
    bars = pattern.get("bars", 1)
    steps = bars * 16
    cells = {}
    for key, lane in pattern.get("lanes", {}).items():
        for layer, name in enumerate(LAYERS):
            text = lane.get(name, "").replace("|", "").replace(" ", "")
            for step, char in enumerate(text):
                if char != ".":
                    cells[(key, step)] = [layer, 1]
    for edit in pattern.get("edits", []):
        if not edit.get("fill") and (edit["lane"], edit["step"]) in cells:
            cells[(edit["lane"], edit["step"])][1] = edit.get("ratchet", 1) + (1 if edit.get("flam") else 0)
    lanes = pattern.get("lanes", {})
    solo = any(lane.get("solo") for lane in lanes.values())
    rows = []
    for bar in range(bars):
        row = [0, 0, 0]
        for key, lane in lanes.items():
            if lane.get("muted") or (solo and not lane.get("solo")):
                continue
            length = lane.get("length", 0)
            for step in range(bar * 16, bar * 16 + 16):
                cell = cells.get((key, step % length if 1 <= length < steps else step))
                if cell is None:
                    continue
                for density in range(cell[0], 3):
                    row[density] += cell[1]
        rows.append(tuple(row))
    return rows


def report(patterns):
    print(f"{'preset':<28} {'scene':<22} note-ons per bar at Density 0/50/100%")
    flagged = 0
    half = []
    for _, name, category, _, scenes in DRUM_PRESETS:
        for letter, pattern_name in zip("ABCD", scenes):
            rows = note_ons_per_bar(patterns[pattern_name])
            flags = []
            if any(r[1] == 0 for r in rows):
                flags.append("empty bar")
            if any(r[1] > BUSY_AT_HALF for r in rows) or any(r[2] > BUSY_AT_FULL for r in rows):
                flags.append("busy")
            if category != "Strike Body" and any(r[1] < SPARSE_AT_HALF for r in rows):
                flags.append("sparse")
            half.extend(r[1] for r in rows)
            flagged += bool(flags)
            bars = "  ".join(f"{a:>2}/{b:>2}/{c:>2}" for a, b, c in rows)
            note = f"  !! {', '.join(flags)}" if flags else ""
            print(f"{name:<28} {letter} {pattern_name:<20} {bars}{note}")
    print(f"at Density 50%: {min(half)} to {max(half)} note-ons per bar; {flagged} scene(s) flagged")
    return flagged


def write(slug, preset):
    (PRESETS / f"{slug}.json").write_text(json.dumps(preset, indent=2, ensure_ascii=False) + "\n")


def build(patterns):
    used = {name for *_, scenes in DRUM_PRESETS for name in scenes}
    unknown = sorted(used - set(patterns))
    if unknown:
        raise SystemExit(f"manifest names missing pattern(s): {', '.join(unknown)}")
    unused = sorted(set(patterns) - used)
    if unused:
        raise SystemExit(f"pattern(s) used by no preset: {', '.join(unused)}")
    PRESETS.mkdir(exist_ok=True)
    for old in PRESETS.glob("*.json"):
        old.unlink()
    for slug, name, category, params, scenes in DRUM_PRESETS:
        preset = {"format": 3, "name": name, "category": category}
        preset.update(params)
        gate = SLICER_GATE.get(scenes[0], DEFAULT_GATE)
        preset["bank"] = bank([scene(patterns[s]) for s in scenes], gate)
        write(slug, preset)
    for slug, name, source, params in SLICER_PRESETS:
        preset = {"format": 3, "name": name, "category": "MIDI Slicer", "engineMode": "MIDI Slicer"}
        preset.update(params)
        preset["bank"] = bank([scene(patterns[source]), {}, {}, {}], SLICER_GATE[source])
        write(slug, preset)
    print(f"wrote {len(DRUM_PRESETS) + len(SLICER_PRESETS)} presets to {PRESETS}")


def main():
    patterns = load_patterns()
    if "--report" not in sys.argv[1:]:
        build(patterns)
    report(patterns)


if __name__ == "__main__":
    main()
