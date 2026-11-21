# Human Being Audio

Studio source for [Livecut Redux](products/livecut-redux), the live beat slicer, and [Tract Form](products/tract-form), the bass synthesizer. Each product owns its sound, presets, stable plugin IDs, license and release version. The [studio website](web/index.html) lives in the same checkout.

## First build

Clone with the pinned Livecut dependencies, then choose a product.

```sh
git clone --recurse-submodules https://github.com/seandearnaley/human-being-audio.git
cd human-being-audio/products/livecut-redux
# Or enter products/tract-form from the repo root.
cmake --workflow --preset check
```

The workflow configures, builds and tests the selected product. For an existing clone, use `git submodule update --init --recursive`. The first Tract Form configure fetches its pinned dependencies. To reuse those sources between builds, set `CPM_SOURCE_CACHE` to a writable directory. Set `CMAKE_BUILD_PARALLEL_LEVEL` to suit the memory available on your machine. Livecut’s tests also support `ctest --preset dev --parallel 4`. For daily edits, `cmake --build --preset dev` reuses the existing build.

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

Release targets produce a VST3 bundle and standalone application on all three platforms and an Audio Unit on macOS. Find Livecut outputs in `build/release/Livecut_artefacts/Release/` and Tract Form outputs in `build/release/plugin/TractForm_artefacts/Release/`. These are developer builds. To produce signed installers and a draft release, follow the [studio release guide](factory/release/README.md). The build presets leave installed plugins alone.

CTest covers both products on each OS. Tract Form also runs its Apple libmalloc instrumentation on macOS. Its [fixed-lane audio fixtures](products/tract-form/tests/engine/fixtures) preserve the pre-migration samples and original hashes. See each product's tests for host state, presets and DSP behavior.

## Website and shared code

Open `web/index.html` for the imported catalogue. A local web server can serve `web/` directly. Vercel can use that directory as the site root when deployment is enabled. Each product will supply its own published release record for the download catalogue.

Both products include [the studio CMake presets](cmake/native-presets.json) and retain independent configure roots and JUCE versions. Production DSP and UI modules will enter `modules/` when the first change has two verified consumers. The site and the native build do not depend on each other's toolchains.

## Source history and licenses

Both original main commits are ancestors of the studio import. To follow a file across the import boundary, use its original path at the pinned commit. The 45 original public branch and tag tips are preserved as archival tags under `archive/<product>/heads/` and `archive/<product>/tags/`. Existing releases and their download links remain at their original repositories.

| Product | Imported source | License |
| --- | --- | --- |
| Livecut Redux | [307e169](https://github.com/seandearnaley/livecut/commit/307e169e360019d9e1d48858fa46cfcfbda548ea) | [Livecut license](products/livecut-redux/LICENSE) and [credits](products/livecut-redux/README.md#credits) |
| Tract Form | [c79ada9](https://github.com/seandearnaley/tract-form/commit/c79ada94f102598c0e193e2111a7e82ee5eac73d) | [Tract Form license](products/tract-form/LICENSE) |
