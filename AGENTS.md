# Tract Form agent guide

A monophonic neuro bass synth: VST3, AU and Standalone for macOS. `CLAUDE.md` imports this file.
The design contract is `docs/superpowers/specs/2026-11-15-tract-form-design.md`; change it in the
same commit as the code it describes.

## Layout

| Directory | Target | Depends on | Purpose |
|---|---|---|---|
| `core/` | `tf_core` | nlohmann/json | Parameter table, `Params`, lane shapes, vowels, preset JSON |
| `engine/` | `tf_engine` | `tf_core`, HIIR | All DSP. No JUCE, threads or I/O |
| `ui/` | `tf_ui` | `tf_core`, Visage | The interface, behind `tf/ui/editor.h` and `tf/ui/model.h` |
| `plugin/` | `TractForm` | all of the above, JUCE | Formats, host parameters, MIDI, state, presets, analysis |
| `tools/` | `tf-render` | `tf_engine` | Preset to WAV, with measurements |
| `presets/` | | | Factory presets, one flat JSON file each |

`ui` and `engine` never include each other or JUCE.

## Build and test

Homebrew's tools are not on every shell's `PATH`:

```sh
export PATH=/opt/homebrew/bin:$PATH
cmake --preset engine && cmake --build --preset engine && ctest --preset engine   # core + engine, seconds
cmake --preset dev && cmake --build --preset dev && ctest --preset dev             # everything
```

Plugins land in `build/dev/plugin/TractForm_artefacts/RelWithDebInfo/{VST3,AU,Standalone}`.
Builds do not install themselves. To put a build in `~/Library/Audio/Plug-Ins`, run
`tools/scripts/install.sh`, which copies, re-signs and tells you how to validate. Only install a
build whose tests and pluginval pass: someone may be listening to what is in that folder.

## Checking work without ears or eyes

- **Sound**: `build/engine/tools/tf-render --preset presets/<name>.json --out x.wav --metrics`
  prints peak, RMS, sub stability, stereo correlation and spectral movement as JSON.
- **Interface**: `build/dev/ui/tf-ui-snapshot out.png 2560 1440` renders the whole editor
  headlessly against `FakeModel`. Read the PNG; crop with `sips -c <h> <w> --cropOffset <y> <x>`.
- **Host conformance**: `/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10
  --validate-in-process --validate "<path>.vst3"`.

## Adding a parameter

Add it to the table in `core/include/tf/params.h` (the compile-time checks will tell you if the
order or ranges are wrong), then bring the presets up to date, since they write every key out:

```sh
build/engine/tools/tf-render --dump-defaults > defaults.json
tools/scripts/fill_preset_keys.py defaults.json presets/*.json
```

## Rules

- A parameter is defined once, in `core/include/tf/params.h`. The engine, the host parameters and
  the interface all derive from that table, which is checked at compile time.
- The audio path does not allocate, lock or throw. `tf_engine` builds with
  `-Werror=function-effects` and `Engine::process` is `[[clang::nonblocking]]`.
- Interface code uses only the tokens in `ui/src/theme.h` and the controls in `ui/src/controls.h`.
  Cyan is the master sine and the sub, amber is the growl path, violet is movement.
- No taglines, legends or helper text in the interface. If a screen needs them, fix the layout.
- Warnings are errors for our targets.
- Visage: `frame.onResize() = f` replaces its built-in handler. Override `resized()` or use `+=`.
- Verify by measurement: a test, a `tf-render` metric or a snapshot. Say what was not verified.
