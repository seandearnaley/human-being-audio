// SPDX-License-Identifier: AGPL-3.0-or-later
// The engine against resolveBar: truth, determinism, fill schedule, rate and position.
#include "cl/engine.h"
#include "cl/groove_state.h"
#include "cl/hits.h"
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace cl;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;

struct NoteOn {
    int note = 0;
    std::int64_t sample = 0;
    int velocity = 0;
    bool operator==(const NoteOn&) const = default;
};

struct Run {
    std::vector<NoteOn> noteOns;
    std::vector<std::uint8_t> bytes; // every event: offset, size, data
    int noteOffs = 0;
    double endBeat = 0.0;
};

int blocksFor(double hostBars, double bpm) {
    return static_cast<int>(std::ceil(hostBars * 4.0 / (bpm / 60.0 / kSr) / static_cast<double>(kBlock)));
}

// Plays `blocks` blocks from ppq 0. `paramsAt(ppq)` supplies the parameters for the block starting at ppq.
template <typename ParamsAt> Run play(Engine& eng, double bpm, int blocks, ParamsAt&& paramsAt) {
    Run out;
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bps = bpm / 60.0 / kSr;
    double ppq = 0.0;
    for (int blk = 0; blk < blocks; ++blk) {
        const Params p = paramsAt(ppq);
        eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            const std::int64_t at = static_cast<std::int64_t>(blk) * kBlock + ev.sampleOffset;
            if (ev.isNoteOn())
                out.noteOns.push_back({ev.noteNumber(), at, static_cast<int>(ev.data[2])});
            else if (ev.isNoteOff())
                ++out.noteOffs;
            out.bytes.push_back(static_cast<std::uint8_t>(ev.sampleOffset));
            out.bytes.push_back(ev.size);
            out.bytes.insert(out.bytes.end(), ev.data, ev.data + 3);
        }
        ppq += bps * static_cast<double>(kBlock);
    }
    out.endBeat = ppq;
    return out;
}

// A seeded generator for test patterns only. The engine itself has no randomness.
struct TestRng {
    std::uint32_t state;
    int range(int lo, int hi) {
        state = state * 1664525u + 1013904223u;
        return lo + static_cast<int>((state >> 8) % static_cast<std::uint32_t>(hi - lo + 1));
    }
    bool chance(int percent) { return range(0, 99) < percent; }
};

void randomStep(TestRng& rng, Step& s, int onPercent) {
    if (!rng.chance(onPercent))
        return;
    s.velocity = static_cast<std::uint8_t>(rng.range(1, 127));
    s.layer = static_cast<Layer>(rng.range(0, 2));
    s.ratchet = static_cast<std::uint8_t>(rng.chance(15) ? rng.range(2, 4) : 1);
    s.nudge = static_cast<std::int8_t>(rng.chance(15) ? rng.range(-24, 24) : 0);
    s.flam = rng.chance(10);
}

Pattern randomPattern(TestRng& rng) {
    Pattern p{};
    p.bars = static_cast<std::uint8_t>(1 << rng.range(0, 2));
    p.fill.beats = static_cast<std::uint8_t>(1 << rng.range(0, 2));
    for (auto& lane : p.lanes) {
        for (int step = 0; step < patternSteps(p); ++step)
            randomStep(rng, lane.steps[static_cast<std::size_t>(step)], 22);
        lane.length = static_cast<std::uint8_t>(rng.chance(20) ? rng.range(3, patternSteps(p)) : 0);
        lane.offsetMs = static_cast<std::int8_t>(rng.chance(30) ? rng.range(-20, 20) : 0);
        lane.muted = rng.chance(8);
    }
    for (auto& row : p.fill.steps) {
        if (!rng.chance(50))
            continue; // this lane draws nothing in the fill and keeps playing the main pattern
        for (auto& s : row)
            randomStep(rng, s, 30);
    }
    return p;
}

struct Expected {
    int lane = 0;
    std::int64_t sample = 0;
    int velocity = 0;
    int bar = 0;
    int step = 0;
    bool grace = false;
};

// What resolveBar and forEachHit say must sound between ppq 0 and `endBeat`, with no pads and a steady fillNow.
std::vector<Expected> expectedNoteOns(const Pattern& pattern, const Params& p, double bpm, double endBeat) {
    std::vector<Expected> out;
    const double rate = kSeqRateMultipliers[static_cast<std::size_t>(p.choice(ParamId::seqRate))];
    const double bps = bpm / 60.0 / kSr;
    const HitTiming timing{static_cast<double>(p[ParamId::swing]), static_cast<double>(p[ParamId::gateLen]),
                           bpm / 60000.0 * rate};
    const int every = kFillEveryBars[static_cast<std::size_t>(p.choice(ParamId::fillEvery))];
    const int bars = patternBars(pattern);
    const auto lastBar = static_cast<std::int64_t>(std::floor(endBeat * rate / 4.0)) + 1;
    for (std::int64_t absBar = -1; absBar <= lastBar; ++absBar) {
        ResolvedBar bar;
        const int inPattern = static_cast<int>(((absBar % bars) + bars) % bars);
        const bool fill = fillScheduled(absBar, every) || p.on(ParamId::fillNow);
        resolveBar(pattern, inPattern, {p[ParamId::density], p[ParamId::dynamics], fill}, bar);
        forEachHit(pattern, bar, static_cast<double>(absBar) * 4.0, timing, [&](const Hit& h) {
            const double host = h.beat / rate;
            if (host < -1.0e-9 || host >= endBeat - 1.0e-9)
                return;
            out.push_back({h.lane, static_cast<std::int64_t>(std::floor(host / bps + 1.0e-7)), h.velocity,
                           static_cast<int>(absBar), h.step, h.grace});
        });
    }
    return out;
}

// Every expected hit is matched by exactly one emitted note-on on the same lane, at the same sample
// (give or take one, for block-edge rounding) and with the same velocity; and nothing else is emitted.
void requireSameNoteOns(const std::vector<Expected>& expected, const std::vector<NoteOn>& actual, int firstNote) {
    REQUIRE(actual.size() == expected.size());
    std::vector<bool> used(actual.size(), false);
    for (const auto& e : expected) {
        bool found = false;
        for (std::size_t i = 0; i < actual.size() && !found; ++i) {
            if (used[i] || actual[i].note - firstNote != e.lane || actual[i].velocity != e.velocity)
                continue;
            if (std::llabs(actual[i].sample - e.sample) > 1)
                continue;
            used[i] = true;
            found = true;
        }
        INFO("lane " << e.lane << " bar " << e.bar << " step " << e.step << (e.grace ? " (grace)" : "") << " sample "
                     << e.sample << " velocity " << e.velocity);
        REQUIRE(found);
    }
}

// Chromatic kit map: lane n plays note 36 + n, so a note-on names its lane.
Params truthParams() {
    Params p;
    p[ParamId::noteMap] = static_cast<float>(NoteMap::chromatic);
    p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
    return p;
}

// Kick on every 16th in the main pattern. The fill (last 2 beats) draws one kick and two snares.
// The hat draws nothing in the fill, so it keeps playing.
Pattern fillPattern() {
    Pattern p{};
    for (int step = 0; step < kStepsPerBar; ++step) {
        p.lanes[0].steps[static_cast<std::size_t>(step)].velocity = 96;
        p.lanes[0].steps[static_cast<std::size_t>(step)].layer = Layer::core;
        if (step % 2 == 0) {
            p.lanes[4].steps[static_cast<std::size_t>(step)].velocity = 96;
            p.lanes[4].steps[static_cast<std::size_t>(step)].layer = Layer::core;
        }
    }
    p.fill.beats = 2;
    p.fill.steps[0][8].velocity = 96;
    p.fill.steps[1][12].velocity = 96;
    p.fill.steps[1][14].velocity = 96;
    return p;
}

int countIn(const Run& run, int note, int bar, double bpm) {
    const double bps = bpm / 60.0 / kSr;
    int n = 0;
    for (const auto& on : run.noteOns) {
        // One sample late on purpose: a hit on a bar line is rounded down to the sample before it.
        const int at = static_cast<int>(std::floor((static_cast<double>(on.sample) + 1.0) * bps / 4.0));
        n += (on.note == note && at == bar) ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("Truth: every note-on the engine emits is a cell resolveBar reports as sounding", "[engine][truth]") {
    for (std::uint32_t seed = 1; seed <= 24; ++seed) {
        TestRng rng{seed * 2654435761u};
        const int scene = rng.range(0, kSceneCount - 1);
        GrooveBank bank;
        bank.scenes[static_cast<std::size_t>(scene)] = randomPattern(rng);
        const GrooveState groove(bank);
        const Pattern& pattern = groove.bank.scenes[static_cast<std::size_t>(scene)];

        Params p = truthParams();
        p[ParamId::scene] = static_cast<float>(scene);
        p[ParamId::density] = static_cast<float>(rng.range(0, 20)) / 20.0f;
        p[ParamId::dynamics] = static_cast<float>(rng.range(0, 20)) / 20.0f;
        p[ParamId::swing] = static_cast<float>(rng.range(0, 20)) / 20.0f;
        p[ParamId::gateLen] = static_cast<float>(rng.range(0, 20)) / 20.0f;
        p[ParamId::fillEvery] = static_cast<float>(rng.range(0, 4));
        p[ParamId::fillNow] = rng.chance(20) ? 1.0f : 0.0f;
        p[ParamId::seqRate] = static_cast<float>(rng.range(0, 2));
        const double bpm = std::vector<double>{87.0, 120.0, 140.0, 172.0}[static_cast<std::size_t>(rng.range(0, 3))];

        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.setGroove(groove);
        const Run run = play(eng, bpm, blocksFor(16.0, bpm), [&](double) { return p; });

        INFO("seed " << seed << " scene " << scene << " bars " << patternBars(pattern) << " bpm " << bpm);
        const auto expected = expectedNoteOns(pattern, p, bpm, run.endBeat);
        REQUIRE(expected.size() > 0);
        requireSameNoteOns(expected, run.noteOns, 36);
    }
}

TEST_CASE("Truth holds for every factory-style density of the default beat", "[engine][truth]") {
    const GrooveState groove;
    for (const float density : {0.0f, 0.5f, 1.0f}) {
        Params p = truthParams();
        p[ParamId::density] = density;
        Engine eng;
        eng.prepare(kSr, kBlock);
        const Run run = play(eng, 172.0, blocksFor(4.0, 172.0), [&](double) { return p; });
        requireSameNoteOns(expectedNoteOns(groove.bank.scenes[0], p, 172.0, run.endBeat), run.noteOns, 36);
    }
}

TEST_CASE("Determinism: the same inputs give byte-identical MIDI, also after reset", "[engine][truth]") {
    TestRng rng{20261126u};
    GrooveBank bank;
    bank.scenes[0] = randomPattern(rng);
    bank.scenes[1] = randomPattern(rng);
    const GrooveState groove(bank);

    // Scene changes, a fill schedule and a Repeat pad hold, all at fixed times.
    const auto paramsAt = [](double ppq) {
        Params p;
        p[ParamId::swing] = 0.4f;
        p[ParamId::fillEvery] = 1.0f;
        p[ParamId::sceneQuantize] = static_cast<float>(Quantize::sixteenth);
        p[ParamId::scene] = ppq >= 16.0 ? 1.0f : 0.0f;
        p[ParamId::padTouch] = (ppq >= 9.0 && ppq < 13.0) ? 1.0f : 0.0f;
        p[ParamId::padX] = 0.6f;
        return p;
    };

    Engine a;
    Engine b;
    a.prepare(kSr, kBlock);
    b.prepare(kSr, kBlock);
    a.setGroove(groove);
    b.setGroove(groove);
    const int blocks = blocksFor(12.0, 140.0);
    const Run first = play(a, 140.0, blocks, paramsAt);
    const Run other = play(b, 140.0, blocks, paramsAt);
    REQUIRE(first.bytes.size() > 100);
    CHECK(first.bytes == other.bytes);

    a.reset();
    const Run again = play(a, 140.0, blocks, paramsAt);
    CHECK(first.bytes == again.bytes);
}

TEST_CASE("A fill fires only in bars where (bar + 1) is a multiple of Fill, and replaces only its window",
          "[engine][fill]") {
    GrooveBank bank;
    bank.scenes[0] = fillPattern();
    Params p;
    p[ParamId::fillEvery] = 2.0f; // every 4 bars
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    const double bpm = 132.0;
    const Run run = play(eng, bpm, blocksFor(8.0, bpm), [&](double) { return p; });
    for (int bar = 0; bar < 8; ++bar) {
        INFO("bar " << bar);
        const bool fill = bar == 3 || bar == 7;
        CHECK(countIn(run, 36, bar, bpm) == (fill ? 9 : 16)); // steps 0..7 of the main pattern, then one fill kick
        CHECK(countIn(run, 38, bar, bpm) == (fill ? 2 : 0));
        CHECK(countIn(run, 42, bar, bpm) == 8); // the hat draws nothing in the fill and plays through it
    }
}

TEST_CASE("fillNow forces the fill, and is latched once per bar", "[engine][fill]") {
    GrooveBank bank;
    bank.scenes[0] = fillPattern();
    const GrooveState groove(bank);
    const double bpm = 132.0;
    const auto playWith = [&](double from, double to) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.setGroove(groove);
        return play(eng, bpm, blocksFor(3.0, bpm), [&](double ppq) {
            Params p;
            p[ParamId::fillNow] = (ppq >= from && ppq < to) ? 1.0f : 0.0f;
            return p;
        });
    };

    // On for the whole run: every bar plays its fill.
    const Run always = playWith(0.0, 1000.0);
    for (int bar = 0; bar < 3; ++bar)
        CHECK(countIn(always, 38, bar, bpm) == 2);

    // On from beat 1: in time for bar 0's fill window, which starts at beat 2.
    const Run early = playWith(1.0, 1000.0);
    CHECK(countIn(early, 38, 0, bpm) == 2);

    // On from beat 3: bar 0's window has already started, so it waits for bar 1.
    const Run late = playWith(3.0, 1000.0);
    CHECK(countIn(late, 38, 0, bpm) == 0);
    CHECK(countIn(late, 36, 0, bpm) == 16);
    CHECK(countIn(late, 38, 1, bpm) == 2);

    // On until beat 3: bar 0 was latched on and plays its whole fill; bar 1 has none.
    const Run released = playWith(0.0, 3.0);
    CHECK(countIn(released, 38, 0, bpm) == 2);
    CHECK(countIn(released, 36, 0, bpm) == 9);
    CHECK(countIn(released, 38, 1, bpm) == 0);
}

TEST_CASE("Rate plays the pattern at half, normal and double speed", "[engine]") {
    const GrooveState groove;
    std::size_t counts[3]{};
    for (int rate = 0; rate < 3; ++rate) {
        Params p = truthParams();
        p[ParamId::seqRate] = static_cast<float>(rate);
        Engine eng;
        eng.prepare(kSr, kBlock);
        const Run run = play(eng, 140.0, blocksFor(4.0, 140.0), [&](double) { return p; });
        requireSameNoteOns(expectedNoteOns(groove.bank.scenes[0], p, 140.0, run.endBeat), run.noteOns, 36);
        counts[rate] = run.noteOns.size();
    }
    CHECK(counts[1] > counts[0] + 10);
    CHECK(counts[2] > counts[1] + 20);
}

TEST_CASE("Stopping the transport leaves no note sounding", "[engine]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    const Run run = play(eng, 172.0, blocksFor(2.0, 172.0), [](double) { return Params{}; });
    REQUIRE(run.noteOns.size() > 10);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    eng.process(Params{}, {172.0, run.endBeat, false}, midi, l.data(), r.data(), kBlock);
    CHECK(eng.activeNotesLowMask() == 0);
    CHECK(eng.activeNotesHighMask() == 0);
    for (const float v : l)
        REQUIRE(std::isfinite(v));
}

TEST_CASE("The engine reports where it is in the pattern", "[engine]") {
    GrooveBank bank;
    bank.scenes[0] = fillPattern();
    setPatternBars(bank.scenes[0], 2);
    Params p;
    p[ParamId::fillEvery] = 1.0f; // every 2 bars: bars 1, 3, 5...
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;

    eng.process(p, {120.0, 5.3, true}, midi, l.data(), r.data(), kBlock); // bar 1, beat 1.3
    PlayPosition pos = eng.position();
    CHECK(pos.absBar == 1);
    CHECK(pos.barInPattern == 1);
    CHECK(pos.stepInBar == 5);
    CHECK(pos.fillLive);
    CHECK(pos.repeatStartStep == -1);

    eng.process(p, {120.0, 8.0, true}, midi, l.data(), r.data(), kBlock); // bar 2 is bar 0 of the pattern
    pos = eng.position();
    CHECK(pos.absBar == 2);
    CHECK(pos.barInPattern == 0);
    CHECK(pos.stepInBar == 0);
    CHECK_FALSE(pos.fillLive);
}
