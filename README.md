# Human Being Audio

* all plugins made with AI assistance by professional engineers with audio experience for free, made with love, care and support.

Studio source for [Livecut Redux](products/livecut-redux), the live beat slicer, [Tract Form](products/tract-form), the bass synthesizer, [Pole Field](products/pole-field), the morphing filterbank, and [Cutline](products/cutline), the expressive MIDI phrase articulator. Each product owns its sound, presets, stable plugin IDs, license and release version. The [studio website](https://humanbeingaudio.com) lives in the same checkout.

## First build

Clone the repository and choose any product (`livecut-redux`, `tract-form`, `pole-field`, or `cutline`). All dependencies are fetched and cached automatically via `cmake/CPM.cmake` (no git submodules required).

```sh
git clone https://github.com/seandearnaley/human-being-audio.git
cd human-being-audio/products/livecut-redux
# Or enter products/tract-form, products/pole-field, or products/cutline from the repo root.
cmake --workflow --preset check
```

The workflow configures, builds and tests the selected product. The first configure fetches its pinned dependencies (`JUCE 9.0.6`, `Visage v1.0.0`, `nlohmann_json 3.12.0`, `Catch2 3.16.1`). To reuse those sources between builds and products, set `CPM_SOURCE_CACHE` to a writable directory. Set `CMAKE_BUILD_PARALLEL_LEVEL` to suit the memory available on your machine. Livecut’s tests also support `ctest --preset dev --parallel 4`. For daily edits, `cmake --build --preset dev` reuses the existing build.

| Native environment | One-time setup |
| --- | --- |
| macOS | Install Xcode with its command-line tools, CMake 3.25+ and Ninja. Use Apple Clang. The development preset builds for the current CPU and the release preset builds arm64 and x86_64. |
| Windows x64 | Open the x64 Visual Studio developer shell with the Desktop development with C++ workload, Windows SDK, CMake and Ninja on PATH. Livecut uses MSVC. For Tract Form, put upstream LLVM on PATH and set PowerShell variables `$env:CC = 'clang'` and `$env:CXX = 'clang++'` before the first configure. [LLVM 23.1.6](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.6) was tested with VS 2022. Use a fresh build directory when changing compiler. |
| Ubuntu 26.04 x86_64 | At the repo root run `sudo apt-get update` and `xargs -a cmake/ubuntu-26.04-packages.txt sudo apt-get install --no-install-recommends`. Set `export CC=clang-22 CXX=clang++-22`. On a headless machine prefix the workflow or CTest command with `xvfb-run -a ../../cmake/with-window-manager.sh` from the product directory. Livecut’s editor tests exercise real window and focus behavior. |

Tract Form checks at configure time that the compiler both accepts an annotated audio function and rejects an allocation inside it. See [Clang function effect analysis](https://clang.llvm.org/docs/FunctionEffectAnalysis.html).

## Builds and tests

```sh
# Run inside either product
cmake --preset release
cmake --build --preset release
ctest --preset release

# Shorter Tract Form loop for engine work
cmake --preset engine
cmake --build --preset engine
ctest --preset engine
```

Release targets produce a VST3 bundle and standalone application on all three platforms and an Audio Unit on macOS. Find Livecut outputs in `build/release/Livecut_artefacts/Release/` and Tract Form outputs in `build/release/plugin/TractForm_artefacts/Release/`. These are developer builds. To produce signed installers and a public release, follow the [studio release guide](factory/release/README.md). The build presets leave installed plugins alone.

CTest covers both products on each OS. Tract Form also runs its Apple libmalloc instrumentation on macOS. Its [fixed-lane audio fixtures](products/tract-form/tests/engine/fixtures) preserve the pre-migration samples and original hashes. See each product's tests for host state, presets and DSP behavior.

## C++ editor and quality checks

Use LLVM 22 for clang-format, clang-tidy and clangd. Ubuntu 26.04 provides
`clang-format-22 clang-tidy-22 clangd-22 libclang-rt-22-dev`. On macOS use
`brew install llvm@22` and select the tools in `$(brew --prefix llvm@22)/bin`
in your editor. clangd skips inline header bodies and misidentifies some standard-library calls as blocking.
The editor omits that diagnostic category. The engine compiler enforces it with complete function bodies.
Keep using the native compiler from the build table above.
Enable clangd and format on save in your editor. The repository's LLVM configuration
sets the same formatting and diagnostic rules for every product.

- **Editor** — configure and build each product you work on with the existing `dev` preset.
  From the repository root run `python3 factory/quality/native.py editor` to index every registered
  product, or append the product slugs you want. Repeat after changing CMake sources or flags.
  The root database contains one command per studio source, including consumers of the shared headers.
  Vendor and generated source files are excluded from background indexing.
- **Format** — use LLVM 22 in your editor or run `git clang-format origin/main` with its tools on PATH. CI checks changed lines.
- **Lint** — inside a configured and built product run `cmake --build --preset lint`. This uses
  the actual generated headers and compiler options. Put the LLVM 22 tools on PATH for this command. CI checks modified translation units.
  A header or build configuration change checks every studio translation unit for its consumers.
- **Real-time audio** — on Ubuntu run `cmake --workflow --preset rtsan`
  inside a product. This instruments the engine and exercises its real render paths under LLVM's
  RealtimeSanitizer. The checks run alongside ordinary PR builds. Signed release presets are independent.
- **New products** — register the slug in `studio.json` and include `cmake/native-presets.json`
  from the product presets. Mark the real DSP render entry point `[[clang::nonblocking]]`.
  In the test CMakeLists, when `HBA_RTSAN` is on, call `hba_realtime_suite(target "Catch2 filter")`
  in place of ordinary Catch2 discovery for the existing engine tests. CI discovers products from the catalogue.
  The real-time preset fails when the filter selects no tests.

## Working across the studio

| When you are working on… | Start here | What belongs there |
| --- | --- | --- |
| A product name, publisher, website or plug-in identity | [Studio catalogue](studio.json) and [CMake integration](cmake/StudioProduct.cmake) | The catalogue feeds both products. Bundle and host IDs are permanent once shipped. |
| Shared About screens and installation artwork | [Studio UI](modules/studio-ui) and [design handoff](design/README.md) | Change the masthead, source link and keyboard behavior once. Each product owns the controls that make it an instrument or effect. |
| A sound or algorithm | [Research](research/README.md), [gain study](research/gain/README.md) and [shared DSP](modules/dsp) | Compare a reference render with the candidate before moving an operation into both engines. |
| A release | [Factory guide](factory/release/README.md) and [Studio release workflow](.github/workflows/release.yml) | Each product has its own version and musician-facing notes. One dispatch builds, signs, validates and publishes the three packages. |
| The catalogue | [Website source](web) and [live downloads](https://humanbeingaudio.com/#plugins) | Product descriptions and demos live with the site. Published platform links and checksums come from the release records. |

For the next product, reserve its slug and stable identifiers in the catalogue, then implement its processor and editor under `products/<slug>`. Follow an existing product's CMake integration for the shared UI and native presets. Put its own DSP regression tests and a host-state fixture beside the integration, register it in the release selector, and add catalogue copy and media when it is ready to show. The first release should exercise the same installed-package checks as the existing products.

The protected `main` branch requires an up-to-date pull request and the GitHub Actions check
`Studio PR checks`. That check waits for every job selected for the change. A documentation-only
PR runs the workflow planner and the required check. See the [measured PR timings](docs/native-pr-timing.md).

## Website development

The [live website](https://humanbeingaudio.com) is a Next.js App Router application in [web/](web). From that directory run `npm ci` and `npm run dev`. For a production check, run `npm test`, `npm run build`, `npx playwright install chromium` and `npm run test:e2e`. Vercel builds the site from the studio repository. The server reads public release manifests, verifies asset sizes and SHA-256 digests against GitHub, and revalidates the catalogue every minute. The download and source links work without JavaScript.

Both products include [the studio CMake presets](cmake/native-presets.json) and retain independent configure roots and JUCE versions. Both About screens use [the studio UI module](modules/studio-ui). [The studio catalogue](studio.json) supplies product identity to JUCE and packaging. The DSP engines keep their independent signal paths and tests. The site and the native build do not depend on each other's toolchains.

## Source history and licenses

Both original main commits are ancestors of the studio import. To follow a file across the import boundary, use its original path at the pinned commit. The 45 original public branch and tag tips are preserved as archival tags under `archive/<product>/heads/` and `archive/<product>/tags/`. The original repositories are read-only archives. Use the [studio releases](https://github.com/seandearnaley/human-being-audio/releases) for current downloads and the [studio issue tracker](https://github.com/seandearnaley/human-being-audio/issues) for both plugins.

| Product | Imported source | License |
| --- | --- | --- |
| Livecut Redux | [307e169](https://github.com/seandearnaley/livecut/commit/307e169e360019d9e1d48858fa46cfcfbda548ea) | [Livecut license](products/livecut-redux/LICENSE) and [credits](products/livecut-redux/README.md#credits) |
| Tract Form | [c79ada9](https://github.com/seandearnaley/tract-form/commit/c79ada94f102598c0e193e2111a7e82ee5eac73d) | [Tract Form license](products/tract-form/LICENSE) |
