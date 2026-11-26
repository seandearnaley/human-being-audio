// SPDX-License-Identifier: AGPL-3.0-or-later
// Headless CLI renderer for Lineage presets (supports Gen-2 auto-printing & WAV export for listening checks).
#include "ln/engine.h"
#include "ln/preset.h"

#include <hba/core/wav.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: ln-render <out.wav> [--preset <file.json>] [--note <midi>] [--bpm <bpm>] [--bars <n>]\n");
        return 2;
    }
    const std::string outPath = argv[1];
    std::string presetPath;
    int midiNote = 29; // F1
    double bpm = 172.0;
    int bars = 2;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--preset" && i + 1 < argc)
            presetPath = argv[++i];
        else if (arg == "--note" && i + 1 < argc)
            midiNote = std::atoi(argv[++i]);
        else if (arg == "--bpm" && i + 1 < argc)
            bpm = std::atof(argv[++i]);
        else if (arg == "--bars" && i + 1 < argc)
            bars = std::atoi(argv[++i]);
    }

    ln::Preset preset;
    if (!presetPath.empty()) {
        std::ifstream in(presetPath);
        std::ostringstream buf;
        buf << in.rdbuf();
        std::string err;
        const auto parsed = ln::parsePreset(buf.str(), &err);
        if (!parsed) {
            std::fprintf(stderr, "preset error: %s\n", err.c_str());
            return 1;
        }
        preset = *parsed;
    }

    ln::PrintBuffer printed;
    const ln::PrintBuffer* printPtr = nullptr;
    if (preset.autoPrint || preset.params[ln::ParamId::genMix] > 0.01f) {
        printed = ln::Engine::renderPrint(preset.params, preset.curves, 48000.0, bpm, 29);
        printPtr = &printed;
    }

    std::vector<float> left, right;
    ln::Engine::renderOffline(preset.params, preset.curves, 48000.0, bpm, midiNote, bars, printPtr, left, right);
    std::vector<float> interleaved(left.size() * 2u);
    for (std::size_t i = 0; i < left.size(); ++i) {
        interleaved[i * 2u] = left[i];
        interleaved[i * 2u + 1u] = right[i];
    }
    if (!hba::core::writeWavFloat(outPath, interleaved, 2, 48000)) {
        std::fprintf(stderr, "failed to write %s\n", outPath.c_str());
        return 1;
    }
    std::printf("Rendered %s (%zu frames, gen=%d)\n", outPath.c_str(), left.size(), printPtr ? printed.generation : 1);
    return 0;
}
