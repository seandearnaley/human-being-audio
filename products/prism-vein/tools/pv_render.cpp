// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline CLI renderer for Prism Vein: runs the built-in Neuro Reese + Breakbeat
// exciter (or custom harmonic sweep) through any Prism Vein engine/chord/preset
// and exports stereo 32-bit IEEE float WAV for ear checks and DSP regression tests.
#include "pv/engine.h"
#include "pv/preset.h"

#include <hba/core/wav.h>

#include <array>
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
    double bpm = 172.0;
    std::string progMode;
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
        else if (arg == "--bpm" && i + 1 < argc)
            bpm = std::stod(argv[++i]);
        else if (arg == "--prog" && i + 1 < argc)
            progMode = argv[++i];
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
    std::vector<float> scL(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> scR(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> outL(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> interleaved;
    interleaved.reserve(static_cast<std::size_t>(totalFrames) * 2u);

    pv::Transport tr;
    tr.bpm = bpm;
    tr.playing = true;

    // Optional live MIDI chord progression + stereo supersaw sidechain carrier for showcase demos.
    const std::vector<std::vector<int>> futureChords = {
        {51, 55, 58, 62, 65}, // Eb maj9
        {48, 55, 58, 62, 63}, // C min9
        {44, 51, 55, 58, 60}, // Ab maj9
        {46, 53, 55, 58, 62}, // Bb 6/9
    };
    const std::vector<std::vector<int>> colorChords = {
        {53, 56, 60, 63, 67}, // F min9
        {53, 56, 60, 63, 67}, // F min9
        {49, 53, 56, 60, 63}, // Db maj9
        {51, 55, 58, 62, 65}, // Eb maj9
    };
    const std::vector<std::vector<int>> morphChords = {
        {53, 60, 63, 67, 69}, // F dorian 9
        {55, 62, 65, 69, 72}, // G min11
        {56, 63, 67, 70, 72}, // Ab maj9
        {51, 58, 62, 65, 67}, // Eb maj9
    };
    const auto* activeProg = progMode == "future"  ? &futureChords
                             : progMode == "color" ? &colorChords
                             : progMode == "morph" ? &morphChords
                                                   : nullptr;
    const bool synthSidechain = (progMode == "morph");
    int lastBar = -1;
    std::array<double, 15> scPhase{};

    int done = 0;
    while (done < totalFrames) {
        const int n = std::min(kBlock, totalFrames - done);
        tr.ppq = (static_cast<double>(done) / sr) * (tr.bpm / 60.0);
        engine.setTransport(tr);

        if (activeProg != nullptr) {
            const int bar = static_cast<int>(std::floor(tr.ppq / 4.0)) % static_cast<int>(activeProg->size());
            if (bar != lastBar) {
                engine.allNotesOff();
                for (int note : (*activeProg)[static_cast<std::size_t>(bar)])
                    engine.noteOn(note, 0.92f);
                lastBar = bar;
            }
        }

        const float* sideL = nullptr;
        const float* sideR = nullptr;
        if (synthSidechain && activeProg != nullptr) {
            const auto& notes = (*activeProg)[static_cast<std::size_t>(std::max(0, lastBar))];
            for (int s = 0; s < n; ++s) {
                const double tSec = static_cast<double>(done + s) / sr;
                const double beatPhase = std::fmod(tSec * (tr.bpm / 60.0), 1.0);
                const float pump = 0.25f + 0.75f * static_cast<float>(std::sin(beatPhase * 3.14159265358979323846));
                float sumL = 0.0f;
                float sumR = 0.0f;
                int oscIdx = 0;
                for (int note : notes) {
                    const double baseHz = 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
                    for (int det = -1; det <= 1; ++det) {
                        const double hz = baseHz * std::pow(2.0, (static_cast<double>(det) * 11.0) / 1200.0);
                        auto& ph = scPhase[static_cast<std::size_t>(oscIdx++)];
                        ph += hz / sr;
                        ph -= std::floor(ph);
                        const float saw = static_cast<float>(2.0 * ph - 1.0);
                        const float pan = 0.5f + 0.35f * static_cast<float>(det);
                        sumL += saw * (1.0f - pan);
                        sumR += saw * pan;
                    }
                }
                const float norm = (0.14f / static_cast<float>(std::max(1, oscIdx))) * pump;
                scL[static_cast<std::size_t>(s)] = sumL * norm;
                scR[static_cast<std::size_t>(s)] = sumR * norm;
            }
            sideL = scL.data();
            sideR = scR.data();
        }

        engine.process(zero.data(), zero.data(), sideL, sideR, outL.data(), outR.data(), n);
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
