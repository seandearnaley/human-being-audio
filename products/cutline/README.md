# Cutline

Expressive phrase-grammar, gravitational microtiming, and MPE/CC MIDI articulator by [Human Being Audio](https://humanbeingaudio.com).

Cutline compiles deterministic 2-, 4-, and 8-bar musical phrases (`Statement → Answer → Mutation → Ratchet → Turnaround`), bends them through a strictly monotonic continuous microtiming gravity field, wraps them in four tempo-locked Bézier expression lanes, and delivers them live over VST3 / AU (`aumi` MIDI FX) or via one-drag 960-PPQ `.mid` capture.

## Highlights

- **Five phrase grammars & contours**: `Neuro`, `BBCut11`, `WarpCut`, `Halftime`, and `Liquid` with `Loop`/`Evolve` modes, six melodic contours (`Auto`, `Pedal`, `Arch`, `Rise`, `Fall`, `Acid`), 64-step `‹ Roll ›` seed history, and Live MIDI `Transpose` / `Chord Carve` + `Latch`.
- **Direct-manipulation Phrase Stage**: click spans to Lock/Pin across rolls, drag dividers to resize fill/turnaround windows, right-click to override roles (including `REST`), drag notes vertically snapped to scale, Alt-click to mute, inspect timing/velocity/bend in the hover micro-HUD, and monitor live `NOTE / PB / CC74 / CC1 / AT` LEDs.
- **Gravitational microtiming field**: continuous, strictly monotonic push/pull toward `Downbeat`, `Backbeat`, `Offbeat 16`, or `Triplet` pockets — mathematically guaranteed never to invert adjacent ratchets.
- **Three first-class output modes**: `Mono Ch1` (Ableton Live track-to-track safe with RPN `00 00` bend range), `MPE Zone` (Lower Zone Ch 1 Master + Ch 2–15 Member with MCM RPN & ±48 st scale-diatonic voicing), and `Drum Kit` (Kick/Snare/Ghost/Hat/Ratchet articulation).
- **Multi-hue expression lanes, poly audition & studio workflow**: four target-coded `hba::dsp::Curve` lanes, 4-voice poly audition synth (`Reese`, `Sub`, `Pluck`, `Kit`), exact current-phrase 960-PPQ `.mid` drag export, plus 64-step Undo/Redo (`⌘Z` / `⌘⇧Z`) and A/B Compare.

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
