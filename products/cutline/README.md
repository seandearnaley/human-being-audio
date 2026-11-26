# Cutline 2.0

Playable X/Y breakbeat, polymetric drum & MIDI articulation instrument by [Human Being Audio](https://humanbeingaudio.com).

Cutline 2.0 pairs a giant 60fps generative-art **2D X/Y Performance Stage** (`PLAY`) with an **8-Lane Polymetric Drum & Live MIDI Slicer Matrix** (`GRID`). Perform continuous breakbeat chops, escalating rolls, gravity bounces, swing pockets, live polymeters, beat stutters, half/double-time warps, and tension drops across 8 jewel-coded worlds (`C3–G3`), drive 10 built-in external drum VST kit maps (including Ableton Live Rack / GM, Strike Body, Addictive Drums 2, Toontrack SD3/EZD, BFD3, GetGood Drums, Slate SSD5, NI Studio/Abbey, XLN XO, and Chromatic), or slice your own chords, basslines, and clips with zero invented random notes.

## Highlights

- **Two-Surface `PLAY | GRID` Architecture**:
  - **`PLAY` Surface (default)**: 8 playable 2D X/Y worlds (`Slice`, `Roll`, `Bounce`, `Swing`, `Poly`, `Stutter`, `Morph`, `Drop` — selectable by mouse, automation, or MIDI `C3–G3`) with live `Hold`, quantized switching, `XY → CC` expression streaming (`CC74 + CC1`, `CC74 + Vel`, `CC1 + AT`), and an 8-lane live MIDI Pulse Ribbon.
  - **`GRID` Surface (1-click toggle)**: 8 musical macro knobs (`Style`, `Gravity`, `Ghosts`, `Chop`, `Ratchet`, `Complex`, `Fills`, `Dynamics`), 8 interactive polymetric drum lanes (`1–32` steps per lane, velocity drag, ratchets `×2/×3/×4`, grace flams, Euclidean overrides, Mute/Solo/Lock), 4 switchable Scenes (`A–D`), `Mutate` / `Polymeter` / `Clone →` actions, and 2 optional CC/Velocity expression curve lanes (zero default Pitch Bend).
- **12 Multi-Bar Breakbeat Styles**: `Amen`, `Think`, `Neuro`, `Halftime`, `UKG`, `Footwork`, `Stepper`, `Electro`, `Drill`, `Afro`, `Trap`, and `Euclidean`.
- **10 External Drum VST Kit Maps + Per-Lane Custom Notes**: `Live Rack / GM`, `Strike Body`, `Chromatic`, `Addictive Drums 2`, `Toontrack (SD3/EZD)`, `BFD3`, `GetGood Drums`, `Slate SSD5`, `NI Studio / Abbey`, and `XLN XO` — with dynamic kit-aware lane labels in `GRID` and per-lane draggable MIDI note overrides.
- **Global `0.5× / 1× / 2×` Sequence Rate**: Instant, automatable Half-Time, Normal, and Double-Time scaling across live MIDI output, X/Y articulation engines, UI playhead, and exported `.mid` clips.
- **Pitch-Transparent MIDI Slicer**: `Stab Chop`, `Strum / Flam`, `Break Arp`, `Bass / Stab`, and `Clip Slice` (1-bar live MIDI ring-buffer re-slicer) — 100% your played pitches, zero random melodies.
- **Studio Delivery**: VST3 / AU (`aumi` MIDI FX) with built-in 8-voice electronic drum kit & clean poly stab audition synth, 960-PPQ drag-and-drop `.mid` clip export, 32 curated factory presets, plus 64-step Undo/Redo and A/B Compare.

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
