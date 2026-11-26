// SPDX-License-Identifier: AGPL-3.0-or-later
// Headless Cutline 2.0 renderer: exports a preset's 960-PPQ .mid groove/slice and/or
// stereo 48 kHz float32 audition audio (supports optional --chord "48,55,58,62" for MIDI Slicer).
#include "cl/engine.h"
#include "cl/preset.h"
#include "cl/smf.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string presetPath;
    std::string midiOut;
    std::string pcmOut;
    std::string chordArg;
    double bpm = 172.0;
    double seconds = 5.6;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--preset") == 0 && i + 1 < argc)
            presetPath = argv[++i];
        else if (std::strcmp(argv[i], "--mid") == 0 && i + 1 < argc)
            midiOut = argv[++i];
        else if (std::strcmp(argv[i], "--pcm") == 0 && i + 1 < argc)
            pcmOut = argv[++i];
        else if (std::strcmp(argv[i], "--chord") == 0 && i + 1 < argc)
            chordArg = argv[++i];
        else if (std::strcmp(argv[i], "--bpm") == 0 && i + 1 < argc)
            bpm = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = std::atof(argv[++i]);
    }

    cl::Preset preset;
    if (!presetPath.empty()) {
        std::ifstream in(presetPath);
        if (!in) {
            std::fprintf(stderr, "Failed to open preset: %s\n", presetPath.c_str());
            return 1;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string err;
        auto parsed = cl::parsePreset(ss.str(), &err);
        if (!parsed) {
            std::fprintf(stderr, "Preset parse error: %s\n", err.c_str());
            return 1;
        }
        preset = std::move(*parsed);
    }

    cl::HeldNotes held;
    if (!chordArg.empty()) {
        std::stringstream ss(chordArg);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty())
                held.push(std::atoi(item.c_str()), 108);
        }
    }

    if (!midiOut.empty()) {
        const auto bytes =
            cl::exportPhraseSmf(preset.params, preset.curves, held, bpm, preset.params.choice(cl::ParamId::scene),
                                preset.bank);
        std::ofstream out(midiOut, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            std::fprintf(stderr, "Failed to write MIDI file: %s\n", midiOut.c_str());
            return 1;
        }
        std::printf("Wrote %zu bytes to %s\n", bytes.size(), midiOut.c_str());
    }

    if (!pcmOut.empty()) {
        constexpr double kSr = 48000.0;
        constexpr int kBlock = 64;
        const int totalFrames = static_cast<int>(kSr * seconds);

        cl::Engine engine;
        engine.prepare(kSr, kBlock);
        engine.setLaneCurves(preset.curves);
        engine.setPatternBank(preset.bank);
        for (int i = 0; i < held.count; ++i)
            engine.inputNoteOn(held.notes[static_cast<std::size_t>(i)], held.velocities[static_cast<std::size_t>(i)]);

        cl::Params params = preset.params;
        params[cl::ParamId::audition] = 1.0f;

        std::vector<float> left(kBlock), right(kBlock);
        std::vector<float> interleaved;
        interleaved.reserve(static_cast<std::size_t>(totalFrames) * 2U);

        cl::MidiEventBuffer midi;
        const double beatsPerSample = bpm / 60.0 / kSr;
        double ppq = 0.0;
        int done = 0;
        while (done < totalFrames) {
            const int n = std::min(kBlock, totalFrames - done);
            cl::Transport tr{bpm, ppq, true};
            engine.process(params, tr, midi, left.data(), right.data(), n);
            for (int i = 0; i < n; ++i) {
                interleaved.push_back(left[static_cast<std::size_t>(i)]);
                interleaved.push_back(right[static_cast<std::size_t>(i)]);
            }
            ppq += beatsPerSample * static_cast<double>(n);
            done += n;
        }

        std::ofstream out(pcmOut, std::ios::binary);
        out.write(reinterpret_cast<const char*>(interleaved.data()),
                  static_cast<std::streamsize>(interleaved.size() * sizeof(float)));
        if (!out) {
            std::fprintf(stderr, "Failed to write PCM file: %s\n", pcmOut.c_str());
            return 1;
        }
        std::printf("Wrote %d stereo frames to %s\n", totalFrames, pcmOut.c_str());
    }

    return 0;
}
