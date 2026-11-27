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

## Later (UI, after the grid spec lands)

A pattern menu in the grid header loads any library pattern into the edited scene (one undo step), grouped by
family. The fill view gets a fills menu that loads a library fill into the pattern's fill.
