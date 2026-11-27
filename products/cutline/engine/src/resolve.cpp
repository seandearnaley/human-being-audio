// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/resolve.h"

#include "cl/groove_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace cl {

namespace {

// Kick, snare, closed hat, open hat, clap, ghost, perc A, perc B.
constexpr std::array<int, kDrumLaneCount> kLanePriority{0, 1, 5, 4, 2, 3, 6, 7}; // indexed by lane

constexpr int metricWeight(int stepInBar) noexcept CL_NONBLOCKING {
    if (stepInBar == 0)
        return 0;
    if (stepInBar == 8)
        return 1;
    if (stepInBar == 4 || stepInBar == 12)
        return 2;
    return (stepInBar % 2) == 0 ? 3 : 4;
}

struct Ref {
    std::uint8_t lane = 0;
    std::uint8_t step = 0;
};

void rankLayer(const Pattern& pattern, Layer layer, float base, DensityMap& out) noexcept CL_NONBLOCKING {
    std::array<Ref, kDrumLaneCount * kMaxPatternSteps> refs{};
    int count = 0;
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const int loop = laneLoopSteps(pattern, lane);
        for (int step = 0; step < loop; ++step) {
            const Step& s = pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
            if (s.on() && s.layer == layer)
                refs[static_cast<std::size_t>(count++)] = {static_cast<std::uint8_t>(lane),
                                                           static_cast<std::uint8_t>(step)};
        }
    }
    for (int i = 1; i < count; ++i) {
        const Ref key = refs[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && rankBefore(key.lane, key.step, refs[static_cast<std::size_t>(j)].lane,
                                    refs[static_cast<std::size_t>(j)].step)) {
            refs[static_cast<std::size_t>(j + 1)] = refs[static_cast<std::size_t>(j)];
            --j;
        }
        refs[static_cast<std::size_t>(j + 1)] = key;
    }
    for (int i = 0; i < count; ++i) {
        const Ref r = refs[static_cast<std::size_t>(i)];
        out.onAt[r.lane][r.step] = base + 0.5f * static_cast<float>(i + 1) / static_cast<float>(count);
    }
}

} // namespace

bool rankBefore(int laneA, int stepA, int laneB, int stepB) noexcept CL_NONBLOCKING {
    const int weightA = metricWeight(stepA % kStepsPerBar);
    const int weightB = metricWeight(stepB % kStepsPerBar);
    if (weightA != weightB)
        return weightA < weightB;
    const int priorityA = kLanePriority[static_cast<std::size_t>(laneA)];
    const int priorityB = kLanePriority[static_cast<std::size_t>(laneB)];
    if (priorityA != priorityB)
        return priorityA < priorityB;
    return stepA < stepB; // earlier bar first, then earlier step
}

void buildDensityMap(const Pattern& pattern, DensityMap& out) noexcept CL_NONBLOCKING {
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const int loop = laneLoopSteps(pattern, lane);
        for (int step = 0; step < kMaxPatternSteps; ++step) {
            const Step& s = pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
            const bool core = step < loop && s.on() && s.layer == Layer::core;
            out.onAt[static_cast<std::size_t>(lane)][static_cast<std::size_t>(step)] = core ? 0.0f : 2.0f;
        }
    }
    rankLayer(pattern, Layer::groove, 0.0f, out);
    rankLayer(pattern, Layer::detail, 0.5f, out);
}

std::uint8_t applyDynamics(int velocity, float dynamics) noexcept CL_NONBLOCKING {
    const float d = std::isfinite(dynamics) ? std::clamp(dynamics, 0.0f, 1.0f) : 0.5f;
    const float v = 64.0f + (static_cast<float>(velocity) - 64.0f) * (0.5f + d);
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 1, 127));
}

void resolveBar(const Pattern& pattern, const DensityMap& map, int barInPattern, const ResolveInput& in,
                ResolvedBar& out) noexcept CL_NONBLOCKING {
    const int bars = patternBars(pattern);
    const int bar = ((barInPattern % bars) + bars) % bars;
    const int fillFrom = in.fillActive ? fillWindowStart(pattern.fill) : kStepsPerBar;
    // A non-finite Density plays Core only. The epsilon is there because thresholds are computed as
    // 0.5f * i / n: a Density typed as the same fraction can land one float step below its threshold.
    const float rawDensity = std::isfinite(in.density) ? in.density : 0.0f;
    const float density = std::clamp(rawDensity, 0.0f, 1.0f) + 1.0e-6f;

    bool anySolo = false;
    for (const auto& lane : pattern.lanes)
        anySolo = anySolo || lane.solo;

    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const auto li = static_cast<std::size_t>(lane);
        const DrumLane& cfg = pattern.lanes[li];
        const bool audible = anySolo ? (cfg.solo && !cfg.muted) : !cfg.muted;
        // A lane takes the fill only if something is drawn for it inside the window.
        // A lane with nothing drawn there keeps playing the main pattern.
        bool laneFilled = false;
        for (int step = fillFrom; step < kStepsPerBar; ++step)
            laneFilled = laneFilled || pattern.fill.steps[li][static_cast<std::size_t>(step)].on();
        for (int step = 0; step < kStepsPerBar; ++step) {
            const auto si = static_cast<std::size_t>(step);
            ResolvedStep& r = out.lanes[li][si];
            r = {};
            const bool inFill = laneFilled && step >= fillFrom;
            const int stored = storedStep(pattern, lane, bar * kStepsPerBar + step);
            const Step& s = inFill ? pattern.fill.steps[li][si] : cfg.steps[static_cast<std::size_t>(stored)];
            if (!s.on())
                continue;
            const bool sounding = inFill || density >= map.onAt[li][static_cast<std::size_t>(stored)];
            r.written = true;
            r.fromFill = inFill;
            r.layer = s.layer;
            r.ratchet = static_cast<std::uint8_t>(std::clamp(static_cast<int>(s.ratchet), 1, 4));
            r.nudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(s.nudge), -24, 24));
            r.flam = s.flam;
            r.velocity = (sounding && audible) ? applyDynamics(s.velocity, in.dynamics) : std::uint8_t{0};
        }
    }
}

void resolveBar(const Pattern& pattern, int barInPattern, const ResolveInput& in,
                ResolvedBar& out) noexcept CL_NONBLOCKING {
    DensityMap map;
    buildDensityMap(pattern, map);
    resolveBar(pattern, map, barInPattern, in, out);
}

GrooveState::GrooveState() noexcept : GrooveState(GrooveBank{}) {}

GrooveState::GrooveState(const GrooveBank& source) noexcept : bank(source) {
    sanitise(bank);
    for (std::size_t scene = 0; scene < bank.scenes.size(); ++scene)
        buildDensityMap(bank.scenes[scene], maps[scene]);
}

} // namespace cl
