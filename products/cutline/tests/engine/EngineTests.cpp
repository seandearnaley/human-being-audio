// SPDX-License-Identifier: AGPL-3.0-or-later
// The engine against resolveBar: truth, determinism, fill schedule, rate and position.
#include "cl/engine.h"
#include "cl/groove_state.h"
#include "cl/hits.h"
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <utility>
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
    std::vector<NoteOn> noteOffEvents; // note-offs of notes that were on, velocity 0
    std::vector<std::uint8_t> bytes;   // every event: offset, size, data
    int noteOffs = 0;
    double endBeat = 0.0;
    std::array<bool, 128> on{}; // notes sounding when the run ended
};

int blocksFor(double hostBars, double bpm, int blockSize = kBlock) {
    return static_cast<int>(std::ceil(hostBars * 4.0 / (bpm / 60.0 / kSr) / static_cast<double>(blockSize)));
}

// Applies one block's MIDI to `out`. `firstSample` is the block's first sample, counted from the run's start.
// A note-on of a note that is already on is malformed MIDI (a retrigger must send the note-off first).
void take(Run& out, const MidiEventBuffer& midi, std::int64_t firstSample) {
    for (int i = 0; i < midi.count; ++i) {
        const auto& ev = midi.events[static_cast<std::size_t>(i)];
        const std::int64_t at = firstSample + ev.sampleOffset;
        const auto note = static_cast<std::size_t>(ev.noteNumber());
        if (ev.isNoteOn()) {
            INFO("note " << note << " at sample " << at);
            REQUIRE_FALSE(out.on[note]);
            out.on[note] = true;
            out.noteOns.push_back({ev.noteNumber(), at, static_cast<int>(ev.data[2])});
        } else if (ev.isNoteOff() && out.on[note]) {
            out.on[note] = false;
            ++out.noteOffs;
            out.noteOffEvents.push_back({ev.noteNumber(), at, 0});
        }
        out.bytes.push_back(static_cast<std::uint8_t>(ev.sampleOffset));
        out.bytes.push_back(ev.size);
        out.bytes.insert(out.bytes.end(), ev.data, ev.data + 3);
    }
}

// Plays `blocks` blocks from `startPpq`, of `blockSize` samples or, when `sizeCycle` is not empty, of the sizes it
// cycles through. `paramsAt(ppq)` supplies the parameters for the block starting at ppq. Samples are counted from
// the run's start.
template <typename ParamsAt>
Run play(Engine& eng, double bpm, int blocks, ParamsAt&& paramsAt, int blockSize = kBlock, double startPpq = 0.0,
         const std::vector<int>& sizeCycle = {}) {
    Run out;
    int maxSize = blockSize;
    for (const int n : sizeCycle)
        maxSize = std::max(maxSize, n);
    std::vector<float> l(static_cast<std::size_t>(maxSize)), r(static_cast<std::size_t>(maxSize));
    MidiEventBuffer midi;
    const double bps = bpm / 60.0 / kSr;
    double ppq = startPpq;
    std::int64_t sample = 0;
    for (int blk = 0; blk < blocks; ++blk) {
        const int n = sizeCycle.empty() ? blockSize : sizeCycle[static_cast<std::size_t>(blk) % sizeCycle.size()];
        const Params p = paramsAt(ppq);
        eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), n);
        take(out, midi, sample);
        sample += n;
        ppq += bps * static_cast<double>(n);
    }
    out.endBeat = ppq;
    return out;
}

// Stops the transport after `run`, which must release every note.
void stopAndCheckReleased(Engine& eng, Run& run, double bpm, std::int64_t atSample) {
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    eng.process(Params{}, {bpm, run.endBeat, false}, midi, l.data(), r.data(), kBlock);
    take(run, midi, atSample);
    for (std::size_t note = 0; note < run.on.size(); ++note) {
        INFO("note " << note << " still on after stop");
        CHECK_FALSE(run.on[note]);
    }
    CHECK(eng.activeNotesLowMask() == 0);
    CHECK(eng.activeNotesHighMask() == 0);
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

// What resolveBar and forEachHit say must sound between `startPpq` and `endBeat`, with no pads and a steady fillNow.
// A hit at pattern beat t sounds at host beat t / rate + Shift. `keep(gridBeat)` filters by the window-clock grid time
// of the step that produced the hit. Samples are counted from `startPpq`.
template <typename Keep>
void appendExpected(const Pattern& pattern, const Params& p, double bpm, double endBeat, double startPpq, Keep&& keep,
                    std::vector<Expected>& out) {
    const double rate = kSeqRateMultipliers[static_cast<std::size_t>(p.choice(ParamId::seqRate))];
    const double bps = bpm / 60.0 / kSr;
    const double shift = static_cast<double>(p[ParamId::shiftMs]) * 1.0e-3 * (bpm / 60.0);
    const HitTiming timing{static_cast<double>(p[ParamId::swing]), static_cast<double>(p[ParamId::gateLen]),
                           bpm / 60000.0 * rate};
    const int every = kFillEveryBars[static_cast<std::size_t>(p.choice(ParamId::fillEvery))];
    const int bars = patternBars(pattern);
    const auto firstBar = static_cast<std::int64_t>(std::floor((startPpq - shift) * rate / 4.0)) - 1;
    const auto lastBar = static_cast<std::int64_t>(std::floor((endBeat - shift) * rate / 4.0)) + 1;
    for (std::int64_t absBar = firstBar; absBar <= lastBar; ++absBar) {
        ResolvedBar bar;
        const int inPattern = static_cast<int>(((absBar % bars) + bars) % bars);
        const bool fill = fillScheduled(absBar, every) || p.on(ParamId::fillNow);
        resolveBar(pattern, inPattern, {p[ParamId::density], p[ParamId::dynamics], fill}, bar);
        forEachHit(pattern, bar, static_cast<double>(absBar) * 4.0, timing, [&](const Hit& h) {
            const double grid = (static_cast<double>(absBar) * 4.0 + static_cast<double>(h.step) * 0.25) / rate;
            if (!keep(grid))
                return;
            const double host = h.beat / rate + shift;
            if (host < startPpq - 1.0e-9 || host >= endBeat - 1.0e-9)
                return;
            out.push_back({h.lane, static_cast<std::int64_t>(std::floor((host - startPpq) / bps + 1.0e-7)), h.velocity,
                           static_cast<int>(absBar), h.step, h.grace});
        });
    }
}

std::vector<Expected> expectedNoteOns(const Pattern& pattern, const Params& p, double bpm, double endBeat,
                                      double startPpq = 0.0) {
    std::vector<Expected> out;
    appendExpected(pattern, p, bpm, endBeat, startPpq, [](double) { return true; }, out);
    return out;
}

// The engine's rule for two hits of one note on one sample: one note-on, at the higher velocity. In the truth tests
// every lane has its own note, so this merges hits of one lane that land on the same sample (a step nudged onto its
// neighbour, say).
std::vector<Expected> mergeSameSample(std::vector<Expected> expected) {
    std::stable_sort(expected.begin(), expected.end(), [](const Expected& a, const Expected& b) {
        return a.lane != b.lane ? a.lane < b.lane : a.sample < b.sample;
    });
    std::vector<Expected> merged;
    for (const auto& e : expected) {
        if (!merged.empty() && merged.back().lane == e.lane && merged.back().sample == e.sample)
            merged.back().velocity = std::max(merged.back().velocity, e.velocity);
        else
            merged.push_back(e);
    }
    return merged;
}

// Every expected hit (after merging hits of one lane on one sample) is matched by exactly one emitted note-on on the
// same lane, at the same sample (give or take one, for block-edge rounding) and with the same velocity; and nothing
// else is emitted.
void requireSameNoteOns(const std::vector<Expected>& unmerged, const std::vector<NoteOn>& actual, int firstNote) {
    const std::vector<Expected> expected = mergeSameSample(unmerged);
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
        const auto expected = expectedNoteOns(groove.bank.scenes[0], p, 172.0, run.endBeat);
        REQUIRE(expected.size() > 0);
        requireSameNoteOns(expected, run.noteOns, 36);
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

TEST_CASE("Truth holds with large host blocks", "[engine][truth]") {
    constexpr int kLargeBlock = 2048;
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        TestRng rng{seed * 2654435761u};
        GrooveBank bank;
        bank.scenes[0] = randomPattern(rng);
        const GrooveState groove(bank);

        Params p = truthParams();
        p[ParamId::seqRate] = static_cast<float>(SeqRate::doubleTime);
        const double bpm = 172.0;

        Engine eng;
        eng.prepare(kSr, kLargeBlock);
        eng.setGroove(groove);
        const Run run = play(eng, bpm, blocksFor(8.0, bpm, kLargeBlock), [&](double) { return p; }, kLargeBlock);

        INFO("seed " << seed << " bars " << patternBars(groove.bank.scenes[0]));
        const auto expected = expectedNoteOns(groove.bank.scenes[0], p, bpm, run.endBeat);
        REQUIRE(expected.size() > 0);
        requireSameNoteOns(expected, run.noteOns, 36);
    }
}

TEST_CASE("Scene quantize applies to scene switches, at its 1 Bar default", "[engine]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    double ppq = 0.0;
    const auto advanceTo = [&](double until) {
        while (ppq + bps * static_cast<double>(kBlock) < until) {
            Params p;
            p[ParamId::scene] = ppq >= 1.1 ? 1.0f : 0.0f;
            eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), kBlock);
            ppq += bps * static_cast<double>(kBlock);
        }
    };

    advanceTo(3.9); // every block so far ends before beat 4
    REQUIRE(ppq > 1.2);
    CHECK(eng.activeScene() == 0);
    CHECK(eng.pendingScene() == 1);

    advanceTo(4.2); // blocks now pass beat 4
    CHECK(eng.activeScene() == 1);
}

namespace {

// Strike Body: kick and snare share 36, ghost and hat share 37, clap and open hat share 38.
int strikeBodyNote(int lane) {
    return kKitNoteMaps[static_cast<std::size_t>(NoteMap::strikeBody)][static_cast<std::size_t>(lane)];
}

// Every expected hit has an emitted note-on of its lane's note within one sample, at least as loud.
void requireCovered(const std::vector<Expected>& expected, const std::vector<NoteOn>& actual) {
    for (const auto& e : expected) {
        bool found = false;
        for (const auto& on : actual)
            found = found || (on.note == strikeBodyNote(e.lane) && std::llabs(on.sample - e.sample) <= 1 &&
                              on.velocity >= e.velocity);
        INFO("lane " << e.lane << " bar " << e.bar << " step " << e.step << (e.grace ? " (grace)" : "") << " sample "
                     << e.sample << " velocity " << e.velocity);
        REQUIRE(found);
    }
}

void putStep(Pattern& p, int lane, int step, int velocity) {
    Step& s = p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = Layer::core;
}

} // namespace

TEST_CASE("A flam's main hit is not cut by its grace note's note-off", "[engine]") {
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    for (const int step : {4, 12}) {
        putStep(bank.scenes[0], 1, step, 96);
        bank.scenes[0].lanes[1].steps[static_cast<std::size_t>(step)].flam = true;
    }
    // The grace note ends 40 ms after it starts and the main hit starts 20 ms after it, so both fall in one
    // block only when blocks are longer than 20 ms: 2048 samples is 42.7 ms.
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    Engine eng;
    eng.prepare(kSr, 2048);
    eng.setGroove(GrooveState(bank));
    const Run run = play(eng, bpm, blocksFor(4.0, bpm, 2048), [](double) { return Params{}; }, 2048);
    int mains = 0;
    for (const auto& on : run.noteOns) {
        if (on.note != 38 || on.velocity != 96)
            continue; // the grace hit is 40 softer
        ++mains;
        std::int64_t nextOff = -1;
        for (const auto& off : run.noteOffEvents) {
            if (off.note == 38 && off.sample > on.sample) { // a note-off at the note-on's own sample precedes it
                nextOff = off.sample;
                break;
            }
        }
        INFO("main snare at sample " << on.sample << ", next note-off at " << nextOff);
        REQUIRE(nextOff >= 0);
        CHECK(static_cast<double>(nextOff - on.sample) * bps >= 0.15);
    }
    CHECK(mains >= 6);
}

TEST_CASE("Two lanes sharing a note at the same sample give one note-on, at the higher velocity", "[engine]") {
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    putStep(bank.scenes[0], 0, 0, 100); // kick, note 36
    putStep(bank.scenes[0], 1, 0, 70);  // snare, also note 36 in the Strike Body map
    Params p;
    p[ParamId::noteMap] = static_cast<float>(NoteMap::strikeBody);
    const double bpm = 120.0;
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    Run run = play(eng, bpm, blocksFor(2.0, bpm), [&](double) { return p; });
    REQUIRE(run.noteOns.size() == 2); // one per bar
    for (const auto& on : run.noteOns) {
        CHECK(on.note == 36);
        CHECK(on.velocity == 100);
    }
    stopAndCheckReleased(eng, run, bpm, static_cast<std::int64_t>(blocksFor(2.0, bpm)) * kBlock);
}

TEST_CASE("A very large block keeps room for its note-offs", "[engine]") {
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        for (int step = 0; step < kStepsPerBar; ++step) {
            putStep(bank.scenes[0], lane, step, 60 + lane * 8);
            Step& s = bank.scenes[0].lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
            s.ratchet = static_cast<std::uint8_t>(1 + (step + lane) % 4);
            s.flam = (step + lane) % 3 == 0;
        }
    }
    Params p;
    p[ParamId::seqRate] = static_cast<float>(SeqRate::doubleTime);
    constexpr int kHuge = 32768;
    const double bpm = 172.0;
    Engine eng;
    eng.prepare(kSr, kHuge);
    eng.setGroove(GrooveState(bank));
    Run run = play(eng, bpm, 3, [&](double) { return p; }, kHuge);
    REQUIRE(run.noteOns.size() > 100);
    stopAndCheckReleased(eng, run, bpm, 3 * static_cast<std::int64_t>(kHuge));
}

TEST_CASE("Truth holds with mixed block sizes, Shift and a negative start", "[engine][truth]") {
    const std::vector<int> cycle{1, 37, 512, 4096};
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        TestRng rng{seed * 2246822519u};
        GrooveBank bank;
        bank.scenes[0] = randomPattern(rng);
        const GrooveState groove(bank);
        const double bpm = 140.0;

        struct Variant {
            const char* name;
            float shiftMs;
            double startPpq;
            bool mixed;
        };
        // The pre-roll variant's pattern starts bar 0 early: a flammed, early-nudged kick on step 0.
        GrooveBank earlyBank = bank;
        Pattern& early = earlyBank.scenes[0];
        early.lanes[0].muted = false;
        early.lanes[0].length = 0;
        early.lanes[0].offsetMs = static_cast<std::int8_t>(std::min<int>(early.lanes[0].offsetMs, 0));
        early.lanes[0].steps[0] = {};
        early.lanes[0].steps[0].velocity = 110;
        early.lanes[0].steps[0].layer = Layer::core;
        early.lanes[0].steps[0].flam = true;
        early.lanes[0].steps[0].nudge = -6;
        const GrooveState earlyGroove(earlyBank);

        for (const Variant v : {Variant{"mixed blocks", 0.0f, 0.0, true}, Variant{"shift +17 ms", 17.0f, 0.0, false},
                                Variant{"shift -23 ms", -23.0f, 0.0, true}, Variant{"start at -2", 0.0f, -2.0, true}}) {
            const GrooveState& g = v.startPpq < 0.0 ? earlyGroove : groove;
            Params p = truthParams();
            p[ParamId::swing] = 0.3f;
            p[ParamId::shiftMs] = v.shiftMs;
            Engine eng;
            eng.prepare(kSr, 4096);
            eng.setGroove(g);
            const int blocks = v.mixed ? 4 * blocksFor(6.0, bpm, 1 + 37 + 512 + 4096) : blocksFor(6.0, bpm, 512);
            const Run run = play(
                eng, bpm, blocks, [&](double) { return p; }, 512, v.startPpq, v.mixed ? cycle : std::vector<int>{});
            INFO("seed " << seed << " " << v.name);
            // Silent before beat 0 (window clock; host beat `shift`): before it only bar 0's early hits sound. A hit of
            // an earlier bar that lands at or after beat 0 plays, as it does in a run that starts at beat 0.
            const double bps = bpm / 60.0 / kSr;
            const double beatZero = static_cast<double>(v.shiftMs) * 1.0e-3 * (bpm / 60.0);
            auto expected = expectedNoteOns(g.bank.scenes[0], p, bpm, run.endBeat, v.startPpq);
            std::erase_if(expected, [&](const Expected& e) {
                return e.bar < 0 && v.startPpq + static_cast<double>(e.sample + 1) * bps <= beatZero;
            });
            REQUIRE(expected.size() > 0);
            if (v.startPpq < 0.0) {
                // Bar 0's own early kick sounds before beat 0.
                bool earlyKick = false;
                for (const auto& on : run.noteOns)
                    earlyKick = earlyKick || (on.note == 36 && static_cast<double>(on.sample) * bps < -v.startPpq);
                REQUIRE(earlyKick);
                // No note-on before beat 0 belongs to a bar before 0: nothing sounds earlier than bar 0's earliest
                // possible hit (and requireSameNoteOns matches each one to a hit of bar 0).
                const double earliest = -maxEarlyBeats(bpm / 60000.0);
                for (const auto& on : run.noteOns) {
                    INFO("note " << on.note << " at host beat " << v.startPpq + static_cast<double>(on.sample) * bps);
                    REQUIRE(v.startPpq + static_cast<double>(on.sample) * bps >= earliest - bps);
                }
            }
            requireSameNoteOns(expected, run.noteOns, 36);
        }
    }
}

TEST_CASE("A kit map with shared notes stays well-formed and covers every hit", "[engine][truth]") {
    const std::vector<int> cycle{1, 37, 512, 4096};
    for (std::uint32_t seed = 1; seed <= 8; ++seed) {
        TestRng rng{seed * 3266489917u};
        GrooveBank bank;
        bank.scenes[0] = randomPattern(rng);
        const GrooveState groove(bank);
        Params p;
        p[ParamId::noteMap] = static_cast<float>(NoteMap::strikeBody);
        p[ParamId::swing] = 0.2f;
        const double bpm = 160.0;
        Engine eng;
        eng.prepare(kSr, 4096);
        eng.setGroove(groove);
        const int blocks = 4 * blocksFor(6.0, bpm, 1 + 37 + 512 + 4096);
        Run run = play(eng, bpm, blocks, [&](double) { return p; }, kBlock, 0.0, cycle);
        INFO("seed " << seed);
        const auto expected = expectedNoteOns(groove.bank.scenes[0], p, bpm, run.endBeat);
        REQUIRE(expected.size() > 0);
        requireCovered(expected, run.noteOns);
        stopAndCheckReleased(eng, run, bpm, static_cast<std::int64_t>(blocks / 4) * (1 + 37 + 512 + 4096));
    }
}

TEST_CASE("Truth across a scene switch: each step plays from the scene active at its grid time", "[engine][truth]") {
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        for (const Quantize quant : {Quantize::oneBar, Quantize::sixteenth}) {
            for (const float shiftMs : {0.0f, 19.0f, -21.0f}) {
                TestRng rng{seed * 2654435761u + 77u};
                GrooveBank bank;
                bank.scenes[0] = randomPattern(rng);
                bank.scenes[1] = randomPattern(rng);
                Pattern& b = bank.scenes[1];
                // Scene B starts its bars early: a flammed, early-nudged kick on step 0. Its other hits are kept
                // within reach of a 1/16 switch request (at most 0.08 beat early, plus an offset of 10 ms).
                for (auto& lane : b.lanes) {
                    lane.offsetMs = static_cast<std::int8_t>(std::max<int>(lane.offsetMs, -10));
                    for (auto& st : lane.steps)
                        st.nudge = static_cast<std::int8_t>(std::max<int>(st.nudge, -8));
                }
                for (auto& row : b.fill.steps)
                    for (auto& st : row)
                        st.nudge = static_cast<std::int8_t>(std::max<int>(st.nudge, -8));
                b.lanes[0].muted = false;
                b.lanes[0].length = 0;
                for (int bar = 0; bar < patternBars(b); ++bar) {
                    Step& st = b.lanes[0].steps[static_cast<std::size_t>(bar * kStepsPerBar)];
                    st = {};
                    st.velocity = 110;
                    st.layer = Layer::core;
                    st.flam = true;
                    st.nudge = -6;
                }
                const GrooveState groove(bank);

                const double bpm = 120.0;
                const double bps = bpm / 60.0 / kSr;
                const double shift = static_cast<double>(shiftMs) * 1.0e-3 * (bpm / 60.0);
                // Ask for scene B early in a 1/16 (window clock), so a 1/16 switch is about a 1/16 away.
                const double requestAt = 7.51 + shift;
                double requested = -1.0;
                const auto paramsAt = [&](double ppq) {
                    Params p = truthParams();
                    p[ParamId::sceneQuantize] = static_cast<float>(quant);
                    p[ParamId::shiftMs] = shiftMs;
                    p[ParamId::scene] = ppq >= requestAt ? 1.0f : 0.0f;
                    if (ppq >= requestAt && requested < 0.0)
                        requested = ppq;
                    return p;
                };
                Engine eng;
                eng.prepare(kSr, 512);
                eng.setGroove(groove);
                const Run run = play(eng, bpm, blocksFor(4.0, bpm, 512), paramsAt, 512);
                REQUIRE(requested > 0.0);

                const double q = quant == Quantize::oneBar ? 4.0 : 0.25;
                const double switchBeat = std::ceil((requested - shift - 1.0e-6) / q) * q; // window clock
                Params p = paramsAt(0.0);
                std::vector<Expected> expected;
                appendExpected(
                    groove.bank.scenes[0], p, bpm, run.endBeat, 0.0,
                    [&](double grid) { return grid < switchBeat - 1.0e-9; }, expected);
                appendExpected(
                    groove.bank.scenes[1], p, bpm, run.endBeat, 0.0,
                    [&](double grid) { return grid >= switchBeat - 1.0e-9; }, expected);
                INFO("seed " << seed << " quantize " << static_cast<int>(quant) << " shift " << shiftMs
                             << " switch beat " << switchBeat << " request block at " << requested << " ("
                             << requested / bps << ")");
                requireSameNoteOns(expected, run.noteOns, 36);
            }
        }
    }
}

TEST_CASE("A fill bar keeps its whole fill when Fill is switched off inside the window", "[engine][fill]") {
    GrooveBank bank;
    bank.scenes[0] = fillPattern();
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    const double bpm = 132.0;
    const Run run = play(eng, bpm, blocksFor(3.0, bpm), [](double ppq) {
        Params p;
        p[ParamId::fillEvery] = ppq < 6.5 ? 1.0f : 0.0f; // every 2 bars until beat 2.5 of bar 1, then Off
        return p;
    });
    CHECK(countIn(run, 38, 1, bpm) == 2);
    CHECK(countIn(run, 36, 1, bpm) == 9);
    CHECK(countIn(run, 38, 2, bpm) == 0);
}

TEST_CASE("With Shift, the engine reports the step that is sounding", "[engine]") {
    // Shift +20 ms at 120 BPM is 0.04 beat: at transport beat 8.02 the pattern sounding is at window beat 7.98,
    // bar 1, step 15.
    Params p;
    p[ParamId::shiftMs] = 20.0f;
    Engine eng;
    eng.prepare(kSr, kBlock);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    eng.process(p, {120.0, 8.02, true}, midi, l.data(), r.data(), kBlock);
    const PlayPosition pos = eng.position();
    CHECK(pos.absBar == 1);
    CHECK(pos.stepInBar == 15);
}

TEST_CASE("A pending scene switch is re-quantized after the transport jumps back", "[engine]") {
    // Scene B is asked for at beat 5 (1 Bar quantize: due at 8). The host loops back to beat 1 at beat 6, so the
    // switch is due at the next bar line after the jump, beat 4.
    Engine eng;
    eng.prepare(kSr, kBlock);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    bool requested = false;
    double ppq = 0.0;
    const auto playUntil = [&](double until) {
        while (ppq + bps * static_cast<double>(kBlock) < until) {
            requested = requested || ppq >= 5.0;
            Params p;
            p[ParamId::scene] = requested ? 1.0f : 0.0f;
            eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), kBlock);
            ppq += bps * static_cast<double>(kBlock);
        }
    };
    playUntil(6.0);
    REQUIRE(eng.pendingScene() == 1);
    ppq = 1.0; // the jump
    playUntil(3.9);
    CHECK(eng.activeScene() == 0);
    CHECK(eng.pendingScene() == 1);
    playUntil(4.2);
    CHECK(eng.activeScene() == 1);
}

TEST_CASE("A fill latched at one Rate is not replayed after Rate changes", "[engine][fill]") {
    // fillNow is held only while bar 1 plays at double time (host beats 2-4), so that bar is latched on. Rate drops
    // to normal at host beat 4.5, inside host bar 1, which is now pattern bar 1 again. Fill is Off and fillNow is
    // off, so host bar 1 must not play the fill that was latched for the other bar 1.
    GrooveBank bank;
    Pattern p{};
    for (int step = 0; step < kStepsPerBar; ++step)
        putStep(p, 0, step, 96);
    p.fill.beats = 2;
    p.fill.steps[1][12].velocity = 96;
    p.fill.steps[1][14].velocity = 96;
    bank.scenes[0] = p;
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    int fillLiveBlocks = 0;
    const Run run = play(eng, bpm, blocksFor(2.0, bpm), [&](double ppq) {
        Params prm = truthParams();
        prm[ParamId::seqRate] = static_cast<float>(ppq < 4.5 ? SeqRate::doubleTime : SeqRate::normal);
        prm[ParamId::fillNow] = (ppq >= 2.0 && ppq < 3.0) ? 1.0f : 0.0f;
        if (ppq >= 4.5)
            fillLiveBlocks += eng.position().fillLive ? 1 : 0;
        return prm;
    });
    int snaresAfter = 0;
    for (const auto& on : run.noteOns)
        snaresAfter += (on.note == 37 && static_cast<double>(on.sample) * bps >= 4.5) ? 1 : 0;
    CHECK(snaresAfter == 0);
    CHECK(fillLiveBlocks == 0);
}

TEST_CASE("A step nudged onto its neighbour on the same lane gives one note-on, at the higher velocity",
          "[engine][truth]") {
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    putStep(bank.scenes[0], 1, 4, 96);
    putStep(bank.scenes[0], 1, 5, 60);
    bank.scenes[0].lanes[1].steps[5].nudge = -24; // a full 16th early: onto step 4
    const GrooveState groove(bank);
    const Params p = truthParams();
    const double bpm = 120.0;
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(groove);
    Run run = play(eng, bpm, blocksFor(2.0, bpm), [&](double) { return p; });
    REQUIRE(run.noteOns.size() == 2); // one per bar
    for (const auto& on : run.noteOns) {
        CHECK(on.note == 37);
        CHECK(on.velocity == 96);
    }
    requireSameNoteOns(expectedNoteOns(groove.bank.scenes[0], p, bpm, run.endBeat), run.noteOns, 36);
    stopAndCheckReleased(eng, run, bpm, static_cast<std::int64_t>(blocksFor(2.0, bpm)) * kBlock);
}

namespace {

// Plays the default beat while the tempo ramps by `slope` BPM per second, bouncing between 60 and 240 BPM. The host
// position integrates the tempo exactly and reports the tempo at each block's start. Each emitted note-on is placed
// on the 16th it sounds on; every step the pattern plays before `untilBeat` must be emitted exactly once. Shift is
// `shiftMs`, plus `sweepMs` times a slow sine when Shift is automated.
void requireEveryStepOnceUnderRamp(int block, double slope, double untilBeat, double shiftMs = 0.0,
                                   double sweepMs = 0.0) {
    const GrooveState groove;
    const Pattern& pattern = groove.bank.scenes[0];
    Params p = truthParams();
    Engine eng;
    eng.prepare(kSr, block);
    eng.setGroove(groove);
    std::vector<float> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
    MidiEventBuffer midi;
    Run run;
    double ppq = 0.0;
    double bpmStart = 100.0;
    double t = 0.0;
    std::int64_t sample = 0;
    std::vector<std::pair<int, std::int64_t>> emitted; // lane, absolute 16th
    double firstWindowStart = 0.0; // the first block's window, in pattern beats: the run covers from here
    while (ppq < untilBeat) {
        const double dt = static_cast<double>(block) / kSr;
        double bpm0 = bpmStart + slope * t;
        if (bpm0 < 60.0 || bpm0 > 240.0) {
            slope = -slope;
            bpmStart = bpm0 - slope * t;
        }
        bpm0 = bpmStart + slope * t;
        const double bpm1 = bpmStart + slope * (t + dt);
        const double advance = (bpm0 + bpm1) * 0.5 / 60.0 * dt;
        const double blockShiftMs = shiftMs + sweepMs * std::sin(2.0 * t);
        p[ParamId::shiftMs] = static_cast<float>(blockShiftMs);
        if (sample == 0)
            firstWindowStart = -static_cast<double>(p[ParamId::shiftMs]) * 1.0e-3 * (bpm0 / 60.0);
        eng.process(p, {bpm0, ppq, true}, midi, l.data(), r.data(), block);
        take(run, midi, sample);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            if (!ev.isNoteOn())
                continue;
            const double beat = ppq + static_cast<double>(ev.sampleOffset) / static_cast<double>(block) * advance;
            const double shift = static_cast<double>(p[ParamId::shiftMs]) * 1.0e-3 * (bpm0 / 60.0);
            emitted.emplace_back(ev.noteNumber() - 36, std::llround((beat - shift) * 4.0));
        }
        ppq += advance;
        t += dt;
        sample += block;
    }

    // What the pattern plays: every resolved step whose 16th starts before the last block's start.
    const double lastBlockStart = ppq - 0.5; // well inside the run, clear of the last window's end
    std::vector<std::pair<int, std::int64_t>> expected;
    const HitTiming timing{0.0, static_cast<double>(p[ParamId::gateLen]), 0.12};
    for (std::int64_t bar = 0; static_cast<double>(bar) * 4.0 < lastBlockStart; ++bar) {
        ResolvedBar resolved;
        resolveBar(pattern, 0, {p[ParamId::density], p[ParamId::dynamics], false}, resolved);
        forEachHit(pattern, resolved, static_cast<double>(bar) * 4.0, timing, [&](const Hit& h) {
            if (h.beat >= firstWindowStart && h.beat < lastBlockStart)
                expected.emplace_back(h.lane, std::llround(h.beat * 4.0));
        });
    }
    std::erase_if(emitted, [&](const auto& e) { return static_cast<double>(e.second) * 0.25 >= lastBlockStart; });
    std::sort(expected.begin(), expected.end());
    std::sort(emitted.begin(), emitted.end());
    INFO("block " << block << " slope " << slope << " shift " << shiftMs << " sweep " << sweepMs);
    REQUIRE(expected.size() > 100);
    int doubled = 0;
    for (std::size_t i = 1; i < emitted.size(); ++i)
        doubled += emitted[i] == emitted[i - 1] ? 1 : 0;
    std::vector<std::pair<int, std::int64_t>> dropped;
    std::set_difference(expected.begin(), expected.end(), emitted.begin(), emitted.end(), std::back_inserter(dropped));
    CHECK(doubled == 0);
    CHECK(dropped.size() == 0u);
    CHECK(emitted.size() == expected.size());
    CHECK(emitted == expected);
}

} // namespace

TEST_CASE("Under a tempo ramp, consecutive blocks cover every pattern step exactly once", "[engine]") {
    for (const int block : {64, 512, 2048}) {
        requireEveryStepOnceUnderRamp(block, 30.0, 96.0);
        requireEveryStepOnceUnderRamp(block, 30.0, 96.0, -15.0);
    }
}

TEST_CASE("While Shift is automated, consecutive blocks cover every pattern step exactly once", "[engine]") {
    for (const int block : {64, 512, 2048})
        requireEveryStepOnceUnderRamp(block, 0.0, 96.0, 0.0, 24.0);
}

namespace {

// Applies one block's MIDI to the notes sounding, checking that it is well-formed: offsets inside the block and in
// order, no note-on of a note that is on, no note-off of a note that is off.
void takeWellFormed(std::array<bool, 128>& on, const MidiEventBuffer& midi, int numSamples) {
    int last = 0;
    for (int i = 0; i < midi.count; ++i) {
        const auto& ev = midi.events[static_cast<std::size_t>(i)];
        INFO("event " << i << " of " << midi.count << " at offset " << ev.sampleOffset << " in a block of "
                      << numSamples);
        REQUIRE(ev.sampleOffset >= 0);
        REQUIRE(ev.sampleOffset < numSamples);
        REQUIRE(ev.sampleOffset >= last);
        last = ev.sampleOffset;
        const auto note = static_cast<std::size_t>(ev.noteNumber());
        if (ev.isNoteOn()) {
            REQUIRE_FALSE(on[note]);
            on[note] = true;
        } else if (ev.isNoteOff()) {
            REQUIRE(on[note]);
            on[note] = false;
        }
    }
}

// A seeded run of everything a user and a host can do at once. The generator only drives the test.
void fuzz(std::uint32_t seed, bool slicer, int blocks) {
    TestRng rng{seed * 2654435761u + (slicer ? 99u : 0u)};
    GrooveBank bank;
    for (auto& scene : bank.scenes)
        scene = randomPattern(rng);
    Engine eng;
    eng.prepare(kSr, 4096);
    eng.setGroove(GrooveState(bank));
    Params p;
    p[ParamId::noteMap] = static_cast<float>(rng.range(0, kNoteMapCount - 1));
    if (slicer)
        p[ParamId::engineMode] = 1.0f;
    std::vector<float> l(4096), r(4096);
    MidiEventBuffer midi;
    std::array<bool, 128> on{};
    double ppq = static_cast<double>(rng.range(-8, 8));
    const double bpm = std::vector<double>{60.0, 120.0, 172.0, 240.0}[static_cast<std::size_t>(rng.range(0, 3))];
    bool playing = true;
    const std::array<int, 4> inputNotes{48, 55, 60, 63}; // 60 and 63 are also Pad MIDI's C3 and D#3
    std::array<bool, 4> inputOn{};
    int noteOns = 0;
    for (int blk = 0; blk < blocks; ++blk) {
        const int n = rng.chance(10) ? rng.range(1, 16) : rng.range(1, 4096);
        if (rng.chance(3))
            p[ParamId::padTouch] = rng.chance(50) ? 1.0f : 0.0f;
        if (rng.chance(1))
            p[ParamId::padHold] = rng.chance(30) ? 1.0f : 0.0f;
        if (rng.chance(3))
            p[ParamId::padProgram] = static_cast<float>(rng.range(0, 3));
        if (rng.chance(5))
            p[ParamId::padX] = static_cast<float>(rng.range(0, 100)) / 100.0f;
        if (rng.chance(5))
            p[ParamId::padY] = static_cast<float>(rng.range(0, 100)) / 100.0f;
        if (rng.chance(2))
            p[ParamId::scene] = static_cast<float>(rng.range(0, kSceneCount - 1));
        if (rng.chance(1))
            p[ParamId::sceneQuantize] = static_cast<float>(rng.range(0, 3));
        if (rng.chance(2))
            p[ParamId::fillNow] = rng.chance(50) ? 1.0f : 0.0f;
        if (rng.chance(1))
            p[ParamId::fillEvery] = static_cast<float>(rng.range(0, 4));
        if (rng.chance(1))
            p[ParamId::seqRate] = static_cast<float>(rng.range(0, 2));
        if (rng.chance(1))
            p[ParamId::density] = static_cast<float>(rng.range(0, 100)) / 100.0f;
        if (rng.chance(1))
            p[ParamId::swing] = static_cast<float>(rng.range(0, 100)) / 100.0f;
        if (rng.chance(2))
            p[ParamId::shiftMs] = static_cast<float>(rng.range(-25, 25));
        if (rng.chance(1))
            p[ParamId::padMidi] = rng.chance(50) ? 1.0f : 0.0f;
        if (rng.chance(1))
            p[ParamId::padCcMode] = static_cast<float>(rng.range(0, 3));
        if (rng.chance(1))
            p[ParamId::noteMap] = static_cast<float>(rng.range(0, kNoteMapCount - 1));
        if (slicer && rng.chance(1))
            p[ParamId::slicerStyle] = static_cast<float>(rng.range(0, 4));
        if (rng.chance(1) && seed % 3 == 0)
            p[ParamId::engineMode] = static_cast<float>(rng.range(0, 1));
        if (rng.chance(4)) {
            const auto i = static_cast<std::size_t>(rng.range(0, 3));
            if (inputOn[i])
                eng.inputNoteOff(inputNotes[i], ppq);
            else
                eng.inputNoteOn(inputNotes[i], rng.range(1, 127), ppq);
            inputOn[i] = !inputOn[i];
        }
        if (rng.chance(1))
            ppq = static_cast<double>(rng.range(-4, 40)) + static_cast<double>(rng.range(0, 3)) * 0.25; // a jump
        if (rng.chance(1))
            ppq = std::floor(ppq / 16.0) * 16.0; // a loop back
        if (rng.chance(1))
            playing = !playing;
        if (rng.chance(1))
            eng.setGroove(GrooveState(bank));

        eng.process(p, {bpm, ppq, playing}, midi, l.data(), r.data(), n);
        INFO("seed " << seed << (slicer ? " slicer" : " drum") << " block " << blk << " ppq " << ppq
                     << (playing ? " playing" : " stopped"));
        takeWellFormed(on, midi, n);
        for (int i = 0; i < midi.count; ++i)
            noteOns += midi.events[static_cast<std::size_t>(i)].isNoteOn() ? 1 : 0;
        if (!playing) {
            for (std::size_t note = 0; note < on.size(); ++note) {
                INFO("note " << note << " sounding while stopped");
                REQUIRE_FALSE(on[note]);
            }
            REQUIRE(eng.activeNotesLowMask() == 0);
            REQUIRE(eng.activeNotesHighMask() == 0);
        }
        if (playing)
            ppq += bpm / 60.0 / kSr * static_cast<double>(n);
    }
    eng.process(p, {bpm, ppq, false}, midi, l.data(), r.data(), 64);
    takeWellFormed(on, midi, 64);
    for (std::size_t note = 0; note < on.size(); ++note) {
        INFO("seed " << seed << " note " << note << " sounding after stop");
        CHECK_FALSE(on[note]);
    }
    CHECK(eng.activeNotesLowMask() == 0);
    CHECK(eng.activeNotesHighMask() == 0);
    CHECK(noteOns > 100);
}

} // namespace

TEST_CASE("Fuzz: drum mode stays well-formed under random controls, transport and block sizes", "[engine]") {
    for (std::uint32_t seed = 1; seed <= 6; ++seed)
        fuzz(seed, false, 2500);
}

TEST_CASE("Fuzz: MIDI Slicer mode stays well-formed under random controls, transport and block sizes", "[engine]") {
    for (std::uint32_t seed = 1; seed <= 6; ++seed)
        fuzz(seed, true, 2500);
}

TEST_CASE("Every pass of a 4-bar host loop plays the same notes", "[engine]") {
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        TestRng rng{seed * 40503u + 7u};
        GrooveBank bank;
        bank.scenes[0] = randomPattern(rng);
        Engine eng;
        eng.prepare(kSr, 512);
        eng.setGroove(GrooveState(bank));
        Params p = truthParams();
        p[ParamId::fillEvery] = 2.0f; // every 4 bars: the loop's last bar
        p[ParamId::swing] = 0.3f;
        std::vector<float> l(512), r(512);
        MidiEventBuffer midi;
        std::array<bool, 128> on{};
        const double bpm = 172.0;
        const double bps = bpm / 60.0 / kSr;
        std::vector<std::vector<NoteOn>> passes;
        for (int pass = 0; pass < 4; ++pass) {
            std::vector<NoteOn> ons;
            double ppq = 0.0;
            std::int64_t sample = 0;
            while (ppq < 16.0 - 1.0e-9) {
                const int n = std::max(1, static_cast<int>(std::min(512.0, std::ceil((16.0 - ppq) / bps))));
                eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), n);
                INFO("seed " << seed << " pass " << pass << " ppq " << ppq);
                takeWellFormed(on, midi, n);
                for (int i = 0; i < midi.count; ++i) {
                    const auto& ev = midi.events[static_cast<std::size_t>(i)];
                    if (ev.isNoteOn())
                        ons.push_back({ev.noteNumber(), sample + ev.sampleOffset, static_cast<int>(ev.data[2])});
                }
                ppq += bps * static_cast<double>(n);
                sample += n;
            }
            passes.push_back(ons);
        }
        INFO("seed " << seed);
        REQUIRE(passes[0].size() > 20);
        for (std::size_t pass = 1; pass < passes.size(); ++pass) {
            CHECK(passes[pass].size() == passes[0].size());
            CHECK(passes[pass] == passes[0]);
        }
    }
}

TEST_CASE("A hit before the block's first sample is not started on the sample where Drop silences", "[engine][pads]") {
    // Shift jumps from 0 to -25 ms (0.05 beat at 120 BPM) while Drop's silence is due at beat 1, so the next window
    // starts where the last one ended, before beat 1, and the block's first sample is already past it. The snare
    // nudged to just before beat 1 lies in that stretch: it and the silence both land on sample 0.
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    putStep(bank.scenes[0], 1, 4, 100);
    bank.scenes[0].lanes[1].steps[4].nudge = -1; // beat 0.98958
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    std::array<bool, 128> on{};
    double ppq = 0.0;
    int noteOns = 0;
    for (int blk = 0; ppq < 2.0; ++blk) {
        Params p = truthParams();
        p[ParamId::padProgram] = static_cast<float>(PadProgram::drop);
        p[ParamId::padX] = 0.2f;                         // the silence half
        p[ParamId::padTouch] = ppq >= 0.9 ? 1.0f : 0.0f; // due at beat 1
        p[ParamId::shiftMs] = ppq >= 0.989 ? -25.0f : 0.0f;
        eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        INFO("block " << blk << " ppq " << ppq);
        takeWellFormed(on, midi, kBlock);
        for (int i = 0; i < midi.count; ++i)
            noteOns += midi.events[static_cast<std::size_t>(i)].isNoteOn() ? 1 : 0;
        ppq += bps * static_cast<double>(kBlock);
    }
    CHECK(noteOns == 0);
    for (std::size_t note = 0; note < on.size(); ++note) {
        INFO("note " << note);
        CHECK_FALSE(on[note]);
    }
}

namespace {

// Plays from `fromPpq` until `toPpq` in kBlock blocks and returns the host beat of every event `pick` selects.
template <typename ParamsAt, typename Pick>
std::vector<double> eventBeats(Engine& eng, double bpm, double fromPpq, double toPpq, ParamsAt&& paramsAt,
                               Pick&& pick) {
    std::vector<double> out;
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    std::array<bool, 128> on{};
    const double bps = bpm / 60.0 / kSr;
    for (double ppq = fromPpq; ppq < toPpq; ppq += bps * static_cast<double>(kBlock)) {
        eng.process(paramsAt(ppq), {bpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        takeWellFormed(on, midi, kBlock);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            if (pick(ev))
                out.push_back(ppq + static_cast<double>(ev.sampleOffset) * bps);
        }
    }
    return out;
}

bool isCc(const MidiEvent& ev) { return (ev.data[0] & 0xF0u) == 0xB0u; }

} // namespace

TEST_CASE("Pre-roll: the default beat is silent before beat 0, then plays as a run from beat 0", "[engine]") {
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    const int preBlocks = 3000; // 8 beats of 64-sample blocks at 120 BPM
    REQUIRE(std::abs(static_cast<double>(preBlocks * kBlock) * bps - 8.0) < 1.0e-9);
    Engine pre;
    pre.prepare(kSr, kBlock);
    const Run withPreRoll =
        play(pre, bpm, preBlocks + blocksFor(4.0, bpm), [](double) { return Params{}; }, kBlock, -8.0);
    Engine fromZero;
    fromZero.prepare(kSr, kBlock);
    const Run plain = play(fromZero, bpm, blocksFor(4.0, bpm), [](double) { return Params{}; });

    const std::int64_t zero = static_cast<std::int64_t>(preBlocks) * kBlock;
    std::vector<NoteOn> ons;
    for (const auto& on : withPreRoll.noteOns) {
        INFO("note " << on.note << " at host beat " << -8.0 + static_cast<double>(on.sample) * bps);
        REQUIRE(on.sample >= zero);
        ons.push_back({on.note, on.sample - zero, on.velocity});
    }
    std::vector<NoteOn> offs;
    for (const auto& off : withPreRoll.noteOffEvents)
        offs.push_back({off.note, off.sample - zero, 0});
    REQUIRE(plain.noteOns.size() > 10);
    CHECK(ons == plain.noteOns);
    CHECK(offs == plain.noteOffEvents);
}

TEST_CASE("Pre-roll: no CC before beat 0, from XY to CC or an expression lane", "[engine]") {
    const double bpm = 120.0;
    SECTION("XY to CC with a pad held") {
        Engine eng;
        eng.prepare(kSr, kBlock);
        const auto beats = eventBeats(
            eng, bpm, -4.0, 1.0,
            [](double) {
                Params p;
                p[ParamId::padCcMode] = static_cast<float>(PadCcMode::cc74AndCc1);
                p[ParamId::padProgram] = static_cast<float>(PadProgram::rate);
                p[ParamId::padX] = 0.5f;
                p[ParamId::padTouch] = 1.0f;
                return p;
            },
            isCc);
        REQUIRE_FALSE(beats.empty());
        for (const double at : beats) {
            INFO("CC at host beat " << at);
            CHECK(at >= -1.0e-9);
        }
    }
    SECTION("An expression lane") {
        Engine eng;
        eng.prepare(kSr, kBlock);
        const auto beats = eventBeats(
            eng, bpm, -4.0, 1.0,
            [](double) {
                Params p;
                p[laneParam(0, LaneField::on)] = 1.0f;
                p[laneParam(0, LaneField::target)] = static_cast<float>(LaneTarget::cc11Expr);
                p[laneParam(0, LaneField::depth)] = 1.0f;
                return p;
            },
            isCc);
        REQUIRE_FALSE(beats.empty());
        for (const double at : beats) {
            INFO("CC at host beat " << at);
            CHECK(at >= -1.0e-9);
        }
    }
}

TEST_CASE("Pre-roll: the engine reports bar 0, step 0 and no fill", "[engine]") {
    GrooveBank bank;
    bank.scenes[0] = fillPattern();
    Params p;
    p[ParamId::fillNow] = 1.0f; // would make bar -1 a fill if bars before 0 played
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    eng.process(p, {120.0, -1.5, true}, midi, l.data(), r.data(), kBlock); // bar -1, beat 2.5: in its fill window
    const PlayPosition pos = eng.position();
    CHECK(pos.absBar == 0);
    CHECK(pos.barInPattern == 0);
    CHECK(pos.stepInBar == 0);
    CHECK_FALSE(pos.fillLive);
    CHECK(pos.repeatStartStep == -1);
}

TEST_CASE("Pre-roll with Shift: the silence ends where the shifted pattern's bar 0 starts", "[engine]") {
    // Shift of 20 ms at 120 BPM is 0.04 beat. The default beat's first kick is on step 0 of bar 0.
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    for (const float shiftMs : {20.0f, -20.0f}) {
        const double shift = static_cast<double>(shiftMs) * 1.0e-3 * (bpm / 60.0);
        Engine eng;
        eng.prepare(kSr, kBlock);
        const auto beats = eventBeats(
            eng, bpm, -4.0, 1.0,
            [&](double) {
                Params p;
                p[ParamId::shiftMs] = shiftMs;
                return p;
            },
            [](const MidiEvent& ev) { return ev.isNoteOn(); });
        INFO("shift " << shiftMs << " ms");
        REQUIRE_FALSE(beats.empty());
        for (const double at : beats) {
            INFO("note-on at host beat " << at);
            CHECK(at >= shift - 2.0 * bps);
        }
        CHECK(std::abs(beats.front() - shift) <= 2.0 * bps);
    }
}
