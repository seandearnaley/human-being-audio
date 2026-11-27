// SPDX-License-Identifier: AGPL-3.0-or-later
// The four interim pad programs: Repeat, Roll, Rate, Drop.
#include "cl/engine.h"
#include "cl/groove_state.h"
#include "pad_programs.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <vector>

using namespace cl;
using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;
constexpr double kBpm = 132.0;

struct NoteOn {
    int note = 0;
    std::int64_t sample = 0;
    int velocity = 0;
    bool operator==(const NoteOn&) const = default;
};

struct Run {
    std::vector<NoteOn> noteOns;
    std::map<int, int> open; // note -> note-ons minus note-offs
    int controlChanges = 0;
    double endBeat = 0.0;
};

// Applies one block's MIDI to `out`. A note-on of a note that is already on is malformed MIDI.
void take(Run& out, const MidiEventBuffer& midi, std::int64_t firstSample) {
    for (int i = 0; i < midi.count; ++i) {
        const auto& ev = midi.events[static_cast<std::size_t>(i)];
        if (ev.isNoteOn()) {
            INFO("note " << ev.noteNumber() << " at sample " << firstSample + ev.sampleOffset);
            // A retrigger sends the old note's note-off first, so a note is never open twice.
            REQUIRE(out.open[ev.noteNumber()] == 0);
            out.noteOns.push_back({ev.noteNumber(), firstSample + ev.sampleOffset, static_cast<int>(ev.data[2])});
            ++out.open[ev.noteNumber()];
        } else if (ev.isNoteOff()) {
            if (out.open[ev.noteNumber()] > 0)
                --out.open[ev.noteNumber()];
        } else if ((ev.data[0] & 0xF0u) == 0xB0u) {
            ++out.controlChanges;
        }
    }
}

template <typename ParamsAt> Run play(Engine& eng, double hostBars, ParamsAt&& paramsAt, int blockSize = kBlock) {
    Run out;
    std::vector<float> l(static_cast<std::size_t>(blockSize)), r(static_cast<std::size_t>(blockSize));
    MidiEventBuffer midi;
    const double bps = kBpm / 60.0 / kSr;
    const int blocks = static_cast<int>(std::ceil(hostBars * 4.0 / bps / static_cast<double>(blockSize)));
    double ppq = 0.0;
    for (int blk = 0; blk < blocks; ++blk) {
        eng.process(paramsAt(ppq), {kBpm, ppq, true}, midi, l.data(), r.data(), blockSize);
        take(out, midi, static_cast<std::int64_t>(blk) * blockSize);
        ppq += bps * static_cast<double>(blockSize);
    }
    eng.process(paramsAt(ppq), {kBpm, ppq, false}, midi, l.data(), r.data(), blockSize); // stop: flushes what is held
    take(out, midi, static_cast<std::int64_t>(blocks) * blockSize);
    out.endBeat = ppq;
    return out;
}

Params padParams(PadProgram program, float x, float y) {
    Params p;
    p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
    p[ParamId::padProgram] = static_cast<float>(program);
    p[ParamId::padTouch] = 1.0f;
    p[ParamId::padX] = x;
    p[ParamId::padY] = y;
    return p;
}

int beatOf(const NoteOn& on) {
    // One sample late on purpose: a hit on a beat is rounded down to the sample before it.
    return static_cast<int>(std::floor((static_cast<double>(on.sample) + 1.0) * (kBpm / 60.0 / kSr)));
}

int countInBeat(const Run& run, int note, int beat) {
    int n = 0;
    for (const auto& on : run.noteOns)
        n += (on.note == note && beatOf(on) == beat) ? 1 : 0;
    return n;
}

// Default beat with a drawn two-beat snare fill.
GrooveState beatWithFill() {
    GrooveBank bank;
    bank.scenes[0].fill.beats = 2;
    bank.scenes[0].fill.steps[1][10].velocity = 96;
    bank.scenes[0].fill.steps[1][13].velocity = 96;
    return GrooveState(bank);
}

} // namespace

TEST_CASE("Pad X and Y map to the documented steps", "[pads]") {
    CHECK(pads::repeatWindowBeats(0.0f) == Approx(1.0));
    CHECK(pads::repeatWindowBeats(0.3f) == Approx(0.5));
    CHECK(pads::repeatWindowBeats(0.6f) == Approx(0.25));
    CHECK(pads::repeatWindowBeats(1.0f) == Approx(0.125));
    CHECK(pads::repeatGate(0.0f) == Approx(0.20));
    CHECK(pads::repeatGate(1.0f) == Approx(1.05));
    CHECK(pads::rollStepBeats(0.0f) == Approx(0.5));
    CHECK(pads::rollStepBeats(0.3f) == Approx(0.25));
    CHECK(pads::rollStepBeats(0.6f) == Approx(1.0 / 6.0));
    CHECK(pads::rollStepBeats(1.0f) == Approx(0.125));
    CHECK(pads::rollVelocity(0.0f) == 1);
    CHECK(pads::rollVelocity(1.0f) == 127);
    CHECK(pads::rateMultiplier(0.1f) == Approx(0.5));
    CHECK(pads::rateMultiplier(0.5f) == Approx(1.0));
    CHECK(pads::rateMultiplier(0.9f) == Approx(2.0));
    CHECK(pads::rateKeepsLane(0.1f, 0));
    CHECK(pads::rateKeepsLane(0.1f, 1));
    CHECK_FALSE(pads::rateKeepsLane(0.1f, 4));
    CHECK(pads::rateKeepsLane(0.5f, 4));
    CHECK_FALSE(pads::rateKeepsLane(0.9f, 0));
    CHECK(pads::rateKeepsLane(0.9f, 4));
    CHECK(pads::dropIsSilence(0.2f));
    CHECK_FALSE(pads::dropIsSilence(0.8f));
}

TEST_CASE("Every pad program releases all its notes", "[engine][pads]") {
    for (int program = 0; program < kPerfPadCount; ++program) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.setGroove(beatWithFill());
        const Run run = play(eng, 6.0, [&](double ppq) {
            const float sweep = static_cast<float>(std::fmod(ppq, 8.0) / 8.0);
            Params p = padParams(static_cast<PadProgram>(program), 0.05f + 0.9f * sweep, 0.95f - 0.9f * sweep);
            p[ParamId::padTouch] = (ppq >= 2.0 && ppq < 18.0) ? 1.0f : 0.0f;
            return p;
        });
        INFO("program " << program);
        CHECK(run.noteOns.size() > 8);
        for (const auto& [note, open] : run.open)
            CHECK(open == 0);
        CHECK(eng.activeNotesLowMask() == 0);
        CHECK(eng.activeNotesHighMask() == 0);
        CHECK(run.controlChanges == 0); // XY -> CC is Off by default
    }
}

TEST_CASE("A C3 note does not engage a pad with default parameters", "[engine][pads]") {
    Engine plain;
    plain.prepare(kSr, kBlock);
    const Run reference = play(plain, 2.0, [](double) { return Params{}; });

    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.inputNoteOn(60, 100, 0.0);
    const Run run = play(eng, 2.0, [](double) { return Params{}; });
    CHECK_FALSE(eng.padEngaged());
    CHECK(run.noteOns == reference.noteOns);

    // With Pad MIDI switched on, C#3 engages Roll.
    Engine armed;
    armed.prepare(kSr, kBlock);
    armed.inputNoteOn(61, 100, 0.0);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    Params p;
    p[ParamId::padMidi] = 1.0f;
    p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
    armed.process(p, {kBpm, 0.0, true}, midi, l.data(), r.data(), kBlock);
    CHECK(armed.padEngaged());
    CHECK(armed.activePadProgram() == static_cast<int>(PadProgram::roll));
}

TEST_CASE("Repeat loops the hits inside its window", "[engine][pads]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    // Engaged on beat 0 with a one-beat window: kick and hat on the beat, hat on the 8th, every beat.
    const Run run = play(eng, 2.0, [](double) { return padParams(PadProgram::repeat, 0.1f, 0.5f); });
    for (int beat = 1; beat <= 6; ++beat) {
        INFO("beat " << beat);
        CHECK(countInBeat(run, 36, beat) == 1);
        CHECK(countInBeat(run, 42, beat) == 2);
        CHECK(countInBeat(run, 38, beat) == 0);
    }
    // play() ends by stopping the transport, which clears the Repeat telemetry. Run one more block.
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    eng.process(padParams(PadProgram::repeat, 0.1f, 0.5f), {kBpm, run.endBeat, true}, midi, l.data(), r.data(), kBlock);
    CHECK(eng.position().repeatStartStep == 0);
    CHECK(eng.position().repeatBeats == Approx(1.0));
}

TEST_CASE("Roll plays the snare lane's note on a grid, at the velocity Y sets", "[engine][pads]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    const Run run = play(eng, 2.0, [](double) { return padParams(PadProgram::roll, 0.3f, 1.0f); });
    REQUIRE(run.noteOns.size() > 16);
    for (const auto& on : run.noteOns) {
        CHECK(on.note == 38);
        CHECK(on.velocity == 127);
    }
    for (int beat = 1; beat <= 6; ++beat)
        CHECK(countInBeat(run, 38, beat) == 4); // 1/16
}

TEST_CASE("Rate changes the speed and Y filters the kit", "[engine][pads]") {
    Engine pad;
    pad.prepare(kSr, kBlock);
    const Run doubled = play(pad, 2.0, [](double) { return padParams(PadProgram::rate, 0.9f, 0.5f); });

    Engine param;
    param.prepare(kSr, kBlock);
    const Run reference = play(param, 2.0, [](double) {
        Params p;
        p[ParamId::seqRate] = static_cast<float>(SeqRate::doubleTime);
        return p;
    });
    CHECK(doubled.noteOns == reference.noteOns);

    Engine low;
    low.prepare(kSr, kBlock);
    const Run kickSnare = play(low, 2.0, [](double) { return padParams(PadProgram::rate, 0.5f, 0.1f); });
    REQUIRE(kickSnare.noteOns.size() > 4);
    for (const auto& on : kickSnare.noteOns)
        CHECK((on.note == 36 || on.note == 38));

    Engine high;
    high.prepare(kSr, kBlock);
    const Run noKick = play(high, 2.0, [](double) { return padParams(PadProgram::rate, 0.5f, 0.9f); });
    REQUIRE(noKick.noteOns.size() > 4);
    for (const auto& on : noKick.noteOns)
        CHECK(on.note != 36);
}

TEST_CASE("Drop is silence on the left and the fill on the right", "[engine][pads]") {
    Engine left;
    left.prepare(kSr, kBlock);
    left.setGroove(beatWithFill());
    const Run silent = play(left, 2.0, [](double) { return padParams(PadProgram::drop, 0.2f, 0.5f); });
    CHECK(silent.noteOns.empty());

    Engine right;
    right.prepare(kSr, kBlock);
    right.setGroove(beatWithFill());
    const Run dropped = play(right, 2.0, [](double) { return padParams(PadProgram::drop, 0.8f, 0.5f); });

    Engine param;
    param.prepare(kSr, kBlock);
    param.setGroove(beatWithFill());
    const Run reference = play(param, 2.0, [](double) {
        Params p;
        p[ParamId::fillNow] = 1.0f;
        return p;
    });
    REQUIRE(reference.noteOns.size() > 10);
    CHECK(dropped.noteOns == reference.noteOns);
}

TEST_CASE("Releasing a pad returns to the pattern in phase", "[engine][pads]") {
    Engine plain;
    plain.prepare(kSr, kBlock);
    const Run reference = play(plain, 4.0, [](double) { return Params{}; });

    for (int program = 0; program < kPerfPadCount; ++program) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        const Run run = play(eng, 4.0, [&](double ppq) {
            Params p = padParams(static_cast<PadProgram>(program), 0.9f, 0.2f);
            p[ParamId::padTouch] = (ppq >= 4.0 && ppq < 6.0) ? 1.0f : 0.0f;
            return p;
        });
        const auto from = static_cast<std::int64_t>(7.0 / (kBpm / 60.0 / kSr));
        std::vector<NoteOn> a, b;
        for (const auto& on : run.noteOns)
            if (on.sample >= from)
                a.push_back(on);
        for (const auto& on : reference.noteOns)
            if (on.sample >= from)
                b.push_back(on);
        INFO("program " << program);
        REQUIRE(b.size() > 10);
        CHECK(a == b);
    }
}

TEST_CASE("A pad touched mid-beat engages on the next 1/16 at default parameters", "[engine][pads]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    const Run run = play(eng, 2.0, [](double ppq) {
        Params p; // sceneQuantize left at its default
        p[ParamId::padProgram] = static_cast<float>(PadProgram::roll);
        p[ParamId::padX] = 0.3f; // 1/16 roll
        p[ParamId::padY] = 1.0f; // velocity 127
        p[ParamId::padTouch] = ppq >= 1.1 ? 1.0f : 0.0f;
        return p;
    });
    const double bps = kBpm / 60.0 / kSr;

    const NoteOn* firstRoll = nullptr;
    for (const auto& on : run.noteOns) {
        if (on.note == 38 && on.velocity == 127) {
            firstRoll = &on;
            break;
        }
    }
    REQUIRE(firstRoll != nullptr);
    CHECK(std::llabs(firstRoll->sample - static_cast<std::int64_t>(std::floor(1.25 / bps + 1.0e-7))) <= 1);

    // The pattern's closed hat on beat 1 still sounds: the pad had not engaged yet.
    bool hatOnBeat1 = false;
    for (const auto& on : run.noteOns)
        hatOnBeat1 = hatOnBeat1 || (on.note == 42 && std::llabs(on.sample - static_cast<std::int64_t>(1.0 / bps)) <= 1);
    CHECK(hatOnBeat1);
}

TEST_CASE("Repeat keeps playing after the transport jumps back", "[engine][pads]") {
    // Repeat is engaged on beat 4, so its window is anchored there. Then the host loops back to beat 2.
    Engine eng;
    eng.prepare(kSr, kBlock);
    Run run;
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bps = kBpm / 60.0 / kSr;
    bool touched = false; // held from beat 4 on, through the jump
    const auto paramsAt = [&](double ppq) {
        touched = touched || ppq >= 4.0;
        Params p = padParams(PadProgram::repeat, 0.1f, 0.5f);
        p[ParamId::padTouch] = touched ? 1.0f : 0.0f;
        return p;
    };
    double ppq = 0.0;
    std::int64_t sample = 0;
    const auto playUntil = [&](double end) {
        while (ppq < end) {
            eng.process(paramsAt(ppq), {kBpm, ppq, true}, midi, l.data(), r.data(), kBlock);
            take(run, midi, sample);
            sample += kBlock;
            ppq += bps * static_cast<double>(kBlock);
        }
    };
    playUntil(6.0);
    REQUIRE(eng.padEngaged());
    ppq = 2.0; // the jump
    const std::int64_t jumpSample = sample;
    playUntil(4.0);
    int afterJump = 0;
    for (const auto& on : run.noteOns)
        afterJump += (on.sample >= jumpSample && static_cast<double>(on.sample - jumpSample) * bps < 1.0) ? 1 : 0;
    CHECK(afterJump > 0);
}

TEST_CASE("A pattern hit before the pad's switch point plays, even in the block where the pad engages",
          "[engine][pads]") {
    // Perc A (note 43) nudged to 2.21875, just before the pad's switch point at 2.25. With 2048-sample blocks the
    // block that commits the pad starts before both.
    GrooveBank bank;
    Step& s = bank.scenes[0].lanes[6].steps[9];
    s.velocity = 90;
    s.layer = Layer::core;
    s.nudge = -3;
    constexpr int kBig = 2048;
    const double bps = kBpm / 60.0 / kSr;
    Engine eng;
    eng.prepare(kSr, kBig);
    eng.setGroove(GrooveState(bank));
    const Run run = play(
        eng, 1.0,
        [](double ppq) {
            Params p; // pads quantize to 1/16 whatever the scene quantize is
            p[ParamId::padProgram] = static_cast<float>(PadProgram::roll);
            p[ParamId::padX] = 0.3f;
            p[ParamId::padY] = 1.0f;
            p[ParamId::padTouch] = ppq >= 2.05 ? 1.0f : 0.0f;
            return p;
        },
        kBig);
    const auto at = static_cast<std::int64_t>(std::floor(2.21875 / bps));
    REQUIRE(static_cast<std::int64_t>(std::floor(2.25 / bps)) / kBig == at / kBig); // same block, as intended
    bool percA = false;
    for (const auto& on : run.noteOns)
        percA = percA || (on.note == 43 && std::llabs(on.sample - at) <= 1);
    CHECK(percA);
    const NoteOn* firstRoll = nullptr;
    for (const auto& on : run.noteOns) {
        if (on.note == 38 && on.velocity == 127) {
            firstRoll = &on;
            break;
        }
    }
    REQUIRE(firstRoll != nullptr);
    CHECK(std::llabs(firstRoll->sample - static_cast<std::int64_t>(std::floor(2.25 / bps + 1.0e-7))) <= 1);
}

TEST_CASE("With Shift, a pad engages on a 1/16 of the pattern as it sounds", "[engine][pads]") {
    // Shift +20 ms: the pattern sounds 0.044 beat late. Touching at host beat 1.27 is window beat 1.226, so the
    // next 1/16 of the pattern is window beat 1.25, which sounds at host beat 1.25 + shift.
    const float shiftMs = 20.0f;
    const double shift = static_cast<double>(shiftMs) * 1.0e-3 * (kBpm / 60.0);
    Engine eng;
    eng.prepare(kSr, kBlock);
    const Run run = play(eng, 1.0, [&](double ppq) {
        Params p;
        p[ParamId::shiftMs] = shiftMs;
        p[ParamId::padProgram] = static_cast<float>(PadProgram::roll);
        p[ParamId::padX] = 0.3f;
        p[ParamId::padY] = 1.0f;
        p[ParamId::padTouch] = ppq >= 1.27 ? 1.0f : 0.0f;
        return p;
    });
    const double bps = kBpm / 60.0 / kSr;
    const NoteOn* firstRoll = nullptr;
    for (const auto& on : run.noteOns) {
        if (on.note == 38 && on.velocity == 127) {
            firstRoll = &on;
            break;
        }
    }
    REQUIRE(firstRoll != nullptr);
    INFO("first roll note at host beat " << static_cast<double>(firstRoll->sample) * bps);
    CHECK(std::llabs(firstRoll->sample - static_cast<std::int64_t>(std::floor((1.25 + shift) / bps + 1.0e-7))) <= 1);
}
