# Torus Glide

**Continuous pitch-manifold synthesizer (`VST3` / `AU` / `Standalone`)** by Human Being Audio.

Torus Glide unifies bar-locked Shepard-Risset bass & chord spirals, programmable non-pop polyphonic chord bending, and hybrid Möbius endless chord glides inside one zero-allocation, real-time-safe C++20 + Visage GPU instrument.

## Three Modes, One Mathematical Core

1. **Spiral (`Sidewinder` Bar-Locked Shepard-Risset):**
   - 7-octave (or Fifth / Tritone / Minor 3rd / Bohlen-Pierce) unwrapped phase-coherent partials synced to host PPQ (`1/2` to `16 bars`), with constant-RMS Hann² spectral bell normalization, MIDI-octave bell tracking, optional chordal spiral stacks, and a dedicated mono **Sub-Anchor Floor (`30–90 Hz`)** with harmonic warmth.
   - Optional **Risset Eternal Accelerando** pulse gate coupled to the bar spiral.
2. **Copedent (Programmable Polyphonic Chord Bender):**
   - 4 programmable scenes (`A–D`) × 5 voices with curated non-pop dark voicings, **Tymoczko minimal-distance voice-leading**, **Pedal-Steel per-voice stagger**, **Voice-1 sub drone lock**, and continuous **Scale / Just-Intonation / Hybrid Gravity Wells** so wide bends stay musically in tune.
   - Hold any chord on your MIDI controller and click **`Cap A–D`** to capture it straight into a scene, scrub **`Morph`** directly on the ribbon canvas, or switch scenes with low-octave **`C0–D#0`** MIDI keyswitches.
3. **Möbius (Hybrid Shepard-Chord Torus):**
   - Polyphonic voice-leading bends across chords while wrapping seamlessly up or down an infinite 4-octave pitch torus.

## Shared Studio DSP & Export
- 5 timbres (`Reese Saw`, `Pure Sine`, `Formant Choir`, `Wavefold FM`, `Pulse Sub`)
- LR4 mono sub crossover + HIIR oversampled ADAA2 (`Tube`, `Buchla Fold`, `Tri Fold`, `Sine Fold`, `Clip`)
- Dual-stage Cytomic TPT SVF (`LP 12`, `LP 24`, `Bandpass`, `Neuro Notch`)
- Zero-allocation upper-band Stereo Ensemble (`Space`) strictly above `Sub X-Over`
- 4× true-peak look-ahead limiter, live MPE Lower Zone MIDI out (±12/24/48 st), exact bar-locked 8× `Drag .wav` bounce, and 960-PPQ MPE `Drag .mid` export.

## Build & Test

```bash
cd products/torus-glide
# Fast engine-only loop:
cmake --preset engine -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build --preset engine
ctest --preset engine

# Full plugin (VST3, AU, Standalone):
cmake --preset dev -DCPM_SOURCE_CACHE="$HOME/.cache/CPM" -DTG_INSTALL_PLUGIN=ON
cmake --build --preset dev
ctest --preset dev
```
