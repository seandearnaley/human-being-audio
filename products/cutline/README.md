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

## Pads (interim)

Four programs on the PLAY surface. They engage on the next 1/16 and release at once.

| Pad | X | Y |
|-----|---|---|
| Repeat | window: 1, 1/2, 1/4 or 1/8 beat | gate length |
| Roll | snare roll: 1/8, 1/16, 1/16T, 1/32 | velocity |
| Rate | 0.5x, 1x or 2x | under 30% kick and snare; over 70% no kick |
| Drop | left half silence, right half the fill | none |

Pad MIDI (C3 to D#3) and XY to CC are off by default.

## MIDI Slicer

Plays the notes you hold through a literal gate strip of up to 32 steps: Stab Chop, Strum / Flam, Break Arp, Bass / Stab, or Clip Slice for a captured clip.

## Factory content

11 patterns in `patterns/` and 16 presets in `presets/`, built by `tools/build_presets.py`.

## Files

Patterns and presets are JSON with `"format": 3`. Cutline 2 presets are rejected, and a Cutline 2 session opens on the default beat.

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
