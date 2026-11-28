// Movement lane shapes. The engine plays them and the interface draws them, so
// both read this one definition.
#pragma once

#include "tf/curve.h"
#include "tf/params.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tf {

using hba::dsp::kLaneStepsPerCycle;
using hba::dsp::laneHash;
using hba::dsp::laneModeIsFree;
using hba::dsp::lanePhase;
using hba::dsp::lanePhaseHz;
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

inline std::array<hba::dsp::LaneConfig, kLaneCount> extractLaneConfigs(const Params& params) noexcept {
    std::array<hba::dsp::LaneConfig, kLaneCount> lanes{};
    for (int i = 0; i < kLaneCount; ++i) {
        auto& cfg = lanes[static_cast<std::size_t>(i)];
        cfg.on = params.on(laneParam(i, LaneField::on));
        cfg.rateIndex = params.choice(laneParam(i, LaneField::rate));
        cfg.phaseDeg = static_cast<double>(params[laneParam(i, LaneField::phase)]);
        cfg.depth = static_cast<double>(params[laneParam(i, LaneField::depth)]);
        cfg.shape = params.choice(laneParam(i, LaneField::shape));
        cfg.follow = params.choice(laneParam(i, LaneField::follow));
        cfg.ratioIndex = params.choice(laneParam(i, LaneField::ratio));
        cfg.mode = static_cast<LaneMode>(
            std::clamp(params.choice(laneParam(i, LaneField::mode)), 0, static_cast<int>(LaneMode::freeRetrig)));
        cfg.hz = static_cast<double>(params[laneParam(i, LaneField::hz)]);
    }
    return lanes;
}

// The lane whose shape, curve and rate `lane` plays: itself, or the lane it
// follows. Chains are followed to their end; a loop falls back to the lane itself.
inline int laneSource(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneSource(extractLaneConfigs(params), lane);
}

// One cycle of `lane` in quarter notes: its own rate, or its leader's divided by
// the ratio (a ratio of x2 runs twice as fast as the leader).
inline double laneLengthQuarters(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneLengthQuarters(extractLaneConfigs(params), lane);
}

inline LaneMode laneModeOf(const Params& params, int lane) noexcept {
    return static_cast<LaneMode>(
        std::clamp(params.choice(laneParam(lane, LaneField::mode)), 0, static_cast<int>(LaneMode::freeRetrig)));
}

// Effective timing mode for `lane`. When following a Free / Free R leader, the
// follower automatically inherits the leader's Hz clock domain so linked lanes stay
// phase-locked and ratio-accurate across arbitrary BPM or transport changes.
inline LaneMode laneEffectiveMode(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneMode(extractLaneConfigs(params), lane);
}

inline bool laneUsesFreeClock(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneUsesFreeClock(extractLaneConfigs(params), lane);
}

// Cycle frequency in Hz for a free-running lane (or a follower locked to a free leader,
// scaled accurately by the follower's ratio).
inline double laneRateHz(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneRateHz(extractLaneConfigs(params), lane);
}

inline double lanePhaseFor(const Params& params, int lane, double hostPpq, double notePpq, double freeSec = 0.0,
                           double noteSec = 0.0) noexcept {
    return hba::dsp::resolveLanePhase(extractLaneConfigs(params), lane, {hostPpq, notePpq, freeSec, noteSec});
}

// The lane's shape in -1..1 at a host position (and optional note-relative / free-Hz clocks).
inline double laneOutput(const Params& params, const LaneCurves& curves, int lane, double ppq, double notePpq = 0.0,
                         double freeSec = 0.0, double noteSec = 0.0) noexcept {
    return hba::dsp::evaluateLaneOutput(extractLaneConfigs(params), curves, lane, {ppq, notePpq, freeSec, noteSec});
}

// What the lane sends to its target, in -1..1: shape times depth times Motion,
// or zero when the lane is off.
inline double laneSend(const Params& params, const LaneCurves& curves, int lane, double ppq, double notePpq = 0.0,
                       double freeSec = 0.0, double noteSec = 0.0) noexcept {
    return hba::dsp::evaluateLaneSend(extractLaneConfigs(params), curves, lane, {ppq, notePpq, freeSec, noteSec},
                                      static_cast<double>(params[ParamId::motion]));
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
inline constexpr std::array<LaneSpan, kLaneTargetCount> kLaneSpans{{
    {90.0f, false},  // phase: degrees, opposite ways on the two voices
    {0.5f, false},   // morph
    {0.5f, false},   // drive
    {0.5f, false},   // beat
    {0.5f, false},   // coupling
    {2.0f, true},    // reach
    {0.5f, false},   // weight
    {2.0f, true},    // sculpt
    {180.0f, false}, // fit: degrees, wraps
    {0.5f, false},   // width
    {18.0f, false},  // shift: formant shift in semitones
    {0.5f, false},   // shape: both voices' Shape
    {0.5f, false},   // res: filter resonance
}};

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
