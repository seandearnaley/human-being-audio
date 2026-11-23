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

TEST_CASE("Live MIDI Chord Carve and Transpose+Latch modes re-phrase and transpose cleanly", "[engine]") {
    cl::Engine engine;
    engine.prepare(48000.0, 256);
    cl::Params params;
    params[cl::ParamId::inputMode] = static_cast<float>(cl::InputMode::chordCarve);
    params[cl::ParamId::motion] = 0.0f;

    cl::MidiEventBuffer midi;
    std::array<float, 256> left{}, right{};
    const double beatsPerSample = 172.0 / 60.0 / 48000.0;

    // Chord Carve with no held keys & Latch off -> zero Note Ons emitted.
    int noteOnsIdle = 0;
    for (int b = 0; b < 40; ++b) {
        engine.process(params, {172.0, static_cast<double>(b * 256) * beatsPerSample, true}, midi, left.data(),
                       right.data(), 256);
        for (int i = 0; i < midi.count; ++i)
            if (midi.events[static_cast<std::size_t>(i)].isNoteOn())
                ++noteOnsIdle;
    }
    CHECK(noteOnsIdle == 0);

    // Hold F2 (41) and C3 (48) -> emitted Note Ons derive strictly from held pitches (and octaves).
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
                CHECK((n == 41 || n == 48 || n == 53 || n == 60 || n == 65 || n == 72));
            }
        }
    }
    CHECK(noteOnsHeld > 3);

    // Transpose + Latch: press G2 (43), release key -> latched root stays on G2!
    params[cl::ParamId::inputMode] = static_cast<float>(cl::InputMode::transpose);
    params[cl::ParamId::latch] = 1.0f;
    engine.inputAllNotesOff();
    engine.inputNoteOn(43, 100);
    engine.inputNoteOff(43);
    engine.process(params, {172.0, 0.0, true}, midi, left.data(), right.data(), 256);
    REQUIRE(engine.currentPlan().noteCount > 0);
    CHECK(engine.currentPlan().notes[0].noteNumber == 43);
}

TEST_CASE("Core motif locks in Loop mode, evolves in Evolve mode, and respects Span Locks & Note Overrides",
          "[engine]") {
    cl::HeldNotes held;
    cl::Params p;
    p[cl::ParamId::seed] = 407.0f;
    p[cl::ParamId::mutate] = 0.85f;
    p[cl::ParamId::phraseMode] = static_cast<float>(cl::PhraseMode::loop);

    cl::PhrasePlan p0, p1;
    cl::compilePhrase(p, 0, held, p0);
    cl::compilePhrase(p, 1, held, p1);
    REQUIRE(p0.noteCount == p1.noteCount);
    for (int i = 0; i < p0.noteCount; ++i)
        CHECK(p0.notes[static_cast<std::size_t>(i)].noteNumber == p1.notes[static_cast<std::size_t>(i)].noteNumber);

    // Evolve mode keeps beat 1 root anchor while allowing variation on phrase 1.
    p[cl::ParamId::phraseMode] = static_cast<float>(cl::PhraseMode::evolve);
    cl::PhrasePlan e0, e1;
    cl::compilePhrase(p, 0, held, e0);
    cl::compilePhrase(p, 1, held, e1);
    CHECK(e0.notes[0].noteNumber == e1.notes[0].noteNumber);

    // Span 0 locked at seed 407 stays identical even when rolling seed to 812.
    cl::PhraseOverrides ov;
    ov.spans[0].locked = true;
    ov.spans[0].lockedSeed = 407;
    ov.spans[3].roleOverride = static_cast<std::int8_t>(cl::Role::rest); // Silence Ratchet span
    if (auto* n0 = ov.findOrCreateNote(0))
        n0->muted = true;

    p[cl::ParamId::phraseMode] = static_cast<float>(cl::PhraseMode::loop);
    p[cl::ParamId::seed] = 812.0f;
    cl::PhrasePlan lockedPlan;
    cl::compilePhrase(p, 0, held, lockedPlan, ov);
    REQUIRE(lockedPlan.noteCount > 0);
    CHECK(lockedPlan.spans[0].locked);
    CHECK(lockedPlan.notes[0].muted);
    CHECK(lockedPlan.notes[0].noteNumber == p0.notes[0].noteNumber);
    for (int i = 0; i < lockedPlan.noteCount; ++i)
        CHECK(lockedPlan.notes[static_cast<std::size_t>(i)].role != cl::Role::ratchet);

    // Scale-diatonic MPE voicing: every MPE note stays strictly inside active scale.
    for (int sc = 0; sc < 5; ++sc) {
        cl::Params mp;
        mp[cl::ParamId::outputMode] = static_cast<float>(cl::OutputMode::mpe);
        mp[cl::ParamId::rootNote] = 2.0f; // D
        mp[cl::ParamId::scale] = static_cast<float>(sc);
        cl::PhrasePlan mpePlan;
        cl::compilePhrase(mp, 0, held, mpePlan);
        REQUIRE(mpePlan.noteCount > 4);
        for (int i = 0; i < mpePlan.noteCount; ++i)
            CHECK(cl::isNoteInScale(mpePlan.notes[static_cast<std::size_t>(i)].noteNumber, 2,
                                    static_cast<cl::Scale>(sc)));
    }
}

TEST_CASE("4-voice poly audition across all tones stays finite and reports live MIDI activity", "[engine]") {
    for (int tone = 0; tone < 4; ++tone) {
        cl::Engine eng;
        eng.prepare(48000.0, 256);
        cl::Params p;
        p[cl::ParamId::outputMode] = static_cast<float>(cl::OutputMode::mpe);
        p[cl::ParamId::audition] = 1.0f;
        p[cl::ParamId::auditionTone] = static_cast<float>(tone);

        cl::MidiEventBuffer midi;
        std::array<float, 256> left{}, right{};
        const double beatsPerBlock = (172.0 / 60.0 / 48000.0) * 256.0;
        std::uint8_t seenAct = 0;
        float maxSample = 0.0f;
        for (int b = 0; b < 80; ++b) {
            eng.process(p, {172.0, static_cast<double>(b) * beatsPerBlock, true}, midi, left.data(), right.data(), 256);
            seenAct |= eng.lastActivityMask();
            for (int i = 0; i < 256; ++i) {
                REQUIRE(std::isfinite(left[static_cast<std::size_t>(i)]));
                REQUIRE(std::isfinite(right[static_cast<std::size_t>(i)]));
                maxSample = std::max(maxSample, std::abs(left[static_cast<std::size_t>(i)]));
            }
        }
        CHECK((seenAct & cl::kActNote) != 0u);
        CHECK((seenAct & cl::kActCc74) != 0u);
        CHECK(maxSample <= 1.01f);
    }
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

TEST_CASE(
    "Phase 1 Cutline regressions: positive shift/pre-roll, Kit tone in Mono/MPE, MPE ordering, HeldNotes, Sustain, SMF",
    "[engine][smf]") {
    // 1. Positive shiftMs and DAW count-in (ppq < 0) trigger beat-0 note exactly once (no machine-gun retrigger).
    {
        cl::Engine eng;
        eng.prepare(48000.0, 256);
        cl::Params p;
        p[cl::ParamId::shiftMs] = 20.0f;
        p[cl::ParamId::motion] = 0.0f;
        cl::MidiEventBuffer midi;
        std::array<float, 256> l{}, r{};
        const double beatsPerBlock = (172.0 / 60.0 / 48000.0) * 256.0;
        int noteOnsInPreRoll = 0;
        int totalNoteOnsFirstBeat = 0;
        for (int b = -8; b < 32; ++b) {
            const double ppq = static_cast<double>(b) * beatsPerBlock;
            eng.process(p, {172.0, ppq, true}, midi, l.data(), r.data(), 256);
            for (int i = 0; i < midi.count; ++i) {
                if (midi.events[static_cast<std::size_t>(i)].isNoteOn()) {
                    if (ppq + beatsPerBlock < 0.0)
                        ++noteOnsInPreRoll;
                    ++totalNoteOnsFirstBeat;
                }
            }
        }
        CHECK(noteOnsInPreRoll == 0);
        CHECK(totalNoteOnsFirstBeat >= 1);
        CHECK(totalNoteOnsFirstBeat <= 4);
    }

    // 2. Tone: Kit produces finite non-zero audio even in Mono and MPE modes.
    for (auto mode : {cl::OutputMode::mono, cl::OutputMode::mpe}) {
        cl::Engine eng;
        eng.prepare(48000.0, 256);
        cl::Params p;
        p[cl::ParamId::outputMode] = static_cast<float>(mode);
        p[cl::ParamId::audition] = 1.0f;
        p[cl::ParamId::auditionTone] = static_cast<float>(cl::AuditionTone::kit);
        cl::MidiEventBuffer midi;
        std::array<float, 256> l{}, r{};
        const double beatsPerBlock = (172.0 / 60.0 / 48000.0) * 256.0;
        float peak = 0.0f;
        for (int b = 0; b < 80; ++b) {
            eng.process(p, {172.0, static_cast<double>(b) * beatsPerBlock, true}, midi, l.data(), r.data(), 256);
            for (int i = 0; i < 256; ++i) {
                REQUIRE(std::isfinite(l[static_cast<std::size_t>(i)]));
                peak = std::max(peak, std::abs(l[static_cast<std::size_t>(i)]));
            }
        }
        CHECK(peak > 0.02f);
    }

    // 3. MPE emits initial PB/CC74/AT before Note On at the same sample offset & never collides channels.
    {
        cl::Engine eng;
        eng.prepare(48000.0, 256);
        cl::Params p;
        p[cl::ParamId::outputMode] = static_cast<float>(cl::OutputMode::mpe);
        p[cl::ParamId::density] = 0.95f;
        p[cl::ParamId::legato] = 0.95f;
        cl::MidiEventBuffer midi;
        std::array<float, 256> l{}, r{};
        const double beatsPerBlock = (172.0 / 60.0 / 48000.0) * 256.0;
        std::array<int, 16> activeOnCh{};
        activeOnCh.fill(-1);
        bool sawPreNoteExpr = false;
        bool sawCollision = false;
        for (int b = 0; b < 120; ++b) {
            eng.process(p, {172.0, static_cast<double>(b) * beatsPerBlock, true}, midi, l.data(), r.data(), 256);
            for (int i = 0; i < midi.count; ++i) {
                const auto& ev = midi.events[static_cast<std::size_t>(i)];
                const int ch = ev.channel();
                if (ev.isNoteOn() && ch >= 2 && ch <= 15) {
                    if (activeOnCh[static_cast<std::size_t>(ch)] >= 0)
                        sawCollision = true;
                    activeOnCh[static_cast<std::size_t>(ch)] = ev.noteNumber();
                    if (i > 0) {
                        const auto& prev = midi.events[static_cast<std::size_t>(i - 1)];
                        const std::uint8_t hi = prev.data[0] & 0xF0u;
                        if (prev.channel() == ch && prev.sampleOffset == ev.sampleOffset &&
                            (hi == 0xE0u || hi == 0xB0u || hi == 0xD0u))
                            sawPreNoteExpr = true;
                    }
                } else if (ev.isNoteOff() && ch >= 2 && ch <= 15) {
                    if (activeOnCh[static_cast<std::size_t>(ch)] == ev.noteNumber())
                        activeOnCh[static_cast<std::size_t>(ch)] = -1;
                }
            }
        }
        CHECK(sawPreNoteExpr);
        CHECK_FALSE(sawCollision);
    }

    // 4. HeldNotes stays pitch-sorted ascending while tracking lastPressed; CC64 Sustain holds keys.
    {
        cl::HeldNotes held;
        held.push(55);
        held.push(43);
        held.push(48);
        REQUIRE(held.count == 3);
        CHECK(held.notes[0] == 43);
        CHECK(held.notes[1] == 48);
        CHECK(held.notes[2] == 55);
        CHECK(held.lastPressed == 48);

        cl::Engine eng;
        eng.prepare(48000.0, 256);
        cl::Params p;
        p[cl::ParamId::inputMode] = static_cast<float>(cl::InputMode::chordCarve);
        p[cl::ParamId::latch] = 0.0f;
        eng.inputSustain(true);
        eng.inputNoteOn(41, 100);
        eng.inputNoteOff(41);
        cl::MidiEventBuffer midi;
        std::array<float, 256> l{}, r{};
        eng.process(p, {172.0, 0.0, true}, midi, l.data(), r.data(), 256);
        CHECK(eng.currentPlan().noteCount > 0);
        eng.inputSustain(false);
        eng.process(p, {172.0, 0.25, true}, midi, l.data(), r.data(), 256);
        CHECK(eng.currentPlan().noteCount == 0);
    }

    // 5. SMF MPE routes CC74/AT to member channels (2..15), never Ch 1.
    {
        cl::HeldNotes held;
        const auto curves = cl::defaultLaneCurves();
        cl::Params p;
        p[cl::ParamId::outputMode] = static_cast<float>(cl::OutputMode::mpe);
        const auto smf = cl::exportPhraseSmf(p, curves, held, 172.0, 0);
        std::size_t pos = 22;
        bool sawMemberExpr = false;
        bool sawMasterCc74OrAt = false;
        while (pos < smf.size()) {
            while (pos < smf.size() && (smf[pos++] & 0x80u) != 0u) {
            }
            const std::uint8_t status = smf[pos++];
            if (status == 0xFF) {
                ++pos;
                const std::uint8_t len = smf[pos++];
                pos += len;
            } else {
                const std::uint8_t hi = status & 0xF0u;
                const int ch = (status & 0x0Fu) + 1;
                const std::uint8_t d1 = smf[pos++];
                if (hi != 0xC0u && hi != 0xD0u)
                    ++pos;
                if ((hi == 0xB0u && d1 == 74) || hi == 0xD0u) {
                    if (ch >= 2 && ch <= 15)
                        sawMemberExpr = true;
                    if (ch == 1)
                        sawMasterCc74OrAt = true;
                }
            }
        }
        CHECK(sawMemberExpr);
        CHECK_FALSE(sawMasterCc74OrAt);
    }
}
