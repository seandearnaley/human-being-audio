# Tract Form fact sheet

| | |
|---|---|
| **Product** | Tract Form |
| **Maker** | Human Being Audio |
| **What it is** | A monophonic bass synthesizer for neurofunk and drum & bass |
| **Status** | 2.0 |
| **Formats** | VST3, Audio Unit, Standalone |
| **Platform** | macOS 12 or later (universal), Windows x64, Ubuntu 26.04 x86_64 |
| **Licence** | Open source, AGPL-3.0-or-later |
| **Source** | <https://github.com/seandearnaley/human-being-audio/tree/main/products/tract-form> |

## The idea

One master sine drives everything. It phase-modulates two reese voices, and it
also becomes a clean, mono sub that bypasses all processing. The growl has its
own ceiling under the sub, so however hard it is driven, the sub does not move.

## What is in it

- **Master sine**: sets the pitch, modulates both voices and becomes the sub.
- **Two reese voices**: each plays Sine, Saw, Pulse or resonant phase
  distortion, stacked up to seven deep, with detune, ratio, phase and phase
  modulation, and a feedback loop timed in cycles of the note.
- **Beat lock**: the two voices beat exactly once per note division, so a reese
  rolls in time with the track.
- **Tract**: a five-formant filter that morphs between two of six vowels.
- **Fold**: three wave shapers with drive and asymmetry, anti-aliased.
- **Filter**: Clean, Ladder, MS-20, Low-pass or Notch, with resonance and
  drive, before or after the fold. Its cutoff, Reach, is a harmonic number, so
  it follows the pitch.
- **Weight**: how far the spectrum leans to the bottom.
- **Sculpt**: up to six notches or peaks placed on harmonics of the note.
- **Fit**: rotates the sub against the growl so the two waveforms interleave.
- **Mid/side**: everything below the crossover is mono.
- **Movement**: four tempo-locked lanes. Each can target one of thirteen parameters,
  play a curve you draw, or follow another lane at a ratio of its rate.
- **Eight macros**: Coupling, Beat, Reach, Tract, Warp, Sculpt, Motion, Width.
  They are the plugin's first eight parameters, so a controller maps them first.
- **Resample**: renders four bars offline at 16× oversampling to a WAV file.
- **54 factory presets** in six categories: Reese, Neuro, Talk, Sub, Motion,
  Pluck & Stab.

## Specifications

| | |
|---|---|
| Voices | 1 (monophonic, last-note priority, glide) |
| Oversampling | 2×, 4× or 8× live; 16× for Resample |
| Latency | 106 samples at 48 kHz, reported to the host |
| Parameters | 75, of which 54 can be automated |
| Sub stability | The sub band varies by under 0.15 dB while all four lanes modulate their default targets at full depth |
| Processor use | About 6% of one core at 4× on an Apple M4 Max |
| Interface | One resizable screen, drawn entirely as vectors |

Figures are measurements from the project's own test suite at the 0.2 beta.

## Built with

JUCE 9, Visage, HIIR, Catch2 and nlohmann/json. The interface is set in Inter
(SIL Open Font License).
