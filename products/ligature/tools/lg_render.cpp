// SPDX-License-Identifier: AGPL-3.0-or-later
// lg-render: renders a Ligature song offline for spot checks.
//   lg-render <out.wav|out.mid> [song.json] [--bars N] [--launch BAR:SECTION]... [--hold BAR:BRIDGE:BARS] [--studio]
// A .wav plays the song through the engine and sketch synth at 48 kHz, starting in section A, applying launches at
// the given bars (1-based) exactly as a performer would. --studio plays it through the studio voices (Torus Glide,
// Lineage, Strike Body and Datum) instead. A .mid writes the whole song in strip order instead.
#include "lg/document.h"
#include "lg/engine.h"
#include "lg/midi_file.h"

#include <hba/core/wav.h>

#if LG_RENDER_STUDIO
#include "lg/studio_voices.h"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Cue {
    int bar = 1;
    lg::Command command;
};

// "4:1" or "9:0:2": colon-separated integers into `out`, keeping the defaults of fields that are missing or bad.
template <std::size_t N> void parseInts(std::string_view text, std::array<int, N>& out) {
    for (auto& value : out) {
        const auto colon = text.find(':');
        const auto field = text.substr(0, colon);
        std::from_chars(field.data(), field.data() + field.size(), value);
        if (colon == std::string_view::npos)
            return;
        text.remove_prefix(colon + 1);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: lg-render <out.wav|out.mid> [song.json] [--bars N] [--launch BAR:SECTION] "
                             "[--hold BAR:BRIDGE:BARS] [--studio]\n");
        return 2;
    }
    const std::string outPath = argv[1];
    lg::Song song = lg::defaultSong();
    int bars = 16;
    std::vector<Cue> cues;
    bool studio = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--studio") {
            studio = true;
        } else if (arg == "--bars" && i + 1 < argc) {
            bars = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--launch" && i + 1 < argc) {
            std::array<int, 2> v{1, 1}; // bar, section
            parseInts(argv[++i], v);
            cues.push_back({v[0], {lg::CommandKind::launch, static_cast<std::int16_t>(v[1])}});
        } else if (arg == "--hold" && i + 1 < argc) {
            std::array<int, 3> v{1, 0, 2}; // bar, bridge, bars held
            parseInts(argv[++i], v);
            cues.push_back({v[0], {lg::CommandKind::hold, static_cast<std::int16_t>(v[1])}});
            cues.push_back({v[0] + v[2], {lg::CommandKind::release, 0}});
        } else {
            std::ifstream in(arg);
            std::stringstream text;
            text << in.rdbuf();
            if (auto parsed = lg::parseSong(text.str())) {
                song = *parsed;
            } else {
                std::fprintf(stderr, "could not read song %s\n", arg.c_str());
                return 1;
            }
        }
    }

    if (outPath.size() > 4 && outPath.substr(outPath.size() - 4) == ".mid") {
        lg::Document doc(song);
        const auto bytes = lg::songMidiFile(doc, std::nullopt, song.bpm);
        std::ofstream out(outPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        std::printf("%s: %zu bytes\n", outPath.c_str(), bytes.size());
        return out ? 0 : 1;
    }

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;
    auto engine = std::make_unique<lg::Engine>();
    engine->prepare(kRate, kBlock);
    lg::compileSong(song, engine->songBuffer().write(), 1);
    engine->songBuffer().publish();

    lg::EngineParams params;
#if LG_RENDER_STUDIO
    std::unique_ptr<lg::StudioVoices> voices;
    if (studio) {
        voices = std::make_unique<lg::StudioVoices>();
        voices->prepare(kRate, kBlock);
        if (!voices->presetsLoaded()) {
            std::fprintf(stderr, "the studio voices could not load their presets\n");
            return 1;
        }
        engine->setVoiceRenderer(voices.get());
        params.studioVoices = true;
    }
#else
    if (studio) {
        std::fprintf(stderr, "this lg-render was built without the studio voices (LG_BUILD_VOICES=OFF)\n");
        return 2;
    }
#endif
    lg::MidiOutBuffer midi;
    std::vector<float> left(kBlock), right(kBlock), interleaved;
    const double bpm = song.bpm;
    const double bps = bpm / 60.0 / kRate;
    const double endBeat = bars * 4.0;
    double ppq = 0.0;
    int notes = 0;
    std::size_t nextCue = 0;
    std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.bar < b.bar; });
    const auto started = std::chrono::steady_clock::now();
    while (ppq < endBeat) {
        while (nextCue < cues.size() && (cues[nextCue].bar - 1) * 4.0 <= ppq)
            engine->command(cues[nextCue++].command);
        engine->process(params, {bpm, ppq, true}, midi, left.data(), right.data(), kBlock);
        for (int i = 0; i < midi.count; ++i)
            notes += (midi.events[static_cast<std::size_t>(i)].data[0] & 0xF0u) == 0x90u ? 1 : 0;
        for (int i = 0; i < kBlock; ++i) {
            interleaved.push_back(left[static_cast<std::size_t>(i)]);
            interleaved.push_back(right[static_cast<std::size_t>(i)]);
        }
        ppq += kBlock * bps;
    }
    const double renderSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    // Whole blocks overshoot the last bar: cut at its end so the next downbeat is not in the file.
    const auto frames = static_cast<std::size_t>(std::llround(endBeat / bps));
    interleaved.resize(std::min(interleaved.size(), frames * 2));
    if (!hba::core::writeWavFloat(outPath, interleaved, 2, static_cast<int>(kRate))) {
        std::fprintf(stderr, "could not write %s\n", outPath.c_str());
        return 1;
    }
    const double audioSeconds = static_cast<double>(interleaved.size() / 2) / kRate;
    std::printf("%s: %d bars at %.0f BPM, %d notes, %s, rendered %.2f s of audio in %.3f s (%.0fx real time)\n",
                outPath.c_str(), bars, bpm, notes, studio ? "studio voices" : "sketch synth", audioSeconds,
                renderSeconds, audioSeconds / std::max(renderSeconds, 1e-9));
    return 0;
}
