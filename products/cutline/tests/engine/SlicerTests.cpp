// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/engine.h"
#include "cl/phrase.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <vector>

using namespace cl;
using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;

HeldNotes chord() {
    HeldNotes held;
    held.push(48, 100);
    held.push(55, 90);
    held.push(60, 80);
    return held;
}

Params slicerParams(SlicerStyle style) {
    Params p;
    p[ParamId::engineMode] = static_cast<float>(EngineMode::slicer);
    p[ParamId::slicerStyle] = static_cast<float>(style);
    return p;
}

std::unique_ptr<SlicerPlan> compile(const Params& p, const GrooveState& groove, const HeldNotes& held, int scene = 0,
                                    double bpm = 120.0) {
    auto plan = std::make_unique<SlicerPlan>();
    compileSlicer(p, groove, scene, held, bpm, *plan);
    return plan;
}

int notesAt(const SlicerPlan& plan, double beat) {
    int n = 0;
    for (int i = 0; i < plan.noteCount; ++i)
        n += std::abs(plan.notes[static_cast<std::size_t>(i)].beat - beat) < 1.0e-6 ? 1 : 0;
    return n;
}

// Tracks which notes are on. A note-on of a note that is already on is malformed MIDI (a retrigger must send the
// note-off first).
struct NotesOn {
    std::array<bool, 128> on{};

    void take(const MidiEventBuffer& midi) {
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            const auto note = static_cast<std::size_t>(ev.noteNumber());
            if (ev.isNoteOn()) {
                INFO("note " << note << " at offset " << ev.sampleOffset);
                REQUIRE_FALSE(on[note]);
                on[note] = true;
            } else if (ev.isNoteOff()) {
                on[note] = false;
            }
        }
    }

    bool anyOn() const {
        for (const bool b : on)
            if (b)
                return true;
        return false;
    }
};

} // namespace

TEST_CASE("Stab Chop plays every held note on every lit gate step", "[slicer]") {
    const GrooveState groove; // default gate: steps 0, 3, 6, 10, 14
    const auto plan = compile(slicerParams(SlicerStyle::stabChop), groove, chord());
    CHECK(plan->phraseBeats == Approx(4.0));
    REQUIRE(plan->noteCount == 15);
    for (const double beat : {0.0, 0.75, 1.5, 2.5, 3.5})
        CHECK(notesAt(*plan, beat) == 3);
    for (int i = 0; i < plan->noteCount; ++i) {
        const int note = plan->notes[static_cast<std::size_t>(i)].note;
        CHECK((note == 48 || note == 55 || note == 60));
    }
}

TEST_CASE("The slicer phrase is as long as the scene's pattern", "[slicer]") {
    GrooveBank bank;
    setPatternBars(bank.scenes[2], 4);
    const auto plan = compile(slicerParams(SlicerStyle::stabChop), GrooveState(bank), chord(), 2);
    CHECK(plan->phraseBeats == Approx(16.0));
    CHECK(plan->noteCount == 60); // the 16-step gate loops four times
    CHECK(notesAt(*plan, 12.75) == 3);
}

TEST_CASE("A gate step ratchets only when its ratchet says so, and Swing moves odd steps", "[slicer]") {
    GrooveBank bank;
    bank.slicerSteps[3].ratchet = 3;
    Params p = slicerParams(SlicerStyle::stabChop);
    auto plan = compile(p, GrooveState(bank), chord());
    CHECK(plan->noteCount == 21); // four plain steps and one triple step, three notes each
    CHECK(notesAt(*plan, 0.75) == 3);
    CHECK(notesAt(*plan, 0.75 + 0.25 / 3.0) == 3);
    CHECK(notesAt(*plan, 0.0) == 3);

    p[ParamId::swing] = 1.0f;
    plan = compile(p, GrooveState(GrooveBank{}), chord());
    CHECK(notesAt(*plan, 0.875) == 3); // step 3 is odd: delayed by an eighth of a beat
    CHECK(notesAt(*plan, 1.5) == 3);   // step 6 is even: unmoved
}

TEST_CASE("Flam on a gate step strums it, and Strum / Flam strums every step", "[slicer]") {
    GrooveBank bank;
    bank.slicerSteps[6].flam = true;
    Params p = slicerParams(SlicerStyle::stabChop);
    p[ParamId::strumMs] = 20.0f; // 0.04 beats at 120 BPM
    auto plan = compile(p, GrooveState(bank), chord());
    CHECK(notesAt(*plan, 0.0) == 3);
    CHECK(notesAt(*plan, 1.5) == 1);
    CHECK(notesAt(*plan, 1.54) == 1);
    CHECK(notesAt(*plan, 1.58) == 1);

    p[ParamId::slicerStyle] = static_cast<float>(SlicerStyle::strumFlam);
    plan = compile(p, GrooveState(GrooveBank{}), chord());
    CHECK(notesAt(*plan, 0.0) == 1);
    CHECK(notesAt(*plan, 0.04) == 1);
}

TEST_CASE("Break Arp walks up through the held notes, one per lit step", "[slicer]") {
    const GrooveState groove;
    const auto plan = compile(slicerParams(SlicerStyle::breakArp), groove, chord());
    REQUIRE(plan->noteCount == 5);
    const int expected[5]{48, 55, 60, 48, 55};
    for (int i = 0; i < 5; ++i)
        CHECK(plan->notes[static_cast<std::size_t>(i)].note == expected[i]);
}

TEST_CASE("Bass / Stab takes its bass rhythm from the resolved kick lane", "[slicer]") {
    GrooveBank bank; // default beat: kick on steps 0 and 8
    bank.scenes[0].lanes[0].steps[6].velocity = 96;
    bank.scenes[0].lanes[0].steps[6].layer = Layer::detail;
    const GrooveState groove(bank);
    Params p = slicerParams(SlicerStyle::bassStab);

    auto plan = compile(p, groove, chord());
    int bass = 0;
    int upper = 0;
    for (int i = 0; i < plan->noteCount; ++i) {
        const auto& n = plan->notes[static_cast<std::size_t>(i)];
        if (n.note == 48) {
            ++bass;
            CHECK((std::abs(n.beat - 0.0) < 1.0e-6 || std::abs(n.beat - 2.0) < 1.0e-6));
        } else {
            ++upper;
        }
    }
    CHECK(bass == 2);  // the Detail kick is silent at Density 50%
    CHECK(upper == 8); // gate steps 3, 6, 10, 14: two upper notes each; step 0 is left to the bass

    p[ParamId::density] = 1.0f;
    plan = compile(p, groove, chord());
    bass = 0;
    for (int i = 0; i < plan->noteCount; ++i)
        bass += plan->notes[static_cast<std::size_t>(i)].note == 48 ? 1 : 0;
    CHECK(bass == 3); // at Density 100% the Detail kick sounds, so the bass follows it
}

TEST_CASE("Clip Slice and an empty chord compile to nothing, and compiling twice gives the same phrase", "[slicer]") {
    const GrooveState groove;
    CHECK(compile(slicerParams(SlicerStyle::clipSlice), groove, chord())->noteCount == 0);
    CHECK(compile(slicerParams(SlicerStyle::stabChop), groove, HeldNotes{})->noteCount == 0);

    Params p = slicerParams(SlicerStyle::strumFlam);
    p[ParamId::swing] = 0.5f;
    const auto a = compile(p, groove, chord());
    const auto b = compile(p, groove, chord());
    REQUIRE(a->noteCount == b->noteCount);
    for (int i = 0; i < a->noteCount; ++i) {
        const auto& x = a->notes[static_cast<std::size_t>(i)];
        const auto& y = b->notes[static_cast<std::size_t>(i)];
        CHECK(x.beat == y.beat);
        CHECK(x.note == y.note);
        CHECK(x.velocity == y.velocity);
    }
}

TEST_CASE("Slicer inputs compare equal until something the phrase depends on changes", "[slicer]") {
    Params p = slicerParams(SlicerStyle::stabChop);
    const SlicerInputs a = slicerInputs(p, 0, chord(), 120.0, 7);
    CHECK_FALSE(a.sameAs(SlicerInputs{})); // nothing compiled yet
    CHECK(a.sameAs(slicerInputs(p, 0, chord(), 120.0, 7)));
    CHECK_FALSE(a.sameAs(slicerInputs(p, 1, chord(), 120.0, 7)));
    CHECK_FALSE(a.sameAs(slicerInputs(p, 0, chord(), 121.0, 7)));
    CHECK_FALSE(a.sameAs(slicerInputs(p, 0, chord(), 120.0, 8)));
    CHECK_FALSE(a.sameAs(slicerInputs(p, 0, HeldNotes{}, 120.0, 7)));
    p[ParamId::swing] = 0.2f;
    CHECK_FALSE(a.sameAs(slicerInputs(p, 0, chord(), 120.0, 7)));
}

TEST_CASE("The engine plays the slicer phrase: your notes only, and no pitch bend", "[engine][slicer]") {
    const std::set<int> allowed{41, 48, 56, 60, 67};
    for (int style = 0; style < 4; ++style) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        for (const int note : allowed)
            eng.inputNoteOn(note, 100);
        Params p = slicerParams(static_cast<SlicerStyle>(style));
        p[ParamId::slicerLatch] = 1.0f;

        std::vector<float> l(kBlock), r(kBlock);
        MidiEventBuffer midi;
        NotesOn notes;
        const double bps = 132.0 / 60.0 / kSr;
        double ppq = 0.0;
        int noteOns = 0;
        int pitchBends = 0;
        for (int blk = 0; blk < 3000; ++blk) {
            eng.process(p, {132.0, ppq, true}, midi, l.data(), r.data(), kBlock);
            notes.take(midi);
            for (int i = 0; i < midi.count; ++i) {
                const auto& ev = midi.events[static_cast<std::size_t>(i)];
                if (ev.isNoteOn()) {
                    ++noteOns;
                    CHECK(allowed.count(ev.noteNumber()) == 1);
                }
                pitchBends += (ev.data[0] & 0xF0u) == 0xE0u ? 1 : 0;
            }
            for (const float v : l)
                REQUIRE(std::isfinite(v));
            ppq += bps * static_cast<double>(kBlock);
        }
        INFO("style " << style);
        CHECK(noteOns > 8);
        CHECK(pitchBends == 0);
        CHECK(eng.slicerPlan().noteCount > 0);

        eng.process(p, {132.0, ppq, false}, midi, l.data(), r.data(), kBlock);
        notes.take(midi);
        CHECK_FALSE(notes.anyOn());
        CHECK(eng.activeNotesLowMask() == 0);
        CHECK(eng.activeNotesHighMask() == 0);
    }
}

TEST_CASE("Slicer MIDI stays well formed with large blocks", "[engine][slicer]") {
    constexpr int kBig = 2048;
    for (int style = 0; style < 4; ++style) {
        Engine eng;
        eng.prepare(kSr, kBig);
        const HeldNotes held = chord();
        for (int i = 0; i < held.count; ++i)
            eng.inputNoteOn(held.notes[static_cast<std::size_t>(i)], held.velocities[static_cast<std::size_t>(i)]);
        GrooveBank bank;
        bank.slicerSteps[3].ratchet = 4;
        bank.slicerSteps[6].flam = true;
        bank.slicerSteps[10].nudge = -24;
        eng.setGroove(GrooveState(bank));
        Params p = slicerParams(static_cast<SlicerStyle>(style));
        p[ParamId::slicerLatch] = 1.0f;
        p[ParamId::swing] = 0.6f;
        p[ParamId::gateLen] = 1.0f;

        std::vector<float> l(kBig), r(kBig);
        MidiEventBuffer midi;
        NotesOn notes;
        const double bps = 132.0 / 60.0 / kSr;
        double ppq = 0.0;
        int noteOns = 0;
        const int blocks = static_cast<int>(std::ceil(16.0 / bps / static_cast<double>(kBig))); // four bars
        for (int blk = 0; blk < blocks; ++blk) {
            eng.process(p, {132.0, ppq, true}, midi, l.data(), r.data(), kBig);
            notes.take(midi);
            for (int i = 0; i < midi.count; ++i)
                noteOns += midi.events[static_cast<std::size_t>(i)].isNoteOn() ? 1 : 0;
            ppq += bps * static_cast<double>(kBig);
        }
        INFO("style " << style);
        CHECK(noteOns > 8);

        eng.process(p, {132.0, ppq, false}, midi, l.data(), r.data(), kBig);
        notes.take(midi);
        CHECK_FALSE(notes.anyOn());
        CHECK(eng.activeNotesLowMask() == 0);
        CHECK(eng.activeNotesHighMask() == 0);
    }
}

TEST_CASE("The engine emits exactly the compiled slicer phrase, phrase after phrase", "[engine][slicer]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    const HeldNotes held = chord();
    for (int i = 0; i < held.count; ++i)
        eng.inputNoteOn(held.notes[static_cast<std::size_t>(i)], held.velocities[static_cast<std::size_t>(i)]);
    const Params p = slicerParams(SlicerStyle::stabChop);

    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    NotesOn notes;
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    double ppq = 0.0;
    int noteOns = 0;
    const int blocks = static_cast<int>(std::ceil(7.9 / bps / static_cast<double>(kBlock))); // just under two phrases
    for (int blk = 0; blk < blocks; ++blk) {
        eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        notes.take(midi);
        for (int i = 0; i < midi.count; ++i)
            noteOns += midi.events[static_cast<std::size_t>(i)].isNoteOn() ? 1 : 0;
        ppq += bps * static_cast<double>(kBlock);
    }
    CHECK(noteOns == 30);
}

TEST_CASE("Clip Slice replays every captured note in every bar, also across bar lines", "[engine][slicer]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    Params p = slicerParams(SlicerStyle::clipSlice);
    p[ParamId::swing] = 0.3f; // beats 0 and 1 are even 16ths: Swing does not move them

    eng.inputNoteOn(36, 118, 0.0); // on the downbeat
    eng.inputNoteOff(36, 0.25);
    eng.inputNoteOn(38, 114, 1.0);
    eng.inputNoteOff(38, 1.25);
    REQUIRE(eng.capturedClipCount() == 2);

    // From beat 4 to just past beat 20. At 140 BPM a 64-sample block is not a whole fraction of a bar,
    // so the blocks straddle the bar lines at 8, 12, 16 and 20.
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    NotesOn notes;
    const double bps = 140.0 / 60.0 / kSr;
    double ppq = 4.0;
    int downbeats = 0;
    int backbeats = 0;
    const int blocks = static_cast<int>(std::ceil(16.2 / bps / static_cast<double>(kBlock)));
    for (int blk = 0; blk < blocks; ++blk) {
        eng.process(p, {140.0, ppq, true}, midi, l.data(), r.data(), kBlock);
        notes.take(midi);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            if (!ev.isNoteOn())
                continue;
            CHECK((ev.noteNumber() == 36 || ev.noteNumber() == 38));
            downbeats += ev.noteNumber() == 36 ? 1 : 0;
            backbeats += ev.noteNumber() == 38 ? 1 : 0;
        }
        ppq += bps * static_cast<double>(kBlock);
    }
    CHECK(downbeats == 5); // beats 4, 8, 12, 16, 20
    CHECK(backbeats == 4); // beats 5, 9, 13, 17
}

TEST_CASE("Pads in MIDI Slicer mode release every note", "[engine][slicer]") {
    for (int program = 0; program < kPerfPadCount; ++program) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.inputNoteOn(48, 100);
        eng.inputNoteOn(55, 100);
        Params p = slicerParams(SlicerStyle::stabChop);
        p[ParamId::slicerLatch] = 1.0f;
        p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
        p[ParamId::padProgram] = static_cast<float>(program);

        std::vector<float> l(kBlock), r(kBlock);
        MidiEventBuffer midi;
        NotesOn notes;
        const double bps = 132.0 / 60.0 / kSr;
        double ppq = 0.0;
        int balance = 0;
        int noteOns = 0;
        for (int blk = 0; blk < 2400; ++blk) {
            p[ParamId::padTouch] = (ppq >= 2.0 && ppq < 6.0) ? 1.0f : 0.0f;
            p[ParamId::padX] = 0.8f;
            eng.process(p, {132.0, ppq, true}, midi, l.data(), r.data(), kBlock);
            notes.take(midi);
            for (int i = 0; i < midi.count; ++i) {
                const auto& ev = midi.events[static_cast<std::size_t>(i)];
                balance += ev.isNoteOn() ? 1 : (ev.isNoteOff() ? -1 : 0);
                noteOns += ev.isNoteOn() ? 1 : 0;
            }
            ppq += bps * static_cast<double>(kBlock);
        }
        eng.process(p, {132.0, ppq, false}, midi, l.data(), r.data(), kBlock);
        notes.take(midi);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            balance += ev.isNoteOn() ? 1 : (ev.isNoteOff() ? -1 : 0);
        }
        INFO("program " << program);
        CHECK(noteOns > 4);
        CHECK(balance == 0);
        CHECK(eng.activeNotesLowMask() == 0);
    }
}
