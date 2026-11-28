// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline studio showcase renderer for Torus Glide presets -> 48 kHz float WAV.
#include "tg/engine.h"
#include "tg/export.h"
#include "tg/preset.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: tg-render <preset.json> <out.wav> [seconds=3.0] [bpm=172] [note=41]\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open preset: " << argv[1] << "\n";
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto preset = tg::parsePresetJson(ss.str());
    if (!preset) {
        std::cerr << "invalid preset json\n";
        return 1;
    }

    const double seconds = argc > 3 ? std::atof(argv[3]) : 3.0;
    const double bpm = argc > 4 ? std::atof(argv[4]) : 172.0;
    const int note = argc > 5 ? std::atoi(argv[5]) : 41;
    constexpr double sr = 48000.0;
    constexpr int kBlock = 64;

    const int totalFrames = std::max(kBlock, static_cast<int>(std::llround(seconds * sr)));
    const int warmupFrames = static_cast<int>(std::llround(0.35 * sr));

    tg::Engine engine;
    engine.prepare(sr, kBlock, 8);
    engine.setLaneCurves(preset->curves);
    engine.setSceneMatrix(preset->matrix);
    engine.noteOn(note, 0.95f);

    std::vector<float> bufL(kBlock), bufR(kBlock);
    const double beatsPerSec = bpm / 60.0;
    int done = 0;
    while (done < warmupFrames) {
        const int n = std::min(kBlock, warmupFrames - done);
        tg::Transport t{bpm, -0.35 * beatsPerSec + (static_cast<double>(done) / sr) * beatsPerSec, true};
        engine.process(preset->params, t, bufL.data(), bufR.data(), n, nullptr);
        done += n;
    }

    std::vector<float> stereo(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    done = 0;
    float peak = 1e-6f;
    while (done < totalFrames) {
        const int n = std::min(kBlock, totalFrames - done);
        tg::Transport t{bpm, (static_cast<double>(done) / sr) * beatsPerSec, true};
        engine.process(preset->params, t, bufL.data(), bufR.data(), n, nullptr);
        for (int i = 0; i < n; ++i) {
            const float l = bufL[static_cast<std::size_t>(i)];
            const float r = bufR[static_cast<std::size_t>(i)];
            stereo[static_cast<std::size_t>(done + i) * 2u] = l;
            stereo[static_cast<std::size_t>(done + i) * 2u + 1u] = r;
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
        }
        done += n;
    }

    // Peak-calibrate to -3.0 dBFS with clean 15 ms tail fade-out (matching factory/showcase/README.md)
    const float gain = 0.70794578f / peak;
    const int fadeFrames = static_cast<int>(std::llround(0.015 * sr));
    for (int i = 0; i < totalFrames; ++i) {
        float env = 1.0f;
        if (i >= totalFrames - fadeFrames)
            env = static_cast<float>(totalFrames - 1 - i) / static_cast<float>(fadeFrames);
        stereo[static_cast<std::size_t>(i) * 2u] *= gain * env;
        stereo[static_cast<std::size_t>(i) * 2u + 1u] *= gain * env;
    }

    if (!tg::writeWavFloat(argv[2], stereo, static_cast<int>(sr))) {
        std::cerr << "failed to write wav\n";
        return 1;
    }
    std::cout << "Rendered " << seconds << "s -> " << argv[2] << "\n";
    return 0;
}
