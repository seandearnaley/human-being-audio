<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="brand/logo/tract-form-logotype-on-dark.svg">
    <img src="brand/logo/tract-form-logotype-on-light.svg" alt="Tract Form" width="420">
  </picture>
</p>

A monophonic bass synthesizer for neurofunk drum & bass, by Human Being Audio. [Downloads and installation instructions](https://humanbeingaudio.com/#tract-form).

![Tract Form's interface](press/screenshots/tract-form-interface.png)

One master sine drives everything. It phase-modulates two detuned reese voices,
and it also becomes a clean, mono, phase-locked sub that bypasses all processing.
The growl can be as violent as you like and the sub never moves.

VST3 and standalone on macOS, Windows x64 and Ubuntu 26.04 x86_64. Audio Unit on macOS.

On Ubuntu, the editor uses Vulkan through the desktop graphics driver. The studio’s Ubuntu test environment uses Mesa’s Vulkan driver.

## What it does

- **Master sine** sets the pitch, phase-modulates both reese voices and becomes the sub.
- **Reese A and B** each play Sine, Saw, Pulse or resonant phase distortion (PD), stacked up to seven deep, with detune, ratio, phase offset and a feedback loop into their phase timed in cycles of the note.
- **Tract** morphs a five-formant filter between two vowels (switchable Pre or Post fold).
- **Filter** follows the note: Clean, Ladder, MS-20, Low-pass or Notch, with resonance and drive, before or after the fold.
- **Fold & 4× ADAA1 Growl Stage** pairs an oversampled ADAA2 wavefolder with a 4× HIIR ADAA1 growl clipper for clean, alias-free rolling harmonics.
- **Mid/side** keeps everything below the crossover mono.
- **Movement & MIDI expression** pairs four Bézier curve lanes (`Sync`, `Retrig`, `1-Shot`, plus BPM-independent `Free` and note-retriggered `Free R` Hz LFOs with phase-locked `Follow` + `÷4..×4` ratios) with smooth `CC1`, `CC74`, aftertouch, `CC121`, and RPN `00 00` pitch-bend modulation, visualized with live modulation ghost arcs on macro knobs and bars.
- **Resample & A/B workflow** renders four bars offline at 16× and hands you the WAV to drag into your session, alongside 64-step Undo/Redo (`⌘Z` / `⌘⇧Z`) and A/B Compare.

## Install and release

The [studio downloads page](https://humanbeingaudio.com/#tract-form) lists the Mac PKG, Windows x64 installer and Ubuntu 26.04 x86_64 archive. See the [studio release guide](../../factory/release/README.md) for packaging, signing and verification.

## Building

Start with the [studio build instructions](../../README.md#first-build) for the native toolchain on macOS, Windows or Ubuntu. From this product directory, configure, build and test with one preset.

```sh
cmake --workflow --preset check
```

For an incremental build, use `cmake --build --preset dev` and `ctest --preset dev`. For work on the sound alone, the `engine` preset builds and tests the DSP without JUCE or Visage. On macOS, `tools/scripts/install.sh` explicitly replaces the user-local VST3 and Audio Unit with the development build. For a distributable installer, follow the [studio release guide](../../factory/release/README.md).

Logos, the app icon and usage rules are in [brand/](brand); screenshots, a fact
sheet and ready-to-use copy are in [press/](press).

The design is in [docs/superpowers/specs](docs/superpowers/specs/2026-11-15-tract-form-design.md);
[AGENTS.md](AGENTS.md) describes the layout and how to check work by measurement.

## Licence

AGPL-3.0-or-later. See [LICENSE](LICENSE).
