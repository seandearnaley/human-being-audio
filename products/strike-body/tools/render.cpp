// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline CLI renderer for Strike Body presets (one-shot or 4-articulation DnB test groove).
#include "sb/engine.h"
#include "sb/export.h"
#include "sb/preset.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: sb-render <preset.json> <out.wav> [--groove] [--align]\n";
        return 2;
    }
    const std::string presetPath = argv[1];
    const std::string outPath = argv[2];
    bool groove = false;
    bool runAlign = false;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--groove")
            groove = true;
        else if (arg == "--align")
            runAlign = true;
    }

    try {
        const auto parsed = sb::parsePresetJson(readFile(presetPath));
        if (!parsed) {
            std::cerr << "Failed to parse preset: " << presetPath << "\n";
            return 1;
        }
        sb::Params params = parsed->params;
        if (runAlign) {
            const auto res = sb::solveAutoAlign(params, 48000.0);
            params.set(sb::ParamId::bodyPhase, res.bodyPhaseDeg);
            params.set(sb::ParamId::shellOffsetMs, res.shellOffsetMs);
            params.set(sb::ParamId::shellInvert, res.shellInvert ? 1.0f : 0.0f);
            params.set(sb::ParamId::wireOffsetMs, res.wireOffsetMs);
            params.set(sb::ParamId::wireInvert, res.wireInvert ? 1.0f : 0.0f);
            std::cout << "Auto-Align: lock " << res.beforeLockPct << "% -> " << res.afterLockPct
                      << "% (phase=" << res.bodyPhaseDeg << " deg, shellOff=" << res.shellOffsetMs << " ms)\n";
        }

        if (!groove) {
            sb::OneShotRequest req;
            req.params = params;
            req.sampleRate = 48000.0;
            req.oversampling = 8;
            const auto audio = sb::renderOneShot(req);
            if (!sb::writeWavFloat(outPath, audio, 48000)) {
                std::cerr << "Failed writing " << outPath << "\n";
                return 1;
            }
            std::cout << "Rendered one-shot (" << (static_cast<double>(audio.size()) / 2.0 / 48000.0) << "s) -> "
                      << outPath << "\n";
            return 0;
        }

        // Render a 4-articulation musical showcase phrase (Main, Ghost, Rim, Roll at 172 BPM)
        constexpr double kSr = 48000.0;
        constexpr double kBpm = 172.0;
        constexpr int kBlock = 64;
        const double beatsPerSample = kBpm / 60.0 / kSr;
        const int totalFrames = static_cast<int>(std::llround((8.0 * 60.0 / kBpm) * kSr)); // 2 bars

        struct Hit {
            double beat;
            sb::Articulation art;
            float vel;
        };
        const std::vector<Hit> hits{
            {0.00, sb::Articulation::main, 1.00f},  {1.00, sb::Articulation::main, 0.96f},
            {1.75, sb::Articulation::ghost, 0.62f}, {2.25, sb::Articulation::ghost, 0.56f},
            {3.00, sb::Articulation::rim, 0.94f},   {3.75, sb::Articulation::ghost, 0.68f},
            {4.00, sb::Articulation::main, 1.00f},  {5.00, sb::Articulation::main, 0.98f},
            {5.75, sb::Articulation::ghost, 0.60f}, {6.50, sb::Articulation::roll, 0.78f},
            {6.75, sb::Articulation::roll, 0.85f},  {7.00, sb::Articulation::roll, 0.92f},
            {7.25, sb::Articulation::roll, 0.96f},  {7.50, sb::Articulation::main, 1.00f},
        };

        sb::Engine engine;
        engine.prepare(kSr, kBlock, 4);
        std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
        std::vector<float> bufL(kBlock), bufR(kBlock);
        std::size_t nextHit = 0;

        for (int pos = 0; pos < totalFrames; pos += kBlock) {
            const int n = std::min(kBlock, totalFrames - pos);
            const double bStart = static_cast<double>(pos) * beatsPerSample;
            const double bEnd = static_cast<double>(pos + n) * beatsPerSample;
            while (nextHit < hits.size() && hits[nextHit].beat >= bStart && hits[nextHit].beat < bEnd) {
                engine.trigger(hits[nextHit].art, hits[nextHit].vel, 1.0f);
                ++nextHit;
            }
            engine.process(params, bufL.data(), bufR.data(), n);
            for (int i = 0; i < n; ++i) {
                out[static_cast<std::size_t>(pos + i) * 2u] = bufL[static_cast<std::size_t>(i)];
                out[static_cast<std::size_t>(pos + i) * 2u + 1u] = bufR[static_cast<std::size_t>(i)];
            }
        }
        if (!sb::writeWavFloat(outPath, out, 48000)) {
            std::cerr << "Failed writing " << outPath << "\n";
            return 1;
        }
        std::cout << "Rendered 4-articulation groove (" << (totalFrames / kSr) << "s) -> " << outPath << "\n";
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
