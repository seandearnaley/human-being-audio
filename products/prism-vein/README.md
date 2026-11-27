# Prism Vein

**Phase-locked spectral colorizer, MIDI polyphonic chord vocoder & zero-Soothe Bark auto-tame resonator** for Human Being Audio (`VST3`, `AU`, `Standalone`).

## Highlights
- **4 Spectral & Waveguide Color Engines:**
  - **Chord Lattice:** 4× overlap-add Hann² COLA Stereo STFT (`1024` frame / `256` hop) with Spectral Pitch Pulling, exact `exp(+j·π/2·k)` phase-coherent harmonic tail decay, odd/even partial stereo fanning, and octave-up shimmer.
  - **Carrier Morph:** Sample-aligned dual-stereo STFT cross-synthesis & spectral vocoder driven by an optional Sidechain Carrier bus (or internal harmonic chord carrier).
  - **Harmonic Comb:** 12-voice stereo tuned fractional-delay waveguide comb filterbank with frequency-compensated T60 decay, tilt damping, and stereo width.
  - **Glass Freeze:** Continuous spectral cloud capture sculpted live through the active Chord Lattice and MIDI input.
- **Sub-Safe LR4 Bypass:** Phase-locked Linkwitz-Riley 4th-order crossover (`50–240 Hz`) with mono collapse and exact latency alignment so sub weight stays punchy and phase-intact even at 100% Color.
- **Exact Unity Transient Shield:** Dual-time-constant RMS-power transient separator (`transient + body == input`) keeping kick, snare, and pluck attacks razor-sharp while bodies bloom into iridescent chords.
- **In-Frame Bark Auto-Tame:** Zero-extra-latency local spectral prominence attenuator targeting harsh `2–8 kHz` whistles before IFFT synthesis.
- **Zero-Latency Live Mode (`Live 0ms`):** Switchable between `STFT 1024` precision and `0 ms` waveguide comb playback in the Studio Voice Bar.
- **Built-In `DEMO` Audition:** Instant internal 172 BPM Neuro Reese + Breakbeat exciter for Standalone and quick sound design.

## Build & Test
```bash
cd products/prism-vein
cmake --preset dev -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake --build --preset dev
ctest --preset dev
```

## License
AGPL-3.0-or-later.
