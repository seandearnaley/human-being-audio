#include "tf/ui/fake_model.h"

#include "tf/feel.h"
#include "tf/lanes.h"

#include <algorithm>
#include <cmath>

namespace tf::ui {

namespace {
constexpr float kTwoPi = 6.2831853f;
}

FakeModel::FakeModel(double time) : time_(time) {}

// A rough picture of what the engine produces, good enough to judge layout by.
const Visuals& FakeModel::visuals() {
    const auto t = static_cast<float>(time_);
    const float coupling = params_[ParamId::coupling];
    const float drive = params_[ParamId::drive] * params_[ParamId::warp];
    const float beatPhase = kTwoPi * 0.18f * t;

    for (int i = 0; i < Visuals::kScopePoints; ++i) {
        const float phase = kTwoPi * 2.0f * static_cast<float>(i) / Visuals::kScopePoints;
        const float master = std::sin(phase);
        const float a = std::sin(phase + beatPhase + feel::pmIndex(params_[ParamId::aPm], coupling) * master);
        const float b = std::sin(phase - beatPhase + kTwoPi * 0.5f + feel::pmIndex(params_[ParamId::bPm], coupling) * master);
        const float growl = std::sin(1.5707963f * (1.0f + 4.0f * drive) * 0.5f * (a + b));
        visuals_.scopeMaster[static_cast<std::size_t>(i)] = master;
        visuals_.scopeGrowl[static_cast<std::size_t>(i)] = 0.8f * growl;
        visuals_.scopeOut[static_cast<std::size_t>(i)] = 0.55f * growl + 0.4f * master;
    }

    const float width = params_[ParamId::width];
    for (int i = 0; i < Visuals::kGonioPoints; ++i) {
        const float phase = kTwoPi * 3.0f * static_cast<float>(i) / Visuals::kGonioPoints;
        const float mid = 0.6f * std::sin(phase);
        const float side = 0.35f * width * std::sin(phase * 2.7f + 1.0f);
        visuals_.gonioLeft[static_cast<std::size_t>(i)] = mid + side;
        visuals_.gonioRight[static_cast<std::size_t>(i)] = mid - side;
    }
    visuals_.correlation = 1.0f - 0.9f * width;

    for (int i = 0; i < Visuals::kSpectrumBands; ++i) {
        const float position = static_cast<float>(i) / (Visuals::kSpectrumBands - 1);
        const float hz = 20.0f * std::pow(1000.0f, position);
        const float sub = -12.0f - 40.0f * std::abs(std::log2(hz / 43.65f));
        const float comb = 6.0f * std::sin(hz * 0.02f);
        const float growl = -22.0f - 14.0f * std::log2(std::max(hz, 120.0f) / 120.0f) * (1.2f - coupling) + comb;
        visuals_.spectrumDb[static_cast<std::size_t>(i)] = std::max({ sub, hz > 100.0f ? growl : -90.0f, -90.0f });
    }

    visuals_.peakDbLeft = -3.2f;
    visuals_.peakDbRight = -3.1f;
    visuals_.gainReductionDb = 1.4f;

    visuals_.ppq = time_ * 174.0 / 60.0;
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto index = static_cast<std::size_t>(lane);
        const double length = kLaneRateQuarters[static_cast<std::size_t>(params_.choice(laneParam(lane, LaneField::rate)))];
        const double phase = lanePhase(visuals_.ppq, length, params_[laneParam(lane, LaneField::phase)]);
        const auto shape = static_cast<LaneShape>(params_.choice(laneParam(lane, LaneField::shape)));
        visuals_.lanePhase[index] = static_cast<float>(phase);
        visuals_.laneValue[index] = params_.on(laneParam(lane, LaneField::on))
                                        ? static_cast<float>(laneShape(shape, phase, lane)) * params_[laneParam(lane, LaneField::depth)] * params_[ParamId::motion]
                                        : 0.0f;
    }

    visuals_.hz = 43.65f;
    visuals_.note = 29;
    visuals_.gate = true;
    return visuals_;
}

}
