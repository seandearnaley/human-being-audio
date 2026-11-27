// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline CLI renderer for Prism Vein: runs the built-in Neuro Reese + Breakbeat
// exciter (or custom harmonic sweep) through any Prism Vein engine/chord/preset
// and exports stereo 32-bit IEEE float WAV for ear checks and DSP regression tests.
#include "pv/engine.h"
#include "pv/preset.h"

#include <hba/core/wav.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string outPath = "prism-vein-out.wav";
    std::string presetPath;
    int engineIdx = 0;
    std::uint16_t chordMask = pv::kDefaultChordMask;
    double seconds = 3.0;
    double sr = 48000.0;
    float colorAmt = -1.0f;
    float tameAmt = -1.0f;
    float decayAmt = -1.0f;
    bool dryOnly = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-o" || arg == "--out") && i + 1 < argc)
            outPath = argv[++i];
        else if (arg == "--preset" && i + 1 < argc)
            presetPath = argv[++i];
        else if (arg == "--engine" && i + 1 < argc)
            engineIdx = std::stoi(argv[++i]);
        else if (arg == "--chord" && i + 1 < argc)
            chordMask = static_cast<std::uint16_t>(std::stoi(argv[++i], nullptr, 0));
        else if (arg == "--seconds" && i + 1 < argc)
            seconds = std::stod(argv[++i]);
        else if (arg == "--color" && i + 1 < argc)
            colorAmt = std::stof(argv[++i]);
        else if (arg == "--tame" && i + 1 < argc)
            tameAmt = std::stof(argv[++i]);
        else if (arg == "--decay" && i + 1 < argc)
            decayAmt = std::stof(argv[++i]);
        else if (arg == "--dry")
            dryOnly = true;
    }

    pv::Preset preset;
    if (!presetPath.empty()) {
        std::ifstream f(presetPath);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string err;
        if (auto parsed = pv::parsePreset(ss.str(), &err)) {
            preset = *parsed;
        } else {
            std::fprintf(stderr, "Failed to parse preset %s: %s\n", presetPath.c_str(), err.c_str());
            return 1;
        }
    } else {
        preset.params.set(pv::ParamId::engineMode, static_cast<float>(engineIdx));
        preset.chordMask = chordMask;
    }

    if (colorAmt >= 0.0f)
        preset.params.set(pv::ParamId::color, colorAmt);
    if (tameAmt >= 0.0f)
        preset.params.set(pv::ParamId::tame, tameAmt);
    if (decayAmt >= 0.0f)
        preset.params.set(pv::ParamId::decay, decayAmt);
    if (dryOnly)
        preset.params.set(pv::ParamId::color, 0.0f);

    preset.params.set(pv::ParamId::demoAudition, 1.0f);

    constexpr int kBlock = 256;
    pv::Engine engine;
    engine.prepare(sr, kBlock);
    engine.setParams(preset.params);
    engine.setChordMask(preset.chordMask);
    engine.setLaneCurves(preset.curves);

    const int totalFrames = static_cast<int>(std::lround(seconds * sr));
    std::vector<float> zero(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> outL(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> interleaved;
    interleaved.reserve(static_cast<std::size_t>(totalFrames) * 2u);

    pv::Transport tr;
    tr.bpm = 172.0;
    tr.playing = true;

    int done = 0;
    while (done < totalFrames) {
        const int n = std::min(kBlock, totalFrames - done);
        tr.ppq = (static_cast<double>(done) / sr) * (tr.bpm / 60.0);
        engine.setTransport(tr);
        engine.process(zero.data(), zero.data(), nullptr, nullptr, outL.data(), outR.data(), n);
        for (int s = 0; s < n; ++s) {
            interleaved.push_back(outL[static_cast<std::size_t>(s)]);
            interleaved.push_back(outR[static_cast<std::size_t>(s)]);
        }
        done += n;
    }

    if (!hba::core::writeWavFloat(outPath, interleaved, 2, static_cast<int>(sr))) {
        std::fprintf(stderr, "Failed to write %s\n", outPath.c_str());
        return 1;
    }
    std::printf("Wrote %s (%d frames @ %.0f Hz, peak=%.3f/%.3f, GR=%.2f dB, tame=%.1f dB)\n", outPath.c_str(),
                totalFrames, sr, engine.telemetry().peakL, engine.telemetry().peakR, engine.telemetry().grDb,
                engine.telemetry().tamePeakDb);
    return 0;
}
