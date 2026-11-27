# Cutline 3 pattern library: four scenes in every preset

Spec 3 of 4. Written 2026-11-26 while the founder was away; the decisions here are Claude's and can be overruled.

## Problem

Cutline 3 ships 11 patterns converted by rule from the 2.0 style tables. Each factory preset fills scene A only,
so scenes B to D are empty and a preset offers no movement. 2.0 had 27 drum presets.

## Content

1. **Patterns.** Grow `patterns/` to about 32 files, weighted to drum and bass: rollers, neuro, halftime,
   jump up, liquid, jungle (Amen and Think edits), techstep, minimal, two-step, dancefloor; plus breaks, UKG,
   footwork, dubstep, house, techno, trap, drill, afro. Each file is 1, 2 or 4 bars in the existing format
   (Core, Groove, Detail per lane, optional pocket and loop length) with one drawn fill, and passes the lint in
   spec 1. Groove and Detail are written so that Density 0% (Core only) is a usable stripped beat, 50% the main
   groove and 100% the busy version.
2. **Presets.** About 12 drum presets, each with all four scenes filled from the library:
   A main groove, B a variation of A (a different second bar or extra Detail), C a breakdown (halftime or
   stripped), D the peak (busier hats or a 4-bar version). Patterns may be shared between presets. Fill stays
   Off in every preset (founder decision: fills are off by default). The five slicer presets stay.
3. **Strike Body presets.** Four presets with the Strike Body kit map, whose patterns use only lanes Strike Body
   plays.
4. **Fill library.** `fills/` with about 12 fills (1 and 2 beats: snare rolls, kick-snare pickups, tom-style perc
   runs, hat stutters, silence-with-pickup), each passing the fill lint. The fill view's picker that loads them
   is UI work for after the grid lands.
5. `tools/build_presets.py` builds every preset from a manifest in the script (name, category, kit map, the four
   pattern names), and `FactoryPresetTests` keeps presets and patterns in sync.

Nobody can listen here. For every preset, the build reports note-ons per bar at Density 0%, 50% and 100% for
each scene, so busy or empty scenes are caught by numbers.

## As built (2026-11-26)

Data and tooling only; nothing was auditioned. Counts come from `tools/build_presets.py`, whose per-bar
numbers were checked against `resolveBar` for every pattern and matched exactly.

- **Patterns.** 46 files: 33 genre patterns and 13 Strike Body patterns. The 11 seed patterns were re-authored
  under their old names, because the converted 2.0 tables sat at the Core and 50% ceilings (16 and 24 note-ons
  per bar) and reached 77 per bar at 100% where short looped lanes repeated their Detail. Every file carries a
  top-level `"category"` for the menus (Drum & Bass, Halftime, Jungle, Breaks, Club, Bass Music, Strike Body);
  the pattern reader ignores it.
- **Shape of a pattern.** Core is kick, backbeat and the basic hat line (6 to 12 note-ons per bar). Groove adds
  the second kicks, one or two ghosts and the in-between hats (9 to 22 at 50%). Detail adds 16th hats, more
  ghosts and percussion (12 to 29 at 100%). The closed and open hat never share a step.
- **Lint additions.** Density 100% is at most 32 note-ons per bar. Pattern names are unique, because presets
  are built by name.
- **Presets.** 13 drum presets, 4 Strike Body presets and the 5 slicer presets (22). All Fill Off. The manifest
  in `tools/build_presets.py` lists the four scenes; `FactoryPresetTests` checks that every drum scene is a
  library pattern, that every library pattern is played by some preset, and that only the Strike Body presets
  play the Strike Body patterns.
- **Strike Body.** Strike Body is one drum per instance and its map sends both kick and snare to Main, so a
  snare-voice pattern mutes the kick lane and the kick-voice patterns mute the snare lane. The muted lane still
  holds the anchor the lint asks for. Metallic Poly plays every lane, with 3-, 5- and 7-step loops on the second
  Ghost and Rim lanes.
- **Fills.** 13 files in `fills/`, each exactly `{"format": 3, "name", "category", "beats", "lanes"}`, where
  `beats` and `lanes` are a pattern's `"fill"` object. A fill loads by wrapping it as a pattern's fill and calling
  `parsePattern`, so fills carry no edits (no ratchets or flams). The fill lint also checks that nothing is drawn
  before the window. Each pattern's own fill is a copy of one library fill.

## Later (UI, after the grid spec lands)

A pattern menu in the grid header loads any library pattern into the edited scene (one undo step), grouped by
family. The fill view gets a fills menu that loads a library fill into the pattern's fill.
