#!/usr/bin/env python3
"""One-off: convert Cutline 2.0 style tables (engine/src/styles.cpp) into Cutline 3 pattern files.

Usage: python3 tools/seed_patterns.py engine/src/styles.cpp patterns
Deleted together with styles.cpp once the engine no longer uses it.
"""
import json
import re
import sys
from pathlib import Path

NAMES = ["Amen", "Think", "Neuro", "Halftime", "UKG", "Footwork", "Stepper", "Electro", "Drill", "Afro", "Trap"]
LANE_KEYS = ["kick", "snare", "ghost", "clap", "hat", "openHat", "percA", "percB"]
LANE_PRIORITY = [0, 1, 4, 5, 3, 2, 6, 7]  # kick, snare, closed hat, open hat, clap, ghost, perc A, perc B
SNARE_FAMILY = (1, 2, 3)
CORE, GROOVE, DETAIL = 0, 1, 2
LAYER_KEYS = ["core", "groove", "detail"]
BARS = 2
STEPS = BARS * 16

# One drawn fill per seed: (beats, {lane key: 16-character string}).
FILL_SNARE = (2, {"kick": "........x.......", "snare": "........x.x.x.xX"})
FILL_TOMS = (2, {"percB": "........xx......", "percA": "..........xx....", "snare": "............xx..", "kick": "..............x."})
FILL_PICKUP = (1, {"kick": "............x...", "snare": "..............xX"})
FILL_DROP = (2, {"kick": "........X.......", "snare": "............X...", "clap": "............x...", "openHat": "..............x."})
FILLS = {"Amen": FILL_SNARE, "Think": FILL_SNARE, "Neuro": FILL_PICKUP, "Halftime": FILL_DROP,
         "UKG": FILL_PICKUP, "Footwork": FILL_TOMS, "Stepper": FILL_PICKUP, "Electro": FILL_TOMS,
         "Drill": FILL_SNARE, "Afro": FILL_TOMS, "Trap": FILL_DROP}


def m32(steps):
    mask = 0
    for s in steps:
        if 0 <= s < 32:
            mask |= 1 << s
    return mask


def dup16(steps):
    bar = m32(steps)
    return (bar | (bar << 16)) & 0xFFFFFFFF


MASK_RE = re.compile(r"(m32|dup16)\(\{([^}]*)\}\)")


def parse_masks(text):
    """Reads a C++ initialiser list of m32({...}) / dup16({...}) calls without evaluating any code."""
    out = []
    for fn, body in MASK_RE.findall(text):
        steps = [int(v) for v in body.split(",") if v.strip()]
        out.append(dup16(steps) if fn == "dup16" else m32(steps))
    return out


STYLE_RE = re.compile(
    r"\.coreMask\s*=\s*\{(?P<core>.*?)\},\s*"
    r"\.grooveMask\s*=\s*\{(?P<groove>.*?)\},\s*"
    r"\.spiceMask\s*=\s*\{(?P<spice>.*?)\},\s*"
    r"\.accentMask\s*=\s*\{(?P<accent>.*?)\},\s*"
    r"\.defaultLen\s*=\s*\{(?P<len>.*?)\},.*?"
    r"\.pocketMs\s*=\s*\{(?P<pocket>.*?)\},",
    re.S)


def parse_styles(source):
    styles = []
    for m in STYLE_RE.finditer(source):
        def masks(key):
            values = parse_masks(m.group(key))
            return values + [0] * (8 - len(values))
        styles.append({
            "core": masks("core"), "groove": masks("groove"), "spice": masks("spice"), "accent": masks("accent"),
            "len": [int(v) for v in m.group("len").split(",")],
            "pocket": [float(v) for v in m.group("pocket").split(",")],
        })
    return styles


def metric_weight(step_in_bar):
    if step_in_bar == 0:
        return 0
    if step_in_bar == 8:
        return 1
    if step_in_bar in (4, 12):
        return 2
    return 3 if step_in_bar % 2 == 0 else 4


def rank_key(lane, step):
    return (metric_weight(step % 16), LANE_PRIORITY.index(lane), step // 16, step)


def build(style):
    """Returns (cells, lengths): cells[lane][stored step] = [layer, velocity char]; lengths[lane] = 0 or loop length."""
    cells = [dict() for _ in range(8)]
    lengths = [0] * 8
    for lane in range(8):
        length = style["len"][lane]
        stored = STEPS if length >= 16 else length
        if length < 16:
            lengths[lane] = length

        def bit(mask, step):
            if length >= 32:
                return bool(mask >> step & 1)
            if length == 16:
                return bool(mask >> (step % 16) & 1)
            return any(mask >> b & 1 and b % length == step for b in range(32))

        for step in range(stored):
            if bit(style["core"][lane], step):
                layer = CORE
            elif bit(style["groove"][lane], step):
                layer = GROOVE
            elif bit(style["spice"][lane], step):
                layer = DETAIL
            else:
                continue
            char = "o" if lane == 2 else ("X" if bit(style["accent"][lane], step) else "x")
            cells[lane][step] = [layer, char]
    return cells, lengths


def stored_step(lengths, lane, step):
    return step % lengths[lane] if lengths[lane] else step


def bar_hits(cells, lengths, bar, layers, lanes=range(8)):
    """Stored (lane, step) pairs sounding in `bar` for the given layers, one entry per sounding pattern step."""
    out = []
    for lane in lanes:
        for step in range(bar * 16, bar * 16 + 16):
            src = stored_step(lengths, lane, step)
            cell = cells[lane].get(src)
            if cell and cell[0] in layers:
                out.append((lane, src))
    return out


def curate(cells, lengths):
    # Anchor: snare Core must be the same in every bar.
    for step, cell in list(cells[1].items()):
        if cell[0] == CORE:
            twin = cells[1].get((step + 16) % STEPS)
            if not twin or twin[0] != CORE:
                cell[0] = GROOVE

    def demote(bar, layers, lanes, limit, to, protect=lambda lane, step: False):
        while True:
            hits = bar_hits(cells, lengths, bar, layers, lanes)
            if len(hits) <= limit:
                return
            movable = [h for h in set(hits) if cells[h[0]][h[1]][0] == to - 1 and not protect(*h)]
            if not movable:
                raise SystemExit(f"cannot meet budget {limit} in bar {bar}")
            lane, step = max(movable, key=lambda h: rank_key(*h))
            cells[lane][step][0] = to
            # Keep bars alike: a full-length lane demotes the same step in the other bar too.
            twin = cells[lane].get((step + 16) % STEPS) if not lengths[lane] else None
            if twin and twin[0] == to - 1 and not protect(lane, (step + 16) % STEPS):
                twin[0] = to

    for bar in range(BARS):
        demote(bar, (CORE,), range(8), 16, GROOVE,
               protect=lambda lane, step: lane == 1 or (lane == 0 and step % 16 == 0))
    for bar in range(BARS):
        demote(bar, (GROOVE,), SNARE_FAMILY, 3, DETAIL)
    for bar in range(BARS):
        demote(bar, (CORE, GROOVE), range(8), 24, DETAIL)


def lint(name, cells, lengths):
    assert all(cells[0].get(b * 16, [None])[0] == CORE for b in range(BARS)), f"{name}: kick anchor"
    snare_core = [sorted(s % 16 for s, c in cells[1].items() if c[0] == CORE and s // 16 == b) for b in range(BARS)]
    assert snare_core[0] and all(sc == snare_core[0] for sc in snare_core), f"{name}: snare anchor {snare_core}"
    rows = []
    for bar in range(BARS):
        core = len(bar_hits(cells, lengths, bar, (CORE,)))
        half = len(bar_hits(cells, lengths, bar, (CORE, GROOVE)))
        fam = len(bar_hits(cells, lengths, bar, (GROOVE,), SNARE_FAMILY))
        assert core <= 16 and half <= 24 and fam <= 3, f"{name} bar {bar}: {core}/{half}/{fam}"
        rows.append((core, half, fam))
    return rows


def lane_strings(cells, lane, layer):
    chars = ["."] * STEPS
    for step, (cell_layer, char) in cells[lane].items():
        if cell_layer == layer:
            chars[step] = char
    text = "".join(chars)
    return None if set(text) == {"."} else text[:16] + "|" + text[16:]


def to_json(name, style, cells, lengths):
    lanes = {}
    for lane, key in enumerate(LANE_KEYS):
        entry = {}
        for layer, layer_key in enumerate(LAYER_KEYS):
            text = lane_strings(cells, lane, layer)
            if text:
                entry[layer_key] = text
        if not entry:
            continue
        if lengths[lane]:
            entry["length"] = lengths[lane]
        offset = int(round(style["pocket"][lane]))
        if offset:
            entry["offsetMs"] = offset
        lanes[key] = entry
    beats, fill_lanes = FILLS[name]
    return {"format": 3, "name": name, "bars": BARS, "lanes": lanes, "fill": {"beats": beats, "lanes": fill_lanes}}


def main():
    source = Path(sys.argv[1]).read_text()
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    styles = parse_styles(source)
    assert len(styles) >= len(NAMES), f"parsed {len(styles)} styles"
    for name, style in zip(NAMES, styles):
        cells, lengths = build(style)
        curate(cells, lengths)
        rows = lint(name, cells, lengths)
        path = out_dir / (name.lower() + ".json")
        path.write_text(json.dumps(to_json(name, style, cells, lengths), indent=2) + "\n")
        print(f"{name:9} core/50%/snare-family per bar: {rows}  -> {path}")


if __name__ == "__main__":
    main()
