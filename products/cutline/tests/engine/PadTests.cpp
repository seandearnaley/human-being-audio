// SPDX-License-Identifier: AGPL-3.0-or-later
// The six PLAY programs: Repeat, Roll, Density, Rate, Drop, Fill.
#include "cl/engine.h"
#include "cl/groove_state.h"
#include "cl/pad_programs.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>
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
    std::map<int, int> open;      // note -> note-ons minus note-offs
    std::vector<NoteOn> noteOffs; // note-offs of notes that were on, velocity 0
    int controlChanges = 0;
    std::vector<std::int64_t> ccSamples; // where each control change was sent
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
            if (out.open[ev.noteNumber()] > 0) {
                --out.open[ev.noteNumber()];
                out.noteOffs.push_back({ev.noteNumber(), firstSample + ev.sampleOffset, 0});
            }
        } else if ((ev.data[0] & 0xF0u) == 0xB0u) {
            ++out.controlChanges;
            out.ccSamples.push_back(firstSample + ev.sampleOffset);
        }
    }
}

template <typename ParamsAt>
Run play(Engine& eng, double hostBars, ParamsAt&& paramsAt, int blockSize = kBlock, double bpm = kBpm) {
    Run out;
    std::vector<float> l(static_cast<std::size_t>(blockSize)), r(static_cast<std::size_t>(blockSize));
    MidiEventBuffer midi;
    const double bps = bpm / 60.0 / kSr;
    const int blocks = static_cast<int>(std::ceil(hostBars * 4.0 / bps / static_cast<double>(blockSize)));
    double ppq = 0.0;
    for (int blk = 0; blk < blocks; ++blk) {
        eng.process(paramsAt(ppq), {bpm, ppq, true}, midi, l.data(), r.data(), blockSize);
        take(out, midi, static_cast<std::int64_t>(blk) * blockSize);
        ppq += bps * static_cast<double>(blockSize);
    }
    eng.process(paramsAt(ppq), {bpm, ppq, false}, midi, l.data(), r.data(), blockSize); // stop: flushes what is held
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

// The first note-on of `note` at or after `fromSample`: with the default beat's snare on beats 1 and 3, a snare after
// the pad was touched is the roll's first note.
const NoteOn* firstNoteFrom(const Run& run, int note, std::int64_t fromSample) {
    for (const auto& on : run.noteOns)
        if (on.note == note && on.sample >= fromSample)
            return &on;
    return nullptr;
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
    CHECK(pads::rollLane(0.1f) == 0); // bottom third: kick
    CHECK(pads::rollLane(0.5f) == 1); // middle third: snare
    CHECK(pads::rollLane(0.9f) == 4); // top third: closed hat
    CHECK(pads::rollVelocity(0.0, 0.25) == 72);
    CHECK(pads::rollVelocity(3.75, 0.25) == 120);
    CHECK(pads::rollVelocity(3.875, 0.125) == 120);
    CHECK(pads::rollVelocity(23.0 / 6.0, 1.0 / 6.0) == 120);
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
    CHECK_FALSE(pads::dropIsSilence(0.3f));
    CHECK(pads::dropZone(0.3f) == pads::DropZone::kickOnly);
    CHECK(pads::dropZone(0.6f) == pads::DropZone::kickAndSnare);
    CHECK(pads::dropZone(0.9f) == pads::DropZone::noKick);
}

TEST_CASE("The six programs are in the documented order", "[pads]") {
    CHECK(kPerfPadCount == 6);
    const std::array<std::string_view, kPerfPadCount> expected{"Repeat", "Roll", "Density", "Rate", "Drop", "Fill"};
    for (int i = 0; i < kPerfPadCount; ++i) {
        CHECK(spec(ParamId::padProgram).choices[static_cast<std::size_t>(i)] == expected[static_cast<std::size_t>(i)]);
        CHECK(kPerfPadLabels[static_cast<std::size_t>(i)] == expected[static_cast<std::size_t>(i)]);
    }
    CHECK(static_cast<int>(PadProgram::density) == 2);
    CHECK(static_cast<int>(PadProgram::fill) == 5);
    CHECK(Params{}.choice(ParamId::padProgram) == static_cast<int>(PadProgram::repeat));
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

TEST_CASE("Pad MIDI: C3 to F3 engage the six programs in order", "[engine][pads]") {
    for (int program = 0; program < kPerfPadCount; ++program) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.inputNoteOn(60 + program, 100, 0.0);
        std::vector<float> l(kBlock), r(kBlock);
        MidiEventBuffer midi;
        Params p;
        p[ParamId::padMidi] = 1.0f;
        eng.process(p, {kBpm, 0.0, true}, midi, l.data(), r.data(), kBlock);
        INFO("note " << 60 + program);
        CHECK(eng.padEngaged());
        CHECK(eng.activePadProgram() == program);
        CHECK(eng.midiPadMask() == (1u << static_cast<unsigned>(program)));
    }
    // F#3 is not a pad.
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.inputNoteOn(66, 100, 0.0);
    CHECK(eng.midiPadMask() == 0);
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

TEST_CASE("Roll rolls the lane Y picks, rising from 72 to 120 across each bar, over the pattern", "[engine][pads]") {
    Engine plain;
    plain.prepare(kSr, kBlock);
    const Run reference = play(plain, 2.0, [](double) { return Params{}; });

    struct Case {
        float y;
        int note;
    };
    for (const Case c : {Case{0.1f, 36}, Case{0.5f, 38}, Case{0.9f, 42}}) {
        INFO("Y " << c.y << ", note " << c.note);
        Engine eng;
        eng.prepare(kSr, kBlock);
        const Run run = play(eng, 2.0, [&](double) { return padParams(PadProgram::roll, 0.3f, c.y); });

        // The rolled lane: every 1/16, and in each bar 72 on its first note, rising to 120 on its last.
        std::vector<NoteOn> rolled;
        for (const auto& on : run.noteOns)
            if (on.note == c.note && beatOf(on) < 8) // play() can reach the next bar's downbeat
                rolled.push_back(on);
        REQUIRE(rolled.size() == 32); // two bars of 1/16
        for (int beat = 0; beat < 8; ++beat)
            CHECK(countInBeat(run, c.note, beat) == 4);
        for (std::size_t i = 0; i < rolled.size(); ++i) {
            const int k = static_cast<int>(i % 16);
            INFO("roll note " << i);
            CHECK(rolled[i].velocity == static_cast<int>(std::lround(72.0 + 48.0 * k / 15.0)));
        }
        CHECK(rolled[0].velocity == 72);
        CHECK(rolled[15].velocity == 120);
        CHECK(rolled[16].velocity == 72);

        // The other lanes keep playing the pattern.
        std::vector<NoteOn> others, expected;
        for (const auto& on : run.noteOns)
            if (on.note != c.note)
                others.push_back(on);
        for (const auto& on : reference.noteOns)
            if (on.note != c.note)
                expected.push_back(on);
        REQUIRE_FALSE(expected.empty());
        CHECK(others == expected);
    }
}

TEST_CASE("Density's X and Y replace Density and Dynamics while it is held, and telemetry reports them",
          "[engine][pads]") {
    // Core, Groove and Detail hits at several velocities, so Density and Dynamics both change what is sent.
    GrooveBank bank;
    Pattern& pat = bank.scenes[0];
    pat = Pattern{};
    const auto put = [&pat](int lane, int step, int velocity, Layer layer) {
        Step& s = pat.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
        s.velocity = static_cast<std::uint8_t>(velocity);
        s.layer = layer;
    };
    put(0, 0, 118, Layer::core);
    put(0, 10, 96, Layer::groove);
    put(1, 4, 118, Layer::core);
    put(1, 12, 110, Layer::core);
    put(2, 7, 56, Layer::detail);
    put(2, 15, 50, Layer::detail);
    for (int step = 0; step < kStepsPerBar; step += 2)
        put(4, step, step % 4 == 0 ? 100 : 70, step % 8 == 0 ? Layer::core : Layer::groove);
    put(4, 3, 40, Layer::detail);

    for (const auto& [x, y] : {std::pair{0.95f, 0.2f}, std::pair{0.1f, 0.9f}, std::pair{0.6f, 0.5f}}) {
        INFO("X " << x << ", Y " << y);
        Engine pad;
        pad.prepare(kSr, kBlock);
        pad.setGroove(GrooveState(bank));
        const Run held = play(pad, 2.0, [&](double) { return padParams(PadProgram::density, x, y); });

        Engine param;
        param.prepare(kSr, kBlock);
        param.setGroove(GrooveState(bank));
        const Run reference = play(param, 2.0, [&](double) {
            Params p;
            p[ParamId::density] = x;
            p[ParamId::dynamics] = y;
            return p;
        });
        REQUIRE(reference.noteOns.size() > 10);
        CHECK(held.noteOns == reference.noteOns);

        std::vector<float> l(kBlock), r(kBlock);
        MidiEventBuffer midi;
        pad.process(padParams(PadProgram::density, x, y), {kBpm, held.endBeat, true}, midi, l.data(), r.data(), kBlock);
        CHECK(pad.position().density == Approx(x));
        CHECK(pad.position().dynamics == Approx(y));
        Params released = padParams(PadProgram::density, x, y);
        released[ParamId::padTouch] = 0.0f;
        pad.process(released, {kBpm, held.endBeat + 0.01, true}, midi, l.data(), r.data(), kBlock);
        CHECK(pad.position().density == Approx(spec(ParamId::density).def));
        CHECK(pad.position().dynamics == Approx(spec(ParamId::dynamics).def));
    }
}

TEST_CASE("Fill plays every bar's fill while it is held, as fillNow does", "[engine][pads]") {
    Engine pad;
    pad.prepare(kSr, kBlock);
    pad.setGroove(beatWithFill());
    const Run held = play(pad, 3.0, [](double) { return padParams(PadProgram::fill, 0.5f, 0.5f); });

    Engine param;
    param.prepare(kSr, kBlock);
    param.setGroove(beatWithFill());
    const Run reference = play(param, 3.0, [](double) {
        Params p;
        p[ParamId::fillNow] = 1.0f;
        return p;
    });
    REQUIRE(reference.noteOns.size() > 10);
    CHECK(held.noteOns == reference.noteOns);
    // Every bar plays the fill's snare on steps 10 and 13: beats 2.5 and 3.25 of each bar.
    for (int bar = 0; bar < 3; ++bar)
        CHECK(countInBeat(held, 38, bar * 4 + 2) == 1);
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

TEST_CASE("Drop's four zones: silence, kick only, kick and snare, everything except kick", "[engine][pads]") {
    // Kick, snare, ghost and closed hat, so each zone keeps a different set.
    GrooveBank bank;
    bank.scenes[0].lanes[2].steps[7].velocity = 60;
    bank.scenes[0].lanes[2].steps[7].layer = Layer::core;
    Engine plain;
    plain.prepare(kSr, kBlock);
    plain.setGroove(GrooveState(bank));
    const Run reference = play(plain, 2.0, [](double) { return Params{}; });

    struct Zone {
        float x;
        std::vector<int> keeps; // notes the zone lets through
    };
    for (const Zone& zone : {Zone{0.1f, {}}, Zone{0.4f, {36}}, Zone{0.6f, {36, 38}}, Zone{0.9f, {38, 37, 42}}}) {
        INFO("X " << zone.x);
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.setGroove(GrooveState(bank));
        const Run run = play(eng, 2.0, [&](double) { return padParams(PadProgram::drop, zone.x, 0.5f); });
        std::vector<NoteOn> expected;
        for (const auto& on : reference.noteOns)
            if (std::find(zone.keeps.begin(), zone.keeps.end(), on.note) != zone.keeps.end())
                expected.push_back(on);
        CHECK(run.noteOns == expected);
    }
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
        p[ParamId::padY] = 0.5f; // on the snare
        p[ParamId::padTouch] = ppq >= 1.1 ? 1.0f : 0.0f;
        return p;
    });
    const double bps = kBpm / 60.0 / kSr;

    const NoteOn* firstRoll = firstNoteFrom(run, 38, static_cast<std::int64_t>(1.1 / bps));
    REQUIRE(firstRoll != nullptr);
    CHECK(std::llabs(firstRoll->sample - static_cast<std::int64_t>(std::floor(1.25 / bps + 1.0e-7))) <= 1);
    CHECK(firstRoll->velocity == pads::rollVelocity(1.25, 0.25));

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
            p[ParamId::padProgram] = static_cast<float>(PadProgram::drop);
            p[ParamId::padX] = 0.3f; // kick only, from the switch point on
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
    // From the switch point on, only the kick plays: the hats on 2.5 and 3.0 and the snare on 3.0 are dropped.
    for (const auto& on : run.noteOns) {
        INFO("note " << on.note << " at host beat " << static_cast<double>(on.sample) * bps);
        CHECK((on.sample < static_cast<std::int64_t>(std::floor(2.25 / bps)) || on.note == 36));
    }
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
        p[ParamId::padY] = 0.5f; // on the snare
        p[ParamId::padTouch] = ppq >= 1.27 ? 1.0f : 0.0f;
        return p;
    });
    const double bps = kBpm / 60.0 / kSr;
    const NoteOn* firstRoll = firstNoteFrom(run, 38, static_cast<std::int64_t>(1.27 / bps));
    REQUIRE(firstRoll != nullptr);
    INFO("first roll note at host beat " << static_cast<double>(firstRoll->sample) * bps);
    CHECK(std::llabs(firstRoll->sample - static_cast<std::int64_t>(std::floor((1.25 + shift) / bps + 1.0e-7))) <= 1);
}

TEST_CASE("Drop silences in time order: the hits before its switch point are released at it", "[engine][pads]") {
    // Snare on step 8 (Gate 1.0: it rings past beat 2.25) and a flammed snare on step 9, nudged late, whose grace
    // hit falls just before the Drop point at 2.25. With 2048-sample blocks, the grace hit and the Drop point fall in
    // the block that commits the pad.
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    for (const int step : {8, 9}) {
        Step& st = bank.scenes[0].lanes[1].steps[static_cast<std::size_t>(step)];
        st.velocity = 96;
        st.layer = Layer::core;
    }
    bank.scenes[0].lanes[1].steps[9].flam = true;
    bank.scenes[0].lanes[1].steps[9].nudge = 1;
    constexpr int kBig = 2048;
    const double bps = kBpm / 60.0 / kSr;
    Engine eng;
    eng.prepare(kSr, kBig);
    eng.setGroove(GrooveState(bank));
    const Run run = play(
        eng, 1.0,
        [](double ppq) {
            Params p;
            p[ParamId::gateLen] = 1.0f;
            p[ParamId::padProgram] = static_cast<float>(PadProgram::drop);
            p[ParamId::padX] = 0.2f; // silence
            p[ParamId::padTouch] = (ppq >= 2.05 && ppq < 3.5) ? 1.0f : 0.0f;
            return p;
        },
        kBig);
    const auto dropAt = static_cast<std::int64_t>(std::floor(2.25 / bps));
    const auto releasedAt = static_cast<std::int64_t>(std::ceil(3.5 / bps)) + kBig; // released on a block start
    REQUIRE(static_cast<std::int64_t>(std::floor((2.25 - 0.044) / bps)) / kBig == dropAt / kBig);

    for (const auto& on : run.noteOns) {
        INFO("note-on " << on.note << " at host beat " << static_cast<double>(on.sample) * bps);
        CHECK_FALSE((on.sample > dropAt + 1 && on.sample < releasedAt - 2 * kBig));
    }
    int before = 0;
    for (const auto& on : run.noteOns) {
        if (on.sample > dropAt + 1)
            continue;
        ++before;
        std::int64_t off = -1;
        for (const auto& o : run.noteOffs) {
            if (o.note == on.note && o.sample >= on.sample) {
                off = o.sample;
                break;
            }
        }
        INFO("note " << on.note << " on at host beat " << static_cast<double>(on.sample) * bps << ", off at "
                     << static_cast<double>(off) * bps);
        CHECK((off >= 0 && off <= dropAt + 1));
    }
    CHECK(before >= 2);
}

TEST_CASE("A pattern hit before the pad's switch point keeps its velocity in CC74 + Vel mode", "[engine][pads]") {
    // Perc A (note 43) nudged to 2.21875, in the block that commits the Rate pad at 2.25. The pad's Y adds velocity
    // only to what plays at or after its switch point.
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
            Params p;
            p[ParamId::padCcMode] = static_cast<float>(PadCcMode::cc74AndVel);
            p[ParamId::padProgram] = static_cast<float>(PadProgram::rate);
            p[ParamId::padX] = 0.5f; // 1x
            p[ParamId::padY] = 1.0f; // +22 velocity while the pad plays
            p[ParamId::padTouch] = ppq >= 2.05 ? 1.0f : 0.0f;
            return p;
        },
        kBig);
    const auto at = static_cast<std::int64_t>(std::floor(2.21875 / bps));
    REQUIRE(static_cast<std::int64_t>(std::floor(2.25 / bps)) / kBig == at / kBig);
    const NoteOn* percA = nullptr;
    for (const auto& on : run.noteOns)
        if (on.note == 43 && std::llabs(on.sample - at) <= 1)
            percA = &on;
    REQUIRE(percA != nullptr);
    CHECK(percA->velocity == 90);
}

TEST_CASE("Drop silences when its switch point lands on the block's end", "[engine][pads]") {
    // 120 BPM, 1000-sample blocks: every 6th block ends exactly on a 1/16, so the block ending at 2.5 commits the
    // Drop with its switch point on the block's end. The snare at 2.28125 (step 9, nudged +3, Gate 1.0) would ring
    // to 2.54: it must stop at the Drop point.
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    for (const int step : {8, 9}) {
        Step& st = bank.scenes[0].lanes[1].steps[static_cast<std::size_t>(step)];
        st.velocity = 96;
        st.layer = Layer::core;
    }
    bank.scenes[0].lanes[1].steps[9].nudge = 3;
    constexpr int kGridBlock = 1000;
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    Engine eng;
    eng.prepare(kSr, kGridBlock);
    eng.setGroove(GrooveState(bank));
    const Run run = play(
        eng, 1.0,
        [](double ppq) {
            Params p;
            p[ParamId::gateLen] = 1.0f;
            p[ParamId::padProgram] = static_cast<float>(PadProgram::drop);
            p[ParamId::padX] = 0.2f; // silence
            p[ParamId::padTouch] = ppq >= 2.30 ? 1.0f : 0.0f;
            return p;
        },
        kGridBlock, bpm);
    const auto dropAt = static_cast<std::int64_t>(std::llround(2.5 / bps));
    REQUIRE(dropAt % kGridBlock == 0); // the Drop point is a block boundary
    const auto snareAt = static_cast<std::int64_t>(std::floor(2.28125 / bps + 1.0e-7));
    bool snare = false;
    for (const auto& on : run.noteOns) {
        if (on.note != 38 || std::llabs(on.sample - snareAt) > 1)
            continue;
        snare = true;
        std::int64_t off = -1;
        for (const auto& o : run.noteOffs) {
            if (o.note == 38 && o.sample >= on.sample) {
                off = o.sample;
                break;
            }
        }
        INFO("snare off at host beat " << static_cast<double>(off) * bps);
        CHECK((off >= 0 && off <= dropAt + 1));
    }
    CHECK(snare);
}

TEST_CASE("A pending pad engage is re-quantized after the transport jumps back", "[engine][pads]") {
    // Roll is touched at beat 5.1 (due at 5.25); the host jumps back to 1.03 before it commits, so the first roll
    // note falls on the next 1/16 after the jump, 1.25.
    Engine eng;
    eng.prepare(kSr, kBlock);
    Run run;
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bps = kBpm / 60.0 / kSr;
    bool touched = false;
    double ppq = 0.0;
    std::int64_t sample = 0;
    const auto step = [&]() {
        touched = touched || ppq >= 5.1;
        Params p;
        p[ParamId::padProgram] = static_cast<float>(PadProgram::roll);
        p[ParamId::padX] = 0.3f;
        p[ParamId::padY] = 0.5f; // on the snare
        p[ParamId::padTouch] = touched ? 1.0f : 0.0f;
        eng.process(p, {kBpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        take(run, midi, sample);
        sample += kBlock;
        ppq += bps * static_cast<double>(kBlock);
    };
    while (!touched)
        step();
    REQUIRE_FALSE(eng.padEngaged());
    ppq = 1.03; // the jump, before the engage commits
    const std::int64_t jumpSample = sample;
    while (ppq < 2.0)
        step();
    // After the jump the pattern's next snare is on beat 3, so the first snare is the roll's.
    const NoteOn* firstRoll = firstNoteFrom(run, 38, jumpSample);
    REQUIRE(firstRoll != nullptr);
    const double at = 1.03 + static_cast<double>(firstRoll->sample - jumpSample) * bps;
    INFO("first roll note at beat " << at);
    CHECK(std::abs(at - 1.25) <= 2.0 * bps);
}

TEST_CASE("No pad CC is sent before the pad's switch point in the block where it engages", "[engine][pads]") {
    constexpr int kBig = 2048;
    const double bps = kBpm / 60.0 / kSr;
    Engine eng;
    eng.prepare(kSr, kBig);
    const Run run = play(
        eng, 1.0,
        [](double ppq) {
            Params p;
            p[ParamId::padCcMode] = static_cast<float>(PadCcMode::cc74AndCc1);
            p[ParamId::padProgram] = static_cast<float>(PadProgram::rate);
            p[ParamId::padX] = 0.5f;
            p[ParamId::padTouch] = ppq >= 2.05 ? 1.0f : 0.0f;
            return p;
        },
        kBig);
    const auto padAt = static_cast<std::int64_t>(std::floor(2.25 / bps));
    REQUIRE(padAt % kBig != 0); // the switch point is inside a block
    REQUIRE_FALSE(run.ccSamples.empty());
    for (const auto at : run.ccSamples) {
        INFO("CC at host beat " << static_cast<double>(at) * bps);
        CHECK(at >= padAt - 1);
    }
}

TEST_CASE("Pre-roll: Roll held from before beat 0 plays its first note on beat 0", "[engine][pads]") {
    const double bps = kBpm / 60.0 / kSr;
    Engine eng;
    eng.prepare(kSr, kBlock);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    Run run;
    const Params p = padParams(PadProgram::roll, 0.0f, 0.5f); // 1/8 on the snare
    double ppq = -4.0;
    std::int64_t sample = 0;
    while (ppq < 1.0) {
        eng.process(p, {kBpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        take(run, midi, sample);
        sample += kBlock;
        ppq += bps * static_cast<double>(kBlock);
    }
    REQUIRE_FALSE(run.noteOns.empty());
    for (const auto& on : run.noteOns) {
        const double at = -4.0 + static_cast<double>(on.sample) * bps;
        INFO("note " << on.note << " at host beat " << at);
        CHECK(at >= -bps);
    }
    const double first = -4.0 + static_cast<double>(run.noteOns.front().sample) * bps;
    CHECK(std::abs(first) <= 2.0 * bps);
    const NoteOn* firstRoll = firstNoteFrom(run, 38, 0);
    REQUIRE(firstRoll != nullptr);
    CHECK(std::abs(-4.0 + static_cast<double>(firstRoll->sample) * bps) <= 2.0 * bps);
    CHECK(firstRoll->velocity == 72);
}

TEST_CASE("Pre-roll: Repeat held from before beat 0 loops from beat 0", "[engine][pads]") {
    const double bps = kBpm / 60.0 / kSr;
    Engine eng;
    eng.prepare(kSr, kBlock);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    Run run;
    const Params p = padParams(PadProgram::repeat, 0.1f, 0.5f); // a one-beat window
    double ppq = -4.0;
    std::int64_t sample = 0;
    while (ppq < 2.0) {
        eng.process(p, {kBpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        take(run, midi, sample);
        sample += kBlock;
        ppq += bps * static_cast<double>(kBlock);
    }
    REQUIRE_FALSE(run.noteOns.empty());
    for (const auto& on : run.noteOns) {
        const double at = -4.0 + static_cast<double>(on.sample) * bps;
        INFO("note " << on.note << " at host beat " << at);
        CHECK(at >= -bps);
    }
    // It loops beat 0 of bar 0: the default beat's kick, once on each beat from beat 0. beatOf counts from the run's
    // start, four beats before beat 0.
    CHECK(countInBeat(run, 36, 4) == 1);
    CHECK(countInBeat(run, 36, 5) == 1);
    CHECK(std::abs(-4.0 + static_cast<double>(run.noteOns.front().sample) * bps) <= 2.0 * bps);
}

TEST_CASE("In MIDI Slicer mode Density and Fill do nothing and Drop only silences", "[engine][pads][slicer]") {
    const auto slicerRun = [](PadProgram program, float x, float y, bool touch) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.setGroove(beatWithFill());
        for (const int note : {48, 55, 67})
            eng.inputNoteOn(note, 100, 0.0);
        return play(eng, 2.0, [&](double) {
            Params p;
            p[ParamId::engineMode] = static_cast<float>(EngineMode::slicer);
            p[ParamId::padProgram] = static_cast<float>(program);
            p[ParamId::padX] = x;
            p[ParamId::padY] = y;
            p[ParamId::padTouch] = touch ? 1.0f : 0.0f;
            return p;
        });
    };
    const Run reference = slicerRun(PadProgram::repeat, 0.5f, 0.5f, false);
    REQUIRE(reference.noteOns.size() > 8);
    CHECK(slicerRun(PadProgram::density, 0.05f, 0.95f, true).noteOns == reference.noteOns);
    CHECK(slicerRun(PadProgram::fill, 0.5f, 0.5f, true).noteOns == reference.noteOns);
    for (const float x : {0.3f, 0.6f, 0.9f}) {
        INFO("Drop X " << x);
        CHECK(slicerRun(PadProgram::drop, x, 0.5f, true).noteOns == reference.noteOns);
    }
    CHECK(slicerRun(PadProgram::drop, 0.1f, 0.5f, true).noteOns.empty());
    // Rate applies: the phrase plays twice as fast.
    CHECK(slicerRun(PadProgram::rate, 0.9f, 0.5f, true).noteOns.size() > reference.noteOns.size());
}

TEST_CASE("The sent-note telemetry lists exactly the note-ons in the MIDI buffer", "[engine][pads]") {
    // A busy two-bar groove: ratchets, a flam, nudges, swing and a fill, with the pads switching underneath.
    GrooveBank bank;
    Pattern& pat = bank.scenes[0];
    setPatternBars(pat, 2);
    for (int step = 0; step < 32; step += 3) {
        Step& s = pat.lanes[6].steps[static_cast<std::size_t>(step)];
        s.velocity = 80;
        s.layer = Layer::groove;
        s.nudge = static_cast<std::int8_t>(step % 5 - 2);
    }
    pat.lanes[4].steps[6].ratchet = 3;
    pat.lanes[1].steps[12].flam = true;
    pat.fill.steps[1][14].velocity = 110;

    for (const bool slicer : {false, true}) {
        INFO((slicer ? "MIDI Slicer" : "drums"));
        Engine eng;
        eng.prepare(kSr, 512);
        eng.setGroove(GrooveState(bank));
        for (const int note : {48, 55, 67})
            eng.inputNoteOn(note, 100, 0.0);
        std::vector<float> l(512), r(512);
        MidiEventBuffer midi;
        const double bps = kBpm / 60.0 / kSr;
        double ppq = 0.0;
        std::size_t total = 0;
        for (int blk = 0; ppq < 24.0; ++blk) {
            const int n = 64 + (blk * 37) % 449; // uneven block sizes
            Params p;
            p[ParamId::engineMode] = slicer ? 1.0f : 0.0f;
            p[ParamId::swing] = 0.4f;
            p[ParamId::fillEvery] = 1.0f; // every 2 bars
            p[ParamId::padProgram] = static_cast<float>(static_cast<int>(ppq / 2.0) % kPerfPadCount);
            p[ParamId::padTouch] = std::fmod(ppq, 2.0) < 1.4 ? 1.0f : 0.0f;
            p[ParamId::padX] = static_cast<float>(std::fmod(ppq * 0.37, 1.0));
            p[ParamId::padY] = static_cast<float>(std::fmod(ppq * 0.53, 1.0));
            eng.process(p, {kBpm, ppq, true}, midi, l.data(), r.data(), n);

            std::multiset<std::tuple<int, int, int>> fromMidi, fromTelemetry;
            for (int i = 0; i < midi.count; ++i) {
                const auto& ev = midi.events[static_cast<std::size_t>(i)];
                if (ev.isNoteOn())
                    fromMidi.insert({ev.noteNumber(), ev.sampleOffset, static_cast<int>(ev.data[2])});
            }
            const auto sentNotes = eng.sentNotes();
            for (const SentNote& s : sentNotes) {
                fromTelemetry.insert({s.note, s.sampleOffset, s.velocity});
                CHECK(s.beatInBar >= 0.0f);
                CHECK(s.beatInBar <= 4.0f);
                CHECK(s.ppq == Approx(ppq + static_cast<double>(s.sampleOffset) * bps));
                if (slicer) {
                    CHECK(s.lane == -1);
                } else {
                    REQUIRE(s.lane >= 0);
                    REQUIRE(s.lane < kDrumLaneCount);
                    CHECK(s.note == kKitNoteMaps[0][static_cast<std::size_t>(s.lane)]);
                }
            }
            INFO("block " << blk << " at beat " << ppq);
            CHECK(fromMidi == fromTelemetry);
            total += sentNotes.size();
            ppq += bps * static_cast<double>(n);
        }
        CHECK(total > 100);
    }
}

TEST_CASE("A sent note's place in the bar is on the pattern's clock", "[engine][pads]") {
    // The default beat's kick is on beats 0 and 2 of each bar. At 2x (the Rate pad) a bar lasts two host beats.
    for (const auto& [x, rate] : {std::pair{0.5f, 1.0f}, std::pair{0.9f, 2.0f}}) {
        INFO("rate " << rate);
        Engine eng;
        eng.prepare(kSr, kBlock);
        std::vector<float> l(kBlock), r(kBlock);
        MidiEventBuffer midi;
        const double bps = kBpm / 60.0 / kSr;
        std::vector<float> kicks;
        for (double ppq = 0.0; ppq < 8.0 - bps * kBlock; ppq += bps * kBlock) { // two host bars, not beat 8
            eng.process(padParams(PadProgram::rate, x, 0.5f), {kBpm, ppq, true}, midi, l.data(), r.data(), kBlock);
            for (const SentNote& s : eng.sentNotes()) {
                if (s.note != 36)
                    continue;
                kicks.push_back(s.beatInBar);
                CHECK(s.rate == Approx(rate));
            }
        }
        REQUIRE(kicks.size() == static_cast<std::size_t>(4.0f * rate));
        for (std::size_t i = 0; i < kicks.size(); ++i)
            CHECK(kicks[i] == Approx(i % 2 == 0 ? 0.0f : 2.0f).margin(1.0e-4f));
    }
}

TEST_CASE("SentNoteRing hands notes over in order, numbers them and drops what does not fit", "[pads]") {
    auto ring = std::make_unique<SentNoteRing>();
    for (int i = 0; i < 10; ++i) {
        SentNote n;
        n.note = static_cast<std::int8_t>(i);
        CHECK(ring->push(n));
    }
    std::array<SentNote, 4> out{};
    REQUIRE(ring->drain(out.data(), 4) == 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(out[static_cast<std::size_t>(i)].note == i);
        CHECK(out[static_cast<std::size_t>(i)].seq == static_cast<std::uint32_t>(i + 1));
    }
    std::vector<SentNote> rest(16);
    CHECK(ring->drain(rest.data(), 16) == 6);
    CHECK(rest[5].seq == 10u);

    for (std::uint32_t i = 0; i < SentNoteRing::kCapacity; ++i)
        CHECK(ring->push(SentNote{}));
    CHECK_FALSE(ring->push(SentNote{})); // full: the UI has not drained
    ring->discard();
    CHECK(ring->drain(rest.data(), 16) == 0);
    CHECK(ring->push(SentNote{}));
}

TEST_CASE("releaseAll sends a note-off for every sounding note and does not catch up afterwards", "[engine][pads]") {
    const double bps = kBpm / 60.0 / kSr;
    const double block = bps * static_cast<double>(kBlock);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;

    SECTION("Note-offs for what is sounding") {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.process(Params{}, {kBpm, 0.0, true}, midi, l.data(), r.data(), kBlock); // kick and hat on beat 0
        REQUIRE(eng.activeNotesLowMask() != 0);
        eng.releaseAll(midi);
        std::multiset<int> offs;
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            CHECK(ev.isNoteOff());
            CHECK(ev.sampleOffset == 0);
            offs.insert(ev.noteNumber());
        }
        CHECK(offs == std::multiset<int>{36, 42});
        CHECK(eng.activeNotesLowMask() == 0);
        CHECK(eng.sentNotes().empty());
    }
    SECTION("The bypassed block's notes are not sent late") {
        // Bypassed for the one block that holds the hat on beat 0.5, then processing again on the next block.
        Engine eng;
        eng.prepare(kSr, kBlock);
        const auto bypassed = static_cast<std::int64_t>(std::floor(0.5 / block));
        for (std::int64_t blk = 0; blk < bypassed; ++blk)
            eng.process(Params{}, {kBpm, static_cast<double>(blk) * block, true}, midi, l.data(), r.data(), kBlock);
        eng.releaseAll(midi);
        eng.process(Params{}, {kBpm, static_cast<double>(bypassed + 1) * block, true}, midi, l.data(), r.data(),
                    kBlock);
        for (int i = 0; i < midi.count; ++i)
            CHECK_FALSE(midi.events[static_cast<std::size_t>(i)].isNoteOn());
    }
}
