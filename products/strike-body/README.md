# Strike Body

**Phase-locked neuro snare, kick & nonlinear modal percussion synthesizer** — built by **Human Being Audio** with JUCE 9 and Visage 1.0 (VST3, AU, Standalone).

## Highlights

- **Three Phase-Locked Synthesis Layers**
  1. **Fundamental Body:** Phase-locked start phase (`0°–360°`) with anti-click micro-attack, concave `Drop Curve` pitch whip, 4 body waveforms (`Pure Sine`, `Warm 909`, `Tri-Punch`, `Sub-Square`), and transient PM FM.
  2. **24-Mode Nonlinear Tensioned Modal Shell:** 6 physical eigenmode geometries (`Snare Shell`, `Kirchhoff Plate`, `Drumhead`, `Metallic Bar`, `Hollow Pipe`, `Glass Bell`), 6 mallet exciters (`Wood Stick`, `Hard Felt`, `Metal Hammer`, `Rim Flam`, `Skin Thump`, `Laser Snap`), energy-coupled tension bend, spatial `Strike Pos`, harmonic `Stretch`, `Hi Damp`, stereo `Spread`, and `±5 ms` smoothed alignment + polarity invert.
  3. **Transient-Ducked Wire & Click:** Dedicated phase-aligned crack click plus `Snare Wire` / `Crushed Metal` / `Air Sizzle` beds with automatic transient ducking, sympathetic `Wire Buzz`, stereo width, and deterministic `Humanize` (bit-exact when `0%`).
- **Master Punch Bus:** 16-stage (8 matched pairs) 100% series flat-magnitude Allpass Micro-Disperser, `Tone Scoop` (`380 Hz` carve + `6.2 kHz` shelf), zero-alloc 4-line Hadamard `Studio Room` early reflections, HIIR oversampled (`2x/4x/8x`) ADAA2 shaper (`Clip`, `Tube`, `Buchla`, `Fold`, `Sine`), LR4 `Sub Mono` crossover, 22 Hz DC blocker, and 4× look-ahead true-peak limiter.
- **Realtime Safe:** Zero-allocation, lock-free audio path compiling cleanly under Clang `-Wfunction-effects -Werror=function-effects` (`[[clang::nonblocking]]`).
- **Tactile Visual Workflow:**
  - **40 ms Latency-Compensated Transient Microscope** (drag X for `Align ms`, drag Y for `Start Phase`, live `LOCK %` readout, and one-click grid-locked `Auto-Align`).
  - **Interactive Chladni Modal Drumhead** with model-reactive plasma glow and velocity-reactive shockwaves.
  - **6 Automatable Performance Macros** (`PUNCH`, `TIGHT`, `METAL`, `SMACK`, `SCOOP`, `ROOM`), 6 one-click **Archetype Quick-Starts** (`SNARE`, `KICK`, `CLAP`, `METAL`, `TOM`, `PERC`), **`MUTATE [M]`** smart phase-locked variation, per-layer `[M] [S]` mute/solo, Undo/Redo + A/B compare, and sample-0 grid-locked key-tagged **`.wav` Bounce & Drag**.
- **24 Factory Presets** across `Snare`, `Kick`, `Clap & Slap`, `Metallic`, `Toms`, and `FX & Hats`.

## MIDI Modes

- **Kit Map (default):** `C1 (36)` = Main, `C#1 (37)` = Ghost, `D1 (38)` = Rim, `D#1 (39)` / `E1 (40)` = Roll (mirrored per octave).
- **Chromatic:** `C3 (60)` plays nominal `Body Hz`; all other keys track pitch chromatically.
- **Voice Choke:** `Split Sub` (default — chokes only sub/body on re-trigger while letting 4 modal shells ring naturally), `Poly 4x`, or `Mono Cut`.

## Build & Test

```bash
cd products/strike-body

# Fast engine-only loop (no JUCE/Visage):
cmake --preset engine -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build --preset engine
ctest --preset engine --output-on-failure

# Full plugin build (VST3, AU, Standalone, UI snapshot, CLI renderer & all tests):
cmake --preset dev -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

## License

AGPL-3.0-or-later — see [LICENSE](LICENSE).
