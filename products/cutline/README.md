# Cutline

Cutline is a drum-pattern MIDI plugin whose grid is exactly what plays, plus a MIDI slicer for your own chords and clips. By [Human Being Audio](https://humanbeingaudio.com).

## The grid is the truth

Literal steps on 8 lanes, in three layers: Core, Groove and Detail. Nothing in playback is random. One function, `resolveBar`, decides what sounds, and both the engine and the grid call it.

## Controls

- **Density**: 0% Core, 50% Core and Groove, 100% everything. Default 50%.
- **Swing**: delays the odd 16ths. Default 0%.
- **Dynamics**: velocity contrast. Default 50%.
- **Fill**: Off, or every 2, 4, 8 or 16 bars. Default Off.
- **Scene**: A to D, four patterns.
- **Rate**: 0.5x, 1x or 2x.

## Fills

Drawn, and off by default. A lane with nothing drawn in the fill keeps playing the main pattern.

## PLAY

Six programs on the PLAY surface, locked to the grid. They engage on the next 1/16, release at once, and return to the pattern in phase.

| Pad | X | Y |
|-----|---|---|
| Repeat | window: 1, 1/2, 1/4 or 1/8 beat | gate length |
| Roll | 1/8, 1/16, 1/16T, 1/32; velocity rises from 72 to 120 across each bar | lane: kick, snare, closed hat (bottom to top); the other lanes keep playing |
| Density | Density, while held | Dynamics, while held |
| Rate | 0.5x, 1x or 2x | under 30% kick and snare; over 70% no kick |
| Drop | four zones: silence; kick only; kick and snare; everything except kick | none |
| Fill | none: while held, every bar plays its fill | none |

The surface draws the notes Cutline actually sent, one row per lane, fading over a bar, over faint outlines of the pattern. In MIDI Slicer mode it draws one row per pitch; Density and Fill do nothing there, and Drop only silences.

Pad MIDI (C3 to F3, in drum mode) and XY to CC are off by default. Bypassing Cutline sends a note-off for every note it has sounding.

## MIDI Slicer

Plays the notes you hold through a literal gate strip of up to 32 steps: Stab Chop, Strum / Flam, Break Arp, Bass / Stab, or Clip Slice for a captured clip.

## Factory content

The library contains 46 patterns, 13 fills and 22 factory presets. The preset builder in `tools/build_presets.py` maps patterns to scenes.

## Files

Patterns and presets are JSON with `"format": 3`. Cutline 2 presets are not loaded: they do not appear in the preset list, and no message says why. A Cutline 2 session opens on the default beat.

## For developers

- `engine` preset: core, engine and their tests, no JUCE or Visage. `dev` preset: everything.
- `cutline-render` renders a preset to MIDI or audio. `cl-ui-snapshot` renders the editor to a PNG.
- Spec: `docs/specs/`. Plans: `docs/plans/`.

## Building

```sh
cmake --workflow --preset check
```

Or engine-only without JUCE/Visage:

```sh
cmake --preset engine && cmake --build --preset engine && ctest --preset engine
```

## License

`AGPL-3.0-or-later`. See [LICENSE](LICENSE).
