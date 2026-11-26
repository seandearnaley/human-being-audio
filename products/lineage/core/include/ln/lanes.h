// SPDX-License-Identifier: AGPL-3.0-or-later
// Thin adapter binding Lineage parameters to the shared hba::dsp 5-mode lane router.
#pragma once

#include "ln/curve.h"
#include "ln/params.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ln {

using hba::dsp::kLaneStepsPerCycle;
using hba::dsp::laneHash;
using hba::dsp::laneModeIsFree;
using hba::dsp::lanePhase;
using hba::dsp::lanePhaseHz;
using hba::dsp::laneStepValue;

inline double laneShape(LaneShape shape, double phase, int lane) noexcept HBA_NONBLOCKING {
    return hba::dsp::laneWaveform(static_cast<hba::dsp::LaneWaveform>(shape), phase, lane);
}

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

inline int laneSource(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneSource(extractLaneConfigs(params), lane);
}

inline double laneLengthQuarters(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneLengthQuarters(extractLaneConfigs(params), lane);
}

inline LaneMode laneModeOf(const Params& params, int lane) noexcept {
    return static_cast<LaneMode>(
        std::clamp(params.choice(laneParam(lane, LaneField::mode)), 0, static_cast<int>(LaneMode::freeRetrig)));
}

inline LaneMode laneEffectiveMode(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneMode(extractLaneConfigs(params), lane);
}

inline bool laneUsesFreeClock(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneUsesFreeClock(extractLaneConfigs(params), lane);
}

inline double laneRateHz(const Params& params, int lane) noexcept {
    return hba::dsp::resolveLaneRateHz(extractLaneConfigs(params), lane);
}

inline double lanePhaseFor(const Params& params, int lane, double hostPpq, double notePpq = 0.0, double freeSec = 0.0,
                           double noteSec = 0.0) noexcept {
    return hba::dsp::resolveLanePhase(extractLaneConfigs(params), lane, {hostPpq, notePpq, freeSec, noteSec});
}

inline double laneOutput(const Params& params, const LaneCurves& curves, int lane, double ppq, double notePpq = 0.0,
                         double freeSec = 0.0, double noteSec = 0.0) noexcept {
    return hba::dsp::evaluateLaneOutput(extractLaneConfigs(params), curves, lane, {ppq, notePpq, freeSec, noteSec});
}

inline double laneSend(const Params& params, const LaneCurves& curves, int lane, double ppq, double notePpq = 0.0,
                       double freeSec = 0.0, double noteSec = 0.0) noexcept {
    return hba::dsp::evaluateLaneSend(extractLaneConfigs(params), curves, lane, {ppq, notePpq, freeSec, noteSec},
                                      static_cast<double>(params[ParamId::motion]));
}

inline LaneTarget laneTargetOf(const Params& params, int lane) noexcept {
    return static_cast<LaneTarget>(
        std::clamp(params.choice(laneParam(lane, LaneField::target)), 0, kLaneTargetCount - 1));
}

struct LaneSpan {
    float amount;
    bool octaves;
};

inline constexpr std::array<LaneSpan, kLaneTargetCount> kLaneSpans{{
    {3.0f, true},   // filt1Hz: +/- 3 octaves
    {3.0f, true},   // filt2Hz: +/- 3 octaves
    {0.55f, false}, // tone
    {0.55f, false}, // drive
    {0.50f, false}, // strataPos
    {0.50f, false}, // strataSpan
    {0.50f, false}, // genMix
    {0.55f, false}, // disperse
    {180.0f, false},// subPhase (degrees)
    {0.50f, false}, // width
    {0.50f, false}, // density
}};

inline float laneSum(const Params& params, const std::array<float, kLaneCount>& sends, LaneTarget target) noexcept {
    float sum = 0.0f;
    for (int lane = 0; lane < kLaneCount; ++lane)
        if (laneTargetOf(params, lane) == target)
            sum += sends[static_cast<std::size_t>(lane)];
    return sum;
}

inline float laneApply(LaneTarget target, float base, float sum) noexcept {
    const LaneSpan span = kLaneSpans[static_cast<std::size_t>(target)];
    return span.octaves ? base * std::pow(2.0f, span.amount * sum) : base + span.amount * sum;
}

} // namespace ln
