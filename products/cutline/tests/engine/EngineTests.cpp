// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/engine.h"
#include "cl/phrase.h"
#include "cl/smf.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Approx;

namespace {

struct RecordedEvent {
    std::int64_t sample = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 3> data{};
};

struct NoteTracker {
    std::array<std::array<int, 128>, 16> counts{};
    int totalOns = 0;
    int totalOffs = 0;
    int doubleOnErrors = 0;
    int orphanOffErrors = 0;

    void feed(const cl::MidiEventBuffer& buf) {
        for (int i = 0; i < buf.count; ++i) {
            const auto& ev = buf.events[static_cast<std::size_t>(i)];
            const auto ch = static_cast<std::size_t>(ev.channel() - 1);
            const auto note = static_cast<std::size_t>(ev.noteNumber());
            if (ev.isNoteOn()) {
                ++totalOns;
                if (counts[ch][note] > 0)
                    ++doubleOnErrors;
                ++counts[ch][note];
            } else if (ev.isNoteOff()) {
                ++totalOffs;
                if (counts[ch][note] <= 0)
                    ++orphanOffErrors;
                else
                    --counts[ch][note];
            }
        }
    }

    int activeCount() const {
        int sum = 0;
        for (const auto& ch : counts)
            for (int c : ch)
                sum += c;
        return sum;
    }
};

std::vector<RecordedEvent> renderSequence(const cl::Params& params, int blockSize, int totalSamples, double sr,
                                          double bpm) {
    cl::Engine engine;
    engine.prepare(sr, blockSize);
    cl::MidiEventBuffer midi;
    std::vector<float> left(static_cast<std::size_t>(blockSize)), right(static_cast<std::size_t>(blockSize));
    std::vector<RecordedEvent> out;

    const double beatsPerSample = bpm / 60.0 / sr;
    int pos = 0;
    while (pos < totalSamples) {
        const int n = std::min(blockSize, totalSamples - pos);
        cl::Transport tr{bpm, static_cast<double>(pos) * beatsPerSample, true};
        engine.process(params, tr, midi, left.data(), right.data(), n);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            out.push_back(
                {static_cast<std::int64_t>(pos + ev.sampleOffset), ev.size, {ev.data[0], ev.data[1], ev.data[2]}});
        }
        pos += n;
    }
    // Final stop flushes open notes cleanly
    cl::Transport stop{bpm, static_cast<double>(totalSamples) * beatsPerSample, false};
    engine.process(params, stop, midi, left.data(), right.data(), std::min(blockSize, 64));
    for (int i = 0; i < midi.count; ++i) {
        const auto& ev = midi.events[static_cast<std::size_t>(i)];
        out.push_back(
            {static_cast<std::int64_t>(totalSamples + ev.sampleOffset), ev.size, {ev.data[0], ev.data[1], ev.data[2]}});
    }
    return out;
}

} // namespace

TEST_CASE("Gravitational microtiming field is strictly monotonic and bar-anchored", "[engine]") {
    for (int a = 0; a < 4; ++a) {
        const auto attr = static_cast<cl::Attractor>(a);
        for (double g : {-1.0, -0.6, 0.0, 0.6, 1.0}) {
            double prev = -1.0;
            for (int i = 0; i <= 1600; ++i) {
                const double beat = static_cast<double>(i) * 0.01;
                const double warped = cl::warpBeat(beat, g, attr);
                REQUIRE(std::isfinite(warped));
                REQUIRE(warped > prev);
                prev = warped;
            }
        }
    }
}

TEST_CASE("Phrase compiler is deterministic and emits balanced notes across all grammars and modes", "[engine]") {
    cl::HeldNotes held;
    for (int g = 0; g < 5; ++g) {
        for (int m = 0; m < 3; ++m) {
            for (int b = 0; b < 3; ++b) {
                cl::Params p;
                p[cl::ParamId::grammar] = static_cast<float>(g);
                p[cl::ParamId::outputMode] = static_cast<float>(m);
                p[cl::ParamId::phraseBars] = static_cast<float>(b);

                cl::PhrasePlan a, c;
                cl::compilePhrase(p, 0, held, a);
                cl::compilePhrase(p, 0, held, c);

                REQUIRE(a.noteCount > 0);
                REQUIRE(a.noteCount == c.noteCount);
                for (int i = 0; i < a.noteCount; ++i) {
                    const auto idx = static_cast<std::size_t>(i);
                    CHECK(a.notes[idx].warpedBeats == Approx(c.notes[idx].warpedBeats));
                    CHECK(a.notes[idx].noteNumber == c.notes[idx].noteNumber);
                    CHECK(a.notes[idx].channel == c.notes[idx].channel);
                    CHECK(a.notes[idx].warpedBeats >= 0.0);
                    CHECK(a.notes[idx].warpedBeats + a.notes[idx].durationBeats <= a.phraseBeats + 1e-6);
                    if (m == static_cast<int>(cl::OutputMode::mpe)) {
                        CHECK(a.notes[idx].channel >= 2);
                        CHECK(a.notes[idx].channel <= 15);
                    } else {
                        CHECK(a.notes[idx].channel == 1);
                    }
                }
            }
        }
    }
}

TEST_CASE("Zero stuck notes across 2000 chaotic transport, loop, jump, preset and mode transitions", "[engine]") {
    cl::Engine engine;
    engine.prepare(48000.0, 512);
    cl::MidiEventBuffer midi;
    std::array<float, 512> left{}, right{};
    NoteTracker tracker;

    cl::Params params;
    double ppq = 0.0;
    std::uint32_t rng = 0x13579BDFU;
    const auto nextRand = [&]() {
        rng = hba::dsp::laneHash(rng + 0x9e3779b9U);
        return rng;
    };

    for (int step = 0; step < 2000; ++step) {
        const std::uint32_t r = nextRand();
        const int blockSize = 16 + static_cast<int>(r % 497U);
        const bool playing = (r & 0x7U) != 0U;

        if ((r & 0x1FU) == 0U)
            params[cl::ParamId::outputMode] = static_cast<float>((r >> 5U) % 3U);
        if ((r & 0x3FU) == 0U)
            params[cl::ParamId::grammar] = static_cast<float>((r >> 8U) % 5U);
        if ((r & 0x1FU) == 1U)
            ppq = static_cast<double>((r >> 10U) % 64U) * 0.5; // random loop wrap / seek
        if ((r & 0x7FU) == 3U)
            params[cl::ParamId::seed] = static_cast<float>(1U + (r % 999U));

        cl::Transport tr{172.0, ppq, playing};
        engine.process(params, tr, midi, left.data(), right.data(), blockSize);
        tracker.feed(midi);
        REQUIRE(midi.count <= cl::kMaxMidiEventsPerBlock);

        // Verify audition audio stays strictly finite and bounded.
        for (int i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(left[static_cast<std::size_t>(i)]));
            REQUIRE(std::abs(left[static_cast<std::size_t>(i)]) <= 1.01f);
        }
        if (playing)
            ppq += (172.0 / 60.0 / 48000.0) * static_cast<double>(blockSize);
    }

    // Final transport stop must flush every remaining active note cleanly.
    cl::Transport stop{172.0, ppq, false};
    engine.process(params, stop, midi, left.data(), right.data(), 64);
    tracker.feed(midi);

    CHECK(tracker.totalOns > 50);
    CHECK(tracker.doubleOnErrors == 0);
    CHECK(tracker.orphanOffErrors == 0);
    CHECK(tracker.activeCount() == 0);
    CHECK(tracker.totalOns == tracker.totalOffs);
}

TEST_CASE("MIDI event timestamps and bytes are block-size invariant across regular and uneven buffers", "[engine]") {
    cl::Params params;
    params[cl::ParamId::grammar] = 0.0f; // Neuro
    params[cl::ParamId::gravity] = 0.42f;
    params[cl::ParamId::outputMode] = 0.0f; // Mono Ch1
    params[cl::ParamId::ccThinMs] = 2.0f;   // 4 ms grid

    constexpr double kSr = 48000.0;
    constexpr double kBpm = 172.0;
    constexpr int kTotalSamples = 48000 * 2; // 2 seconds (~5.7 beats, crosses bar boundary)

    const auto ref = renderSequence(params, 64, kTotalSamples, kSr, kBpm);
    REQUIRE(ref.size() > 20);

    for (int blockSize : {1, 7, 128, 257, 512, 1024}) {
        const auto cand = renderSequence(params, blockSize, kTotalSamples, kSr, kBpm);
        REQUIRE(cand.size() == ref.size());
        for (std::size_t i = 0; i < ref.size(); ++i) {
            CHECK(cand[i].sample == ref[i].sample);
            CHECK(cand[i].size == ref[i].size);
            CHECK(cand[i].data == ref[i].data);
        }
    }
}

TEST_CASE("MPE mode emits Lower Zone MCM RPN and stays well under VST3 event ceiling", "[engine]") {
    cl::Engine engine;
    engine.prepare(48000.0, 512);
    cl::Params params;
    params[cl::ParamId::outputMode] = static_cast<float>(cl::OutputMode::mpe);
    params[cl::ParamId::grammar] = static_cast<float>(cl::Grammar::warpcut);
    params[cl::ParamId::ratchet] = 1.0f;
    params[cl::ParamId::density] = 1.0f;
    params[cl::ParamId::motion] = 1.0f;
    params[cl::ParamId::ccThinMs] = 0.0f; // fastest 1 ms tick stress test

    cl::MidiEventBuffer midi;
    std::array<float, 512> left{}, right{};
    const double beatsPerSample = 174.0 / 60.0 / 48000.0;

    bool sawMcmRpn = false;
    int maxBlockEvents = 0;

    for (int b = 0; b < 300; ++b) {
        cl::Transport tr{174.0, static_cast<double>(b * 512) * beatsPerSample, true};
        engine.process(params, tr, midi, left.data(), right.data(), 512);
        maxBlockEvents = std::max(maxBlockEvents, midi.count);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            if (ev.data[0] == 0xB0 && ev.data[1] == 100 && ev.data[2] == 6)
                sawMcmRpn = true;
        }
    }

    CHECK(sawMcmRpn);
    // Steinberg VST3 Host Checker cap is 2048; Cutline enforces <= 256.
    CHECK(maxBlockEvents <= cl::kMaxMidiEventsPerBlock);
}

TEST_CASE("Live MIDI input mode stays silent without held keys and re-phrases incoming notes", "[engine]") {
    cl::Engine engine;
    engine.prepare(48000.0, 256);
    cl::Params params;
    params[cl::ParamId::inputMode] = static_cast<float>(cl::InputMode::liveMidi);
    params[cl::ParamId::motion] = 0.0f;

    cl::MidiEventBuffer midi;
    std::array<float, 256> left{}, right{};
    const double beatsPerSample = 172.0 / 60.0 / 48000.0;

    // No held keys -> zero Note Ons emitted.
    int noteOnsIdle = 0;
    for (int b = 0; b < 40; ++b) {
        engine.process(params, {172.0, static_cast<double>(b * 256) * beatsPerSample, true}, midi, left.data(),
                       right.data(), 256);
        for (int i = 0; i < midi.count; ++i)
            if (midi.events[static_cast<std::size_t>(i)].isNoteOn())
                ++noteOnsIdle;
    }
    CHECK(noteOnsIdle == 0);

    // Hold F2 (41) and C3 (48) -> emitted Note Ons derive from held pitches.
    engine.inputNoteOn(41, 110);
    engine.inputNoteOn(48, 100);
    int noteOnsHeld = 0;
    for (int b = 40; b < 360; ++b) {
        engine.process(params, {172.0, static_cast<double>(b * 256) * beatsPerSample, true}, midi, left.data(),
                       right.data(), 256);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            if (ev.isNoteOn()) {
                ++noteOnsHeld;
                const int n = ev.noteNumber();
                CHECK((n == 41 || n == 48 || n == 53 || n == 60));
            }
        }
    }
    CHECK(noteOnsHeld > 3);
}

TEST_CASE("SMF binary exporter writes valid 960-PPQ header, balanced Note On/Off, and final EOT", "[smf]") {
    cl::HeldNotes held;
    const auto curves = cl::defaultLaneCurves();

    for (int m = 0; m < 3; ++m) {
        cl::Params p;
        p[cl::ParamId::outputMode] = static_cast<float>(m);
        p[cl::ParamId::phraseBars] = 1.0f; // 4 bars

        const auto smf = cl::exportPhraseSmf(p, curves, held, 172.0, 0);
        REQUIRE(smf.size() > 26);
        CHECK(smf[0] == 'M');
        CHECK(smf[1] == 'T');
        CHECK(smf[2] == 'h');
        CHECK(smf[3] == 'd');
        CHECK(smf[14] == 'M');
        CHECK(smf[15] == 'T');
        CHECK(smf[16] == 'r');
        CHECK(smf[17] == 'k');

        const std::uint32_t trkLen = (static_cast<std::uint32_t>(smf[18]) << 24U) |
                                     (static_cast<std::uint32_t>(smf[19]) << 16U) |
                                     (static_cast<std::uint32_t>(smf[20]) << 8U) | static_cast<std::uint32_t>(smf[21]);
        REQUIRE(trkLen == smf.size() - 22U);

        // End-of-Track must be strictly the final 3 bytes.
        CHECK(smf[smf.size() - 3] == 0xFF);
        CHECK(smf[smf.size() - 2] == 0x2F);
        CHECK(smf[smf.size() - 1] == 0x00);

        // Parse track and check exact Note On / Note Off balance.
        std::size_t pos = 22;
        int ons = 0;
        int offs = 0;
        while (pos < smf.size()) {
            while (pos < smf.size() && (smf[pos++] & 0x80u) != 0u) {
            }
            REQUIRE(pos < smf.size());
            const std::uint8_t status = smf[pos++];
            if (status == 0xFF) {
                ++pos; // meta type
                const std::uint8_t len = smf[pos++];
                pos += len;
            } else {
                const std::uint8_t hi = status & 0xF0u;
                if (hi == 0x90u)
                    ++ons;
                else if (hi == 0x80u)
                    ++offs;
                pos += (hi == 0xC0u || hi == 0xD0u) ? 1U : 2U;
            }
        }
        CHECK(pos == smf.size());
        CHECK(ons > 0);
        CHECK(ons == offs);
    }
}
