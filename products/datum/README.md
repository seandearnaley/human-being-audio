# Datum

Phase-locked neurofunk kick drum synthesizer for macOS, Windows, and Linux (VST3, AU, Standalone).

## Architecture
- **Four Pure-DSP Cores (`01 CORE`):** Analytical `Sine`, discrete nonlinear `Twin-T` bridged-T analog drum resonator, `Harmonic` 6-partial additive bank with Schroeder / Peak-Min crest-factor phase solver, and `Membrane` (Berger tension-modulated stiff circular Bessel modes + Helmholtz drum cavity).
- **Analytical Spline Envelopes + Zero-X:** Non-periodic piecewise pitch and amplitude splines integrated via 4-point Gauss-Legendre quadrature for sample-exact phase continuity, plus automatic terminal `Zero-X` tail snap to eliminate loop boundary clicks.
- **Pure-DSP Impact (`02 IMPACT`):** Six modal beaters (`Felt`, `Wood`, `Plastic`, `Leather`, `Metal`, `Dirac`), transient 2-op FM, dynamic SVF noise burst, and optional drag-and-drop Click WAV with automatic phase/onset alignment.
- **Phase-Native Sculpting (`03 SHAPE`):** Zero-crossover 3-band balance (`Sub` / `Body` / `Click`), envelope-native Attack / Body / Tail shaping, 8x HIIR ADAA2 saturation (`Tube`, `Tape`, `Soft`, `Hard`, `Fold`, `Asym` with analytical DC cancellation), and zero-phase Box-Cut notch.
- **Sub-Link & Master (`04 MASTER`):** Orchard all-pass `PhaseRotator` (`0..360°`), 8-stage phase disperser, zero-latency `C²` safety ceiling, and live sidechain bass alignment (`Match Phase` + YIN `Tune Root`).
- **Context Composer (`COMPOSE`):** Four-slot Kit Matrix (`A · Main`, `B · Ghost`, `C · Double`, `D · Sub`), 16/32-step velocity/slot/ratchet sequencer, built-in Reference Sub-Bass, sample-0 key-tagged 16x WAV drag, and 960-PPQ MIDI drag.

## Building
```bash
cd products/datum
cmake --preset dev -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build --preset dev
ctest --preset dev
```

## License
AGPL-3.0-or-later.
