// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared 4-lane drawable modulation panel for Prism Vein powered by hba::ui::StudioMovementPanel.
#include "panels.h"

#include <hba/dsp/lane_math.h>
#include <hba/ui/movement_panel.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace pv::ui {

namespace {

Params readParams(const Model& m) {
    Params p;
    for (std::size_t i = 0; i < kParamCount; ++i)
        p.values[i] = m.get(static_cast<ParamId>(i));
    return p;
}

LaneCurves readCurves(const Model& m) {
    LaneCurves c;
    for (int i = 0; i < kLaneCount; ++i)
        c[static_cast<std::size_t>(i)] = m.laneCurve(i);
    return c;
}

std::array<hba::dsp::LaneConfig, kLaneCount> laneConfigs(const Params& p) {
    std::array<hba::dsp::LaneConfig, kLaneCount> cfgs{};
    for (int l = 0; l < kLaneCount; ++l) {
        auto& c = cfgs[static_cast<std::size_t>(l)];
        c.on = p.on(laneParam(l, LaneField::on));
        c.rateIndex = p.choice(laneParam(l, LaneField::rate));
        c.phaseDeg = static_cast<double>(p.real(laneParam(l, LaneField::phase)));
        c.depth = static_cast<double>(p.real(laneParam(l, LaneField::depth)));
        c.shape = p.choice(laneParam(l, LaneField::shape));
        c.follow = p.choice(laneParam(l, LaneField::follow));
        c.ratioIndex = p.choice(laneParam(l, LaneField::ratio));
        c.mode = static_cast<hba::dsp::LaneMode>(p.choice(laneParam(l, LaneField::mode)));
        c.hz = static_cast<double>(p.real(laneParam(l, LaneField::hz)));
    }
    return cfgs;
}

hba::dsp::Curve builtInCurve(LaneShape shape, int lane) {
    switch (shape) {
    case LaneShape::sine:
        return hba::dsp::curves::sine();
    case LaneShape::triangle:
        return hba::dsp::curves::triangle();
    case LaneShape::rampUp:
        return hba::dsp::curves::rampUp();
    case LaneShape::rampDown:
        return hba::dsp::curves::rampDown();
    case LaneShape::square:
        return hba::dsp::curves::square();
    case LaneShape::steps: {
        hba::dsp::Curve c;
        for (int s = 0; s < hba::dsp::kLaneStepsPerCycle; ++s) {
            hba::dsp::addPoint(c,
                               {static_cast<float>(s) / static_cast<float>(hba::dsp::kLaneStepsPerCycle),
                                static_cast<float>(hba::dsp::laneStepValue(lane, s)), 0.0f, hba::dsp::Segment::hold});
        }
        return c;
    }
    case LaneShape::custom:
        break;
    }
    return hba::dsp::curves::sine();
}

struct MovementTraits {
    using Context = pv::ui::Context;
    using Panel = pv::ui::Panel;
    using ParamFrame = pv::ui::ParamFrame;
    using Chip = pv::ui::Chip;
    using Params = pv::Params;
    using ParamId = pv::ParamId;
    using LaneField = pv::LaneField;
    using LaneShape = pv::LaneShape;
    using LaneCurves = pv::LaneCurves;

    static constexpr int kLaneCount = pv::kLaneCount;
    static constexpr int kLaneFieldCount = pv::kLaneFieldCount;
    static constexpr bool kHasEnvLane = false;
    static constexpr bool kHasModeChip = true;
    static constexpr bool kHasFreeHz = true;
    static constexpr bool kDynamicOnAccent = true;

    static constexpr hba::ui::MovementColumnWidths kColumns{
        24.0f, // switchW
        74.0f, // targetW
        54.0f, // followW
        56.0f, // rateW
        54.0f, // modeW
        50.0f, // phaseW
        80.0f, // shapeW
        54.0f, // depthW
    };

    struct NoteTiming {
        bool noteClock = false;
        double noteWindow = 0.0;
        float normPos = 0.0f;
    };

    static unsigned defaultAccent() { return colour::prism; }
    static unsigned freeAccent() { return colour::sub; }
    static ParamId field(int lane, LaneField f) { return laneParam(lane, f); }
    static Params paramsOf(const Model& m) { return readParams(m); }
    static LaneCurves curvesOf(const Model& m) { return readCurves(m); }

    static bool following(const Params& p, int lane) {
        const auto cfgs = laneConfigs(p);
        return hba::dsp::resolveLaneSource(cfgs, lane) != lane;
    }

    static bool isFreeLane(const Params& p, int lane) {
        const auto cfgs = laneConfigs(p);
        return hba::dsp::resolveLaneUsesFreeClock(cfgs, lane);
    }

    static bool isFreeRetrig(const Params& p, int lane) {
        const auto cfgs = laneConfigs(p);
        return hba::dsp::resolveLaneMode(cfgs, lane) == LaneMode::freeRetrig;
    }

    static double freeSeconds(Model& m) { return m.visuals().freeSec; }
    static double noteSeconds(Model& m) { return m.visuals().noteSec; }

    static unsigned laneHue(const Params& p, int lane) {
        const auto t = static_cast<LaneTarget>(p.choice(laneParam(lane, LaneField::target)));
        switch (t) {
        case LaneTarget::color:
        case LaneTarget::shift:
        case LaneTarget::fan:
            return colour::prism;
        case LaneTarget::decay:
        case LaneTarget::morph:
            return colour::harmonic;
        case LaneTarget::tame:
            return colour::rose;
        case LaneTarget::grit:
        case LaneTarget::tilt:
            return colour::tame;
        }
        return colour::prism;
    }

    static hba::dsp::Curve cycleCurve(const Model& m, const Params& p, int lane) {
        const auto cfgs = laneConfigs(p);
        const int src = hba::dsp::resolveLaneSource(cfgs, lane);
        const auto shape = static_cast<LaneShape>(p.choice(laneParam(src, LaneField::shape)));
        return shape == LaneShape::custom ? m.laneCurve(src) : builtInCurve(shape, src);
    }

    static float editorPhase(Model& m, const Params& p, int lane) {
        const auto cfgs = laneConfigs(p);
        const auto& v = m.visuals();
        const hba::dsp::LaneClock clk{v.ppq, v.notePpq, v.freeSec, v.noteSec};
        return static_cast<float>(hba::dsp::resolveLanePhase(cfgs, lane, clk));
    }

    static NoteTiming noteTiming(const Params& p, int lane, double notePpq, double windowQuarters) {
        const auto cfgs = laneConfigs(p);
        const LaneMode mode = hba::dsp::resolveLaneMode(cfgs, lane);
        const bool noteClock = mode != LaneMode::sync;
        const double noteWindow =
            mode == LaneMode::oneShot ? 0.0 : std::floor(notePpq / windowQuarters) * windowQuarters;
        const float normPos = static_cast<float>(std::clamp((notePpq - noteWindow) / windowQuarters, 0.0, 1.0));
        return {noteClock, noteWindow, normPos};
    }

    static double sampleLane(const Params& p, const LaneCurves& curves, int lane, double samplePpq,
                             double sampleNotePpq) {
        const auto cfgs = laneConfigs(p);
        const hba::dsp::LaneClock clk{samplePpq, sampleNotePpq, 0.0, 0.0};
        return hba::dsp::evaluateLaneOutput(cfgs, curves, lane, clk);
    }

    static double sampleLaneFree(const Params& p, const LaneCurves& curves, int lane, double sampleFreeSec,
                                 double sampleNoteSec) {
        const auto cfgs = laneConfigs(p);
        const hba::dsp::LaneClock clk{0.0, 0.0, sampleFreeSec, sampleNoteSec};
        return hba::dsp::evaluateLaneOutput(cfgs, curves, lane, clk);
    }

    static unsigned modeAccent(const Params& p, int lane, bool on, unsigned dim) {
        const auto cfgs = laneConfigs(p);
        return (on && hba::dsp::resolveLaneMode(cfgs, lane) != LaneMode::sync) ? colour::sub : dim;
    }
};

} // namespace

std::unique_ptr<Panel> makeMovementPanel(Context& context) {
    return std::make_unique<hba::ui::StudioMovementPanel<MovementTraits>>(context);
}

} // namespace pv::ui
