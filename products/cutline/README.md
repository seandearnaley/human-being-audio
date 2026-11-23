# Cutline

Expressive phrase-grammar, gravitational microtiming, and MPE/CC MIDI articulator by [Human Being Audio](https://humanbeingaudio.com).

Cutline compiles deterministic 2-, 4-, and 8-bar musical phrases (`Statement → Answer → Mutation → Ratchet → Turnaround`), bends them through a strictly monotonic continuous microtiming gravity field, wraps them in four tempo-locked Bézier expression lanes, and delivers them live over VST3 / AU (`aumi` MIDI FX) or via one-drag 960-PPQ `.mid` capture.

## Highlights

- **Five phrase grammars**: `Neuro` (syncopated call/answer + pitch-dive turnarounds), `BBCut11` (odd-unit repeats + stutter rolls), `WarpCut` (accelerating ratchets), `Halftime` (heavy bar-3 pocket), and `Liquid` (legato overlap phrases), driven from either a deterministic seed or live held MIDI notes.
- **Gravitational microtiming field**: continuous, strictly monotonic push/pull toward `Downbeat`, `Backbeat`, `Offbeat 16`, or `Triplet` pockets — mathematically guaranteed never to invert adjacent ratchets.
- **Three first-class output modes**: `Mono Ch1` (Ableton Live track-to-track safe), `MPE Zone` (Lower Zone Ch 1 Master + Ch 2–15 Member with MCM RPN & ±48 st per-note pitch bends), and `Drum Kit` (Kick/Snare/Ghost/Hat/Ratchet articulation).
- **Four shared expression lanes**: built on `hba::dsp::Curve` with custom breakpoint drawing, segment tension, and lane-follow ratios.
- **Zero-friction audition & export**: optional built-in lightweight audition synth on the VST3/Standalone stereo bus plus one-drag `.mid` export straight onto any DAW MIDI track.

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
