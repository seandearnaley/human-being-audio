// Four movement lanes with Free Hz LFOs and optional Envelope Follower mode, powered by hba::ui::StudioMovementPanel.
#include "panels.h"

#include "movement_edit.h"

#include <hba/ui/movement_panel.h>

#include <algorithm>
#include <cmath>
#include <span>

namespace pf::ui {

namespace {
struct MovementTraits {
    using Context = pf::ui::Context;
    using Panel = pf::ui::Panel;
    using ParamFrame = pf::ui::ParamFrame;
    using Chip = pf::ui::Chip;
    using Params = pf::Params;
    using ParamId = pf::ParamId;
    using LaneField = pf::LaneField;
    using LaneShape = pf::LaneShape;
    using LaneCurves = pf::LaneCurves;

    static constexpr int kLaneCount = pf::kLaneCount;
    static constexpr int kLaneFieldCount = pf::kLaneFieldCount;
    static constexpr bool kHasEnvLane = true;
    static constexpr bool kHasModeChip = true;
    static constexpr bool kHasFreeHz = true;
    static constexpr bool kDynamicOnAccent = false;

    static constexpr hba::ui::MovementColumnWidths kColumns{
        24.0f, // switchW
        76.0f, // targetW
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

    static unsigned defaultAccent() { return colour::motion; }
    static unsigned envAccent() { return colour::sub; }
    static unsigned freeAccent() { return colour::poles; }
    static ParamId envSourceParam() { return ParamId::envSource; }
    static ParamId envAttackParam() { return ParamId::envAttack; }
    static ParamId envReleaseParam() { return ParamId::envRelease; }
    static ParamId field(int lane, LaneField f) { return laneParam(lane, f); }
    static Params paramsOf(const Model& m) { return pf::ui::paramsOf(m); }
    static LaneCurves curvesOf(const Model& m) { return pf::ui::curvesOf(m); }
    static bool following(const Params& p, int lane) { return laneSource(p, lane) != lane; }
    static bool isEnvLane(const Params& p, int lane) { return laneEffectiveShape(p, lane) == LaneShape::env; }
    static bool isFreeLane(const Params& p, int lane) { return laneUsesFreeClock(p, lane); }
    static bool isFreeRetrig(const Params& p, int lane) { return laneEffectiveMode(p, lane) == LaneMode::freeRetrig; }
    static double freeSeconds(Model& m) { return m.visuals().freeSec; }
    static double noteSeconds(Model& m) { return m.visuals().noteSec; }
    static unsigned laneHue(const Params&, int) { return colour::motion; }
    static std::span<const float> envHistory(Model& m) { return m.visuals().envHistory; }
    static float envLevel(Model& m) { return m.visuals().envLevel; }

    static LaneShape shapeOf(const Params& p, int lane) {
        return static_cast<LaneShape>(
            std::clamp(p.choice(field(lane, LaneField::shape)), 0, static_cast<int>(LaneShape::env)));
    }

    static Curve cycleCurve(const Model& m, const Params& p, int lane) {
        const int source = laneSource(p, lane);
        const LaneShape shape = shapeOf(p, source);
        return shape == LaneShape::custom ? m.laneCurve(source) : startingCurve(shape, source);
    }

    static float editorPhase(Model& m, const Params& p, int lane) {
        const auto& v = m.visuals();
        return static_cast<float>(lanePhaseFor(p, lane, v.ppq, v.notePpq, v.freeSec, v.noteSec));
    }

    static NoteTiming noteTiming(const Params& p, int lane, double notePpq, double windowQuarters) {
        const LaneMode mode = laneEffectiveMode(p, lane);
        const bool noteClock = mode != LaneMode::sync;
        const double noteWindow =
            mode == LaneMode::oneShot ? 0.0 : std::floor(notePpq / windowQuarters) * windowQuarters;
        const float normPos = static_cast<float>(std::clamp((notePpq - noteWindow) / windowQuarters, 0.0, 1.0));
        return {noteClock, noteWindow, normPos};
    }

    static double sampleLane(const Params& p, const LaneCurves& curves, int lane, double samplePpq,
                             double sampleNotePpq) {
        return laneOutput(p, curves, lane, samplePpq, 0.0, sampleNotePpq);
    }

    static double sampleLaneFree(const Params& p, const LaneCurves& curves, int lane, double sampleFreeSec,
                                 double sampleNoteSec) {
        return laneOutput(p, curves, lane, 0.0, 0.0, 0.0, sampleFreeSec, sampleNoteSec);
    }

    static unsigned modeAccent(const Params& p, int lane, bool on, unsigned dim) {
        return (on && laneEffectiveMode(p, lane) != LaneMode::sync) ? colour::poles : dim;
    }
};
} // namespace

std::unique_ptr<Panel> makeMovementPanel(Context& context) {
    return std::make_unique<hba::ui::StudioMovementPanel<MovementTraits>>(context);
}

} // namespace pf::ui
