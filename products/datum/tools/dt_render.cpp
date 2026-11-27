// SPDX-License-Identifier: AGPL-3.0-or-later
// Headless CLI renderer for Datum presets (one-shot sample-0 kicks OR full context loops).
#include "dt/engine.h"
#include "dt/preset.h"

#include <hba/core/wav.h>
#include <hba/dsp/limiter.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: dt-render <out.wav> [--preset <file.json>] [--bpm <bpm>] [--slot <0..3>] "
                             "[--loop] [--bars <N>] [--ref-bass <0..4>]\n");
        return 2;
    }
    const std::string outPath = argv[1];
    std::string presetPath;
    double bpm = 172.0;
    int slot = 0;
    bool loopMode = false;
    int bars = 0;
    int refBass = -1;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--preset" && i + 1 < argc)
            presetPath = argv[++i];
        else if (arg == "--bpm" && i + 1 < argc)
            bpm = std::atof(argv[++i]);
        else if (arg == "--slot" && i + 1 < argc)
            slot = std::atoi(argv[++i]);
        else if (arg == "--loop")
            loopMode = true;
        else if (arg == "--bars" && i + 1 < argc)
            bars = std::atoi(argv[++i]);
        else if (arg == "--ref-bass" && i + 1 < argc)
            refBass = std::atoi(argv[++i]);
    }

    dt::Preset preset;
    if (!presetPath.empty()) {
        std::ifstream in(presetPath);
        std::ostringstream buf;
        buf << in.rdbuf();
        std::string err;
        const auto parsed = dt::parsePreset(buf.str(), &err);
        if (!parsed) {
            std::fprintf(stderr, "preset error: %s\n", err.c_str());
            return 1;
        }
        preset = *parsed;
    }

    if (refBass >= 0)
        preset.params.set(dt::ParamId::refBassMode, static_cast<float>(refBass));

    std::vector<float> left, right;
    if (loopMode && bars > 0) {
        // Render N continuous bars with Context Sequencer + optional Ref Sub-Bass
        preset.params.set(dt::ParamId::seqRun, 1.0f);
        preset.params.set(dt::ParamId::seqSteps, 1.0f); // 32-step (2-bar) phrasing
        const double sr = 48000.0;
        const int totalFrames = static_cast<int>(std::ceil((60.0 / bpm) * 4.0 * static_cast<double>(bars) * sr));
        left.assign(static_cast<std::size_t>(totalFrames), 0.0f);
        right.assign(static_cast<std::size_t>(totalFrames), 0.0f);
        dt::Engine eng;
        eng.prepare(sr, 512, 8);
        eng.setCurves(preset.pitchCurve, preset.ampCurve);
        eng.setPattern(preset.pattern);
        const double beatsPerSample = (bpm / 60.0) / sr;
        int pos = 0;
        while (pos < totalFrames) {
            const int n = std::min(512, totalFrames - pos);
            dt::Transport tr{bpm, static_cast<double>(pos) * beatsPerSample, true};
            eng.process(preset.params, tr, left.data() + pos, right.data() + pos, n);
            pos += n;
        }
    } else {
        dt::Engine::renderOffline(preset.params, preset.pitchCurve, preset.ampCurve, preset.pattern, nullptr, 48000.0,
                                  bpm, loopMode, slot, left, right);
    }
    std::vector<float> interleaved(left.size() * 2u);
    for (std::size_t i = 0; i < left.size(); ++i) {
        interleaved[i * 2u] = left[i];
        interleaved[i * 2u + 1u] = right[i];
    }
    if (!hba::core::writeWavFloat(outPath, interleaved, 2, 48000)) {
        std::fprintf(stderr, "failed to write %s\n", outPath.c_str());
        return 1;
    }
    const double peak = hba::dsp::stereoTruePeak(interleaved);
    std::printf("Rendered %s (%zu frames, %.1f ms, %.2f dBTP)\n", outPath.c_str(), left.size(),
                static_cast<double>(left.size()) / 48.0, 20.0 * std::log10(peak));
    return 0;
}
