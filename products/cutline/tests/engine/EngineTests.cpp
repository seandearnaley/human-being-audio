// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/engine.h"
#include "cl/phrase.h"
#include "cl/smf.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace cl;

TEST_CASE("Groove & MPC Swing warpBeat is strictly monotonic across every pocket and extreme depth", "[engine]") {
    for (int p = 0; p < 5; ++p) {
        const auto pocket = static_cast<Attractor>(p);
        for (double g : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
            double prev = -1.0;
            for (int i = 0; i <= 320; ++i) {
                const double b = static_cast<double>(i) * 0.05; // 0..16 beats in fine steps
                const double w = warpBeat(b, g, pocket);
                REQUIRE(std::isfinite(w));
                REQUIRE(w > prev);
                prev = w;
            }
        }
    }
}

TEST_CASE("All 12 multi-bar breakbeat styles compile distinct polyphonic drum patterns", "[engine]") {
    std::set<std::uint32_t> sigs;
    HeldNotes held;
    for (int s = 0; s < kDrumStyleCount; ++s) {
        Params p;
        p[ParamId::style] = static_cast<float>(s);
        p[ParamId::complexity] = 0.68f;
        p[ParamId::ghosts] = 0.65f;
        p[ParamId::fills] = 0.50f;
        PhrasePlan plan;
        compilePhrase(p, 0, 0, held, plan);
        REQUIRE(plan.noteCount > 32);
        sigs.insert(plan.signature);

        std::set<int> lanesHit;
        for (int i = 0; i < plan.noteCount; ++i) {
            const auto& n = plan.notes[static_cast<std::size_t>(i)];
            CHECK(n.warpedBeats >= 0.0);
            CHECK(n.warpedBeats < plan.phraseBeats);
            CHECK(n.velocity >= 1);
            CHECK(n.velocity <= 127);
            lanesHit.insert(n.lane);
        }
        CHECK(lanesHit.size() >= 4);
    }
    CHECK(sigs.size() == static_cast<std::size_t>(kDrumStyleCount));
}

TEST_CASE("MIDI Slicer never invents random notes and emits zero Pitch Bend by default", "[engine]") {
    Engine eng;
    eng.prepare(48000.0, 64);

    // Feed a specific Fmin9 chord: F2(41), C3(48), G#3(56), C4(60), G4(67)
    const std::set<int> allowed{41, 48, 56, 60, 67, 53, 60, 68, 72, 79}; // input notes + clean +12 octave echo
    eng.inputNoteOn(41, 108);
    eng.inputNoteOn(48, 102);
    eng.inputNoteOn(56, 98);
    eng.inputNoteOn(60, 96);
    eng.inputNoteOn(67, 94);

    for (int ss = 0; ss < 4; ++ss) {
        Params p;
        p[ParamId::engineMode] = static_cast<float>(EngineMode::slicer);
        p[ParamId::slicerStyle] = static_cast<float>(ss);
        p[ParamId::style] = static_cast<float>(DrumStyle::ukg);
        p[ParamId::slicerLatch] = 1.0f;

        std::vector<float> l(64), r(64);
        MidiEventBuffer midi;
        const double bps = 132.0 / 60.0 / 48000.0;
        double ppq = 0.0;
        int noteOns = 0;
        int pitchBends = 0;

        for (int blk = 0; blk < 3000; ++blk) {
            eng.process(p, {132.0, ppq, true}, midi, l.data(), r.data(), 64);
            for (int i = 0; i < midi.count; ++i) {
                const auto& ev = midi.events[static_cast<std::size_t>(i)];
                if (ev.isNoteOn()) {
                    ++noteOns;
                    CHECK(allowed.count(ev.noteNumber()) == 1);
                }
                if ((ev.data[0] & 0xF0u) == 0xE0u)
                    ++pitchBends;
            }
            for (int i = 0; i < 64; ++i) {
                REQUIRE(std::isfinite(l[static_cast<std::size_t>(i)]));
                REQUIRE(std::isfinite(r[static_cast<std::size_t>(i)]));
            }
            ppq += bps * 64.0;
        }
        REQUIRE(noteOns > 8);
        CHECK(pitchBends == 0);
    }
}

TEST_CASE("All 8 Playable X/Y Programs sweep continuously, emit XY->CC, and leave zero stuck notes", "[engine]") {
    Engine eng;
    eng.prepare(48000.0, 64);

    std::vector<float> l(64), r(64);
    MidiEventBuffer midi;
    const double bps = 172.0 / 60.0 / 48000.0;
    double ppq = 0.0;

    for (int prog = 0; prog < kPerfPadCount; ++prog) {
        Params p;
        p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
        p[ParamId::padProgram] = static_cast<float>(prog);
        p[ParamId::padTouch] = 1.0f;
        p[ParamId::padCcMode] = static_cast<float>(PadCcMode::cc74AndCc1);

        int padNoteOns = 0;
        int cc74Count = 0;
        int cc1Count = 0;
        int pitchBends = 0;
        for (int blk = 0; blk < 400; ++blk) {
            const float sweep = static_cast<float>(blk) / 399.0f;
            p[ParamId::padX] = 0.15f + 0.78f * sweep;
            p[ParamId::padY] = 0.85f - 0.65f * sweep;
            eng.process(p, {172.0, ppq, true}, midi, l.data(), r.data(), 64);
            for (int i = 0; i < midi.count; ++i) {
                const auto& ev = midi.events[static_cast<std::size_t>(i)];
                if (ev.isNoteOn())
                    ++padNoteOns;
                if ((ev.data[0] & 0xF0u) == 0xB0u && ev.data[1] == 74)
                    ++cc74Count;
                if ((ev.data[0] & 0xF0u) == 0xB0u && ev.data[1] == 1)
                    ++cc1Count;
                if ((ev.data[0] & 0xF0u) == 0xE0u)
                    ++pitchBends;
            }
            for (int i = 0; i < 64; ++i) {
                REQUIRE(std::isfinite(l[static_cast<std::size_t>(i)]));
                REQUIRE(std::isfinite(r[static_cast<std::size_t>(i)]));
            }
            ppq += bps * 64.0;
        }
        CHECK(padNoteOns > 0);
        CHECK(cc74Count > 0);
        CHECK(cc1Count > 0);
        CHECK(pitchBends == 0);
    }

    // Stopping transport cleanly flushes all active notes (zero stuck notes!)
    Params stopP;
    eng.process(stopP, {172.0, ppq, false}, midi, l.data(), r.data(), 64);
    CHECK(eng.activeNotesLowMask() == 0);
    CHECK(eng.activeNotesHighMask() == 0);
}

TEST_CASE("Clip Slice captures live MIDI events and replays them cleanly", "[engine]") {
    Engine eng;
    eng.prepare(48000.0, 64);

    Params p;
    p[ParamId::engineMode] = static_cast<float>(EngineMode::slicer);
    p[ParamId::slicerStyle] = static_cast<float>(SlicerStyle::clipSlice);
    p[ParamId::gravity] = 0.30f;

    eng.inputNoteOn(36, 118, 0.0);
    eng.inputNoteOff(36, 0.25);
    eng.inputNoteOn(38, 114, 1.0);
    eng.inputNoteOff(38, 1.25);
    REQUIRE(eng.capturedClipCount() == 2);

    std::vector<float> l(64), r(64);
    MidiEventBuffer midi;
    const double bps = 140.0 / 60.0 / 48000.0;
    double ppq = 4.0;
    int replayed = 0;
    for (int blk = 0; blk < 1400; ++blk) {
        eng.process(p, {140.0, ppq, true}, midi, l.data(), r.data(), 64);
        for (int i = 0; i < midi.count; ++i) {
            if (midi.events[static_cast<std::size_t>(i)].isNoteOn())
                ++replayed;
        }
        ppq += bps * 64.0;
    }
    CHECK(replayed >= 2);
}

TEST_CASE("SMF Type 0 export produces a valid 960-PPQ MIDI file and scales cleanly with 0.5x / 1x / 2x rate",
          "[engine]") {
    Params p;
    p[ParamId::style] = static_cast<float>(DrumStyle::neuro);
    HeldNotes held;

    p[ParamId::seqRate] = static_cast<float>(SeqRate::half);
    const auto halfBytes = exportPhraseSmf(p, defaultLaneCurves(), held, 172.0, 0);
    p[ParamId::seqRate] = static_cast<float>(SeqRate::normal);
    const auto normBytes = exportPhraseSmf(p, defaultLaneCurves(), held, 172.0, 0);
    p[ParamId::seqRate] = static_cast<float>(SeqRate::doubleTime);
    const auto dblBytes = exportPhraseSmf(p, defaultLaneCurves(), held, 172.0, 0);

    REQUIRE(normBytes.size() > 64);
    CHECK(normBytes[0] == 'M');
    CHECK(normBytes[1] == 'T');
    CHECK(normBytes[2] == 'h');
    CHECK(normBytes[3] == 'd');
    CHECK(halfBytes.size() < normBytes.size());
    CHECK(dblBytes.size() > normBytes.size());
}

TEST_CASE("Global SeqRate 0.5x / 1x / 2x halves and doubles live MIDI event density cleanly", "[engine]") {
    auto countNotesForRate = [](SeqRate rate) {
        Engine eng;
        eng.prepare(48000.0, 64);
        Params p;
        p[ParamId::style] = static_cast<float>(DrumStyle::amen);
        p[ParamId::seqRate] = static_cast<float>(rate);
        std::vector<float> l(64), r(64);
        MidiEventBuffer midi;
        const double bps = 140.0 / 60.0 / 48000.0;
        double ppq = 0.0;
        int notes = 0;
        for (int blk = 0; blk < 2600; ++blk) {
            eng.process(p, {140.0, ppq, true}, midi, l.data(), r.data(), 64);
            for (int i = 0; i < midi.count; ++i) {
                if (midi.events[static_cast<std::size_t>(i)].isNoteOn())
                    ++notes;
            }
            ppq += bps * 64.0;
        }
        eng.process(p, {140.0, ppq, false}, midi, l.data(), r.data(), 64);
        CHECK(eng.activeNotesLowMask() == 0);
        CHECK(eng.activeNotesHighMask() == 0);
        return notes;
    };

    const int halfNotes = countNotesForRate(SeqRate::half);
    const int normNotes = countNotesForRate(SeqRate::normal);
    const int dblNotes = countNotesForRate(SeqRate::doubleTime);
    REQUIRE(halfNotes > 10);
    CHECK(normNotes > halfNotes + 10);
    CHECK(dblNotes > normNotes + 20);
}
