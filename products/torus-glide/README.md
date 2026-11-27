# Torus Glide 2.0

**Playable continuous pitch-manifold synthesizer (`VST3` / `AU` / `Standalone`)** by Human Being Audio.

Torus Glide 2.0 unifies bar-locked Shepard-Risset bass & chord spirals, programmable non-pop polyphonic chord bending, and hybrid Möbius endless chord glides inside a zero-allocation, real-time-safe C++20 + Visage GPU instrument featuring a tactile **2D X/Y Pitch-Manifold Performance Surface**.

## Two Surfaces: `PLAY` & `STUDIO`

- **`PLAY` (Default Performance Surface):**
  - Six jewel-coded 60 fps generative X/Y manifold worlds — **Spiral** (3D torus helix: velocity/freeze/reverse × bell & air), **Möbius** (twisted infinity strip: torus flow × gravity & halo), **Copedent** (5-string pedal-steel tension harp: chord morph × stagger & air), **Abyss** (gravitational sub dive × drive & bite), **Risset** (chronometer pulse gate × filter focus), and **Void** (interference halo × detune & width).
  - Dragging anywhere on the pad or latching **DRONE** (`Space`) plays the internal engine immediately in-key without clicks while seamlessly modulating held MIDI notes.
  - Compact live **Manifold Telemetry Ribbon** above and essential tonal macros below (**Mode**, **Timbre**, **Loop**, **Stack/Voicing**, **Filter**, **Drive**, **Sub**, **Halo**).
- **`STUDIO` (1-Click Deep Sound-Design Workstation):**
  - Full draggable Spectral Bell / 4×5 Copedent chord matrix (`Cap A–D` live chord capture, `Auto-Lead` Tymoczko voice-leading, `Reset`), 3-column synthesis rack, and 2 editable Bézier modulation lanes.

## Upgraded Internal Synthesis Engine (2.0)
- **Click-Free Sample-Rate Cycle Math:** Smooth $C^1$ zero-at-boundary Sub-Anchor wrap envelopes and sample-accurate continuous cycle/pitch ramping eliminate wrap thumps and control-rate stair-stepping across Spiral, Copedent, and Möbius.
- **7 Anti-Aliased Timbres:** Nyquist-aware `Reese Saw`, `Pure Sine`, warm fundamental-anchored `Formant Choir`, bandwidth-limited `Wavefold FM`, `Pulse Sub`, plus new `Glass Bell` and `Super Organ` (Shepard-Risset drawbar tonewheel).
- **Upper-Band Hadamard FDN Manifold Halo + Dimension Space:** Dual-stage stereo ensemble + 4-tap orthogonal Hadamard diffusion reverb strictly above `Sub X-Over`, preserving >98% mono Sub-Anchor phase coherence.
- **Studio Output & Export:** LR4 mono sub crossover, HIIR oversampled ADAA2 (`Tube`, `Buchla Fold`, `Tri Fold`, `Sine Fold`, `Clip`), 4-mode dual-stage Cytomic TPT SVF (`LP 12`, `LP 24`, `Bandpass`, `Neuro Notch`), 4× true-peak look-ahead limiter, live MPE Lower Zone MIDI out (±12/24/48 st), exact bar-locked 8× `Drag .wav`, and 960-PPQ MPE `Drag .mid`. The MIDI file uses Standard MIDI File Type 1. The first track, `MPE setup`, carries the tempo and configures the lower zone. Each of the five named voice tracks carries its note, bend range and pitch bends on its own member channel. Import the tracks together to keep their timing aligned.

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
