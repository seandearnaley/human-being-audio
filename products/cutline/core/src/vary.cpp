// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/vary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace cl {

namespace {

constexpr int kSnareLane = 1;
constexpr int kGhostLane = 2;
constexpr int kClapLane = 3;
constexpr int kHatLane = 4;
constexpr int kPercALane = 6;
constexpr int kMoves = 3;

enum class Move : std::uint8_t { shift = 0, add = 1, remove = 2, ornament = 3 };
constexpr int kMoveKinds = 4;

// One candidate move on stored steps: `from` and `to` are equal except for a shift.
struct Candidate {
    std::int8_t lane = 0;
    std::int8_t from = 0;
    std::int8_t to = 0;
};

// Shift has the most candidates: every lane, every step of the bar, both directions.
constexpr int kMaxCandidates = kDrumLaneCount * kStepsPerBar * 2;

struct CandidateList {
    std::array<Candidate, kMaxCandidates> items{};
    int count = 0;
    void push(int lane, int from, int to) noexcept {
        if (count < kMaxCandidates)
            items[static_cast<std::size_t>(count++)] = {static_cast<std::int8_t>(lane), static_cast<std::int8_t>(from),
                                                        static_cast<std::int8_t>(to)};
    }
};

using Touched = std::array<std::array<bool, kMaxPatternSteps>, kDrumLaneCount>;

const Step& stepOf(const Pattern& p, int lane, int stored) noexcept {
    return p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(stored)];
}

Step& stepOf(Pattern& p, int lane, int stored) noexcept {
    return p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(stored)];
}

bool touched(const Touched& t, int lane, int stored) noexcept {
    return t[static_cast<std::size_t>(lane)][static_cast<std::size_t>(stored)];
}

// A Groove or Detail hit: the only kind vary moves, removes or ornaments.
bool variable(const Step& s) noexcept { return s.on() && s.layer != Layer::core; }

// FNV-1a over the pattern's fields in a fixed order (not over the struct's memory, which has padding).
struct Fnv1a {
    std::uint64_t hash = 14695981039346656037ull;
    void add(std::uint8_t byte) noexcept {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    void add(const Step& s) noexcept {
        add(s.velocity);
        add(static_cast<std::uint8_t>(s.layer));
        add(s.ratchet);
        add(static_cast<std::uint8_t>(s.nudge));
        add(static_cast<std::uint8_t>(s.flam ? 1 : 0));
    }
};

std::uint64_t patternHash(const Pattern& p) noexcept {
    Fnv1a fnv;
    fnv.add(p.bars);
    for (const DrumLane& lane : p.lanes) {
        for (const Step& s : lane.steps)
            fnv.add(s);
        fnv.add(lane.length);
        fnv.add(lane.midiNote);
        fnv.add(static_cast<std::uint8_t>(lane.offsetMs));
        fnv.add(static_cast<std::uint8_t>(lane.muted ? 1 : 0));
        fnv.add(static_cast<std::uint8_t>(lane.solo ? 1 : 0));
    }
    fnv.add(p.fill.beats);
    for (const auto& row : p.fill.steps)
        for (const Step& s : row)
            fnv.add(s);
    return fnv.hash;
}

std::uint64_t splitMix64(std::uint64_t x) noexcept {
    std::uint64_t z = x + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// What one bar sounds at Density 50%: the Core and Groove steps. A ratchet counts its hits, a flam counts two.
struct BarCount {
    int notes = 0;
    int backbeatExtras = 0; // non-Core snare, ghost or clap note-ons
};

BarCount countBar(const Pattern& p, int bar) noexcept {
    BarCount count;
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        for (int step = 0; step < kStepsPerBar; ++step) {
            const Step& s = stepOf(p, lane, storedStep(p, lane, bar * kStepsPerBar + step));
            if (!s.on() || s.layer == Layer::detail)
                continue;
            const int hits = static_cast<int>(s.ratchet) + (s.flam ? 1 : 0);
            count.notes += hits;
            if (lane >= kSnareLane && lane <= kClapLane && s.layer != Layer::core)
                count.backbeatExtras += hits;
        }
    }
    return count;
}

// A move fits when no bar ends up over a budget it was inside of before (a bar already over stays allowed to shrink).
bool fitsBudgets(const Pattern& before, const Pattern& after) noexcept {
    for (int bar = 0; bar < patternBars(after); ++bar) {
        const BarCount was = countBar(before, bar);
        const BarCount now = countBar(after, bar);
        if (now.notes > kVaryMaxBarNotes && now.notes > was.notes)
            return false;
        if (now.backbeatExtras > kVaryMaxBarBackbeatExtras && now.backbeatExtras > was.backbeatExtras)
            return false;
    }
    return true;
}

bool laneHasSteps(const Pattern& p, int lane) noexcept {
    for (int stored = 0; stored < laneLoopSteps(p, lane); ++stored)
        if (stepOf(p, lane, stored).on())
            return true;
    return false;
}

void collect(const Pattern& p, Move kind, const Touched& done, CandidateList& out) noexcept {
    out.count = 0;
    const int origin = varyTargetBar(p) * kStepsPerBar;
    const auto stored = [&p, origin](int lane, int step) { return storedStep(p, lane, origin + step); };
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        if (kind == Move::add &&
            ((lane != kGhostLane && lane != kHatLane && lane != kPercALane) || !laneHasSteps(p, lane)))
            continue;
        for (int step = 0; step < kStepsPerBar; ++step) {
            const int s = stored(lane, step);
            if (touched(done, lane, s))
                continue;
            const Step& here = stepOf(p, lane, s);
            switch (kind) {
            case Move::shift:
                if (!variable(here))
                    break;
                for (const int dir : {-1, 1}) {
                    const int next = step + dir;
                    if (next < 0 || next >= kStepsPerBar)
                        continue;
                    const int t = stored(lane, next);
                    if (t != s && !touched(done, lane, t) && !stepOf(p, lane, t).on())
                        out.push(lane, s, t);
                }
                break;
            case Move::add:
                if ((step & 1) == 1 && !here.on())
                    out.push(lane, s, s);
                break;
            case Move::remove:
                if (variable(here))
                    out.push(lane, s, s);
                break;
            case Move::ornament:
                if (variable(here) &&
                    ((lane == kHatLane && here.ratchet == 1) || lane == kSnareLane || lane == kGhostLane))
                    out.push(lane, s, s);
                break;
            }
        }
    }
}

void apply(Pattern& p, Move kind, const Candidate& c) noexcept {
    Step& s = stepOf(p, c.lane, c.from);
    switch (kind) {
    case Move::shift:
        stepOf(p, c.lane, c.to) = s;
        s = {};
        break;
    case Move::add:
        s = {};
        s.velocity = 64;
        s.layer = Layer::detail;
        break;
    case Move::remove:
        s = {};
        break;
    case Move::ornament:
        if (c.lane == kHatLane)
            s.ratchet = 2;
        else
            s.flam = !s.flam;
        break;
    }
}

} // namespace

bool vary(const Pattern& in, Pattern& out) noexcept {
    Pattern work = in;
    sanitise(work);
    const std::uint64_t hash = patternHash(work);

    Touched done{};
    std::array<CandidateList, kMoveKinds> lists{};
    bool changed = false;
    for (int move = 0; move < kMoves; ++move) {
        const std::uint64_t draw = splitMix64(hash + static_cast<std::uint64_t>(move));

        // The kinds that have a candidate now, in a fixed order.
        std::array<Move, kMoveKinds> kinds{};
        int kindCount = 0;
        for (int k = 0; k < kMoveKinds; ++k) {
            const auto kind = static_cast<Move>(k);
            collect(work, kind, done, lists[static_cast<std::size_t>(k)]);
            if (lists[static_cast<std::size_t>(k)].count > 0)
                kinds[static_cast<std::size_t>(kindCount++)] = kind;
        }
        if (kindCount == 0)
            break;

        // The hash picks a kind and a candidate. When that one would break a budget, the next one is tried.
        const auto firstKind = static_cast<int>(draw % static_cast<std::uint64_t>(kindCount));
        bool moved = false;
        for (int k = 0; k < kindCount && !moved; ++k) {
            const Move kind = kinds[static_cast<std::size_t>((firstKind + k) % kindCount)];
            const CandidateList& list = lists[static_cast<std::size_t>(kind)];
            const auto first = static_cast<int>((draw >> 32) % static_cast<std::uint64_t>(list.count));
            for (int c = 0; c < list.count && !moved; ++c) {
                const Candidate& candidate = list.items[static_cast<std::size_t>((first + c) % list.count)];
                Pattern trial = work;
                apply(trial, kind, candidate);
                if (!fitsBudgets(work, trial))
                    continue;
                work = trial;
                done[static_cast<std::size_t>(candidate.lane)][static_cast<std::size_t>(candidate.from)] = true;
                done[static_cast<std::size_t>(candidate.lane)][static_cast<std::size_t>(candidate.to)] = true;
                moved = true;
            }
        }
        changed = changed || moved;
    }

    sanitise(work);
    if (!changed) {
        out = in;
        return false;
    }
    out = work;
    return true;
}

} // namespace cl
