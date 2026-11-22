// Movement lane shapes. The engine plays them and the interface draws them, so
// both read this one definition.
#pragma once

#include "tf/curve.h"
#include "tf/params.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tf {

using hba::dsp::kLaneStepsPerCycle;
using hba::dsp::laneHash;
using hba::dsp::lanePhase;
using hba::dsp::laneStepValue;

// Shape value in -1..1 at `phase` (0..1). Sine and triangle start at 0 going
// up; the square is high for the first half.
inline double laneShape(LaneShape shape, double phase, int lane) noexcept HBA_NONBLOCKING {
    return hba::dsp::laneWaveform(static_cast<hba::dsp::LaneWaveform>(shape), phase, lane);
}

// The four lanes' drawn curves. Played when a lane's shape is Custom.
using LaneCurves = std::array<Curve, kLaneCount>;

inline LaneCurves defaultLaneCurves() noexcept {
    LaneCurves curves;
    curves.fill(curves::sine());
    return curves;
}

// The lane whose shape, curve and rate `lane` plays: itself, or the lane it
// follows. Chains are followed to their end; a loop falls back to the lane itself.
inline int laneSource(const Params& params, int lane) noexcept {
    int source = lane;
    for (int hop = 0; hop < kLaneCount; ++hop) {
        const int follow = params.choice(laneParam(source, LaneField::follow)) - 1;  // -1 is Own
        if (follow < 0 || follow >= kLaneCount || follow == source) return source;
        source = follow;
    }
    return lane;
}

// One cycle of `lane` in quarter notes: its own rate, or its leader's divided by
// the ratio (a ratio of x2 runs twice as fast as the leader).
inline double laneLengthQuarters(const Params& params, int lane) noexcept {
    const int source = laneSource(params, lane);
    const auto rate = static_cast<std::size_t>(
        std::clamp(params.choice(laneParam(source, LaneField::rate)), 0, static_cast<int>(kLaneRateQuarters.size()) - 1));
    if (source == lane) return kLaneRateQuarters[rate];
    const auto ratio = static_cast<std::size_t>(
        std::clamp(params.choice(laneParam(lane, LaneField::ratio)), 0, static_cast<int>(kLaneRatios.size()) - 1));
    return kLaneRateQuarters[rate] / kLaneRatios[ratio];
}

// The lane's shape in -1..1 at a host position, before depth and Motion. A
// following lane plays its leader's shape or curve at its own length and phase.
inline double laneOutput(const Params& params, const LaneCurves& curves, int lane, double ppq) noexcept {
    const int source = laneSource(params, lane);
    const double phase = lanePhase(ppq, laneLengthQuarters(params, lane), params[laneParam(lane, LaneField::phase)]);
    const auto shape = static_cast<LaneShape>(
        std::clamp(params.choice(laneParam(source, LaneField::shape)), 0, static_cast<int>(LaneShape::custom)));
    if (shape == LaneShape::custom) return evaluate(curves[static_cast<std::size_t>(source)], phase);
    return laneShape(shape, phase, source);
}

// What the lane sends to its target, in -1..1: shape times depth times Motion,
// or zero when the lane is off.
inline double laneSend(const Params& params, const LaneCurves& curves, int lane, double ppq) noexcept {
    if (!params.on(laneParam(lane, LaneField::on))) return 0.0;
    return laneOutput(params, curves, lane, ppq) * params[laneParam(lane, LaneField::depth)] * params[ParamId::motion];
}

inline LaneTarget laneTargetOf(const Params& params, int lane) noexcept {
    return static_cast<LaneTarget>(std::clamp(params.choice(laneParam(lane, LaneField::target)), 0, kLaneTargetCount - 1));
}

// How far a full-depth lane moves each target. Additive targets are in the
// parameter's own units; Reach and Sculpt are in octaves, because they are
// harmonic numbers and move by ratio.
struct LaneSpan {
    float amount;
    bool octaves;
};
inline constexpr std::array<LaneSpan, kLaneTargetCount> kLaneSpans { {
    { 90.0f, false },   // phase: degrees, opposite ways on the two voices
    { 0.5f, false },    // morph
    { 0.5f, false },    // drive
    { 0.5f, false },    // beat
    { 0.5f, false },    // coupling
    { 2.0f, true },     // reach
    { 0.5f, false },    // weight
    { 2.0f, true },     // sculpt
    { 180.0f, false },  // fit: degrees, wraps
    { 0.5f, false },    // width
} };

// The sum of every lane aimed at `target`, in -1..1 per lane (so it can exceed 1
// when lanes stack). `sends` are the lanes' current sends.
inline float laneSum(const Params& params, const std::array<float, kLaneCount>& sends, LaneTarget target) noexcept {
    float sum = 0.0f;
    for (int lane = 0; lane < kLaneCount; ++lane)
        if (laneTargetOf(params, lane) == target) sum += sends[static_cast<std::size_t>(lane)];
    return sum;
}

// A target's value after movement: `base` moved by `sum` lanes' worth of its span.
// The caller clamps or wraps to the parameter's range.
inline float laneApply(LaneTarget target, float base, float sum) noexcept {
    const LaneSpan span = kLaneSpans[static_cast<std::size_t>(target)];
    return span.octaves ? base * std::pow(2.0f, span.amount * sum) : base + span.amount * sum;
}

}
