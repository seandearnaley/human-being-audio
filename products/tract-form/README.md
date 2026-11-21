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

VST3 and standalone on macOS, Windows x64 and Ubuntu 26.04 x86_64. Audio Unit on macOS. Beta 0.2.

## What it does

- **Master sine** sets the pitch, phase-modulates both reese voices and becomes the sub.
- **Reese A and B** are sines with detune, ratio, phase offset and a tempo-synced
  feedback loop into their phase.
- **Tract** morphs a five-formant filter between two vowels.
- **Fold** is an anti-aliased wavefolder with drive and asymmetry.
- **Mid/side** keeps everything below the crossover mono.
- **Movement** is four tempo-locked lanes for phase, tract, fold and detune.
- **Resample** renders four bars offline at 16× and hands you the file to drag
  into your session.

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
