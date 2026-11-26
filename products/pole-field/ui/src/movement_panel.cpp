// Four movement lanes with optional Envelope Follower mode, powered by hba::ui::StudioMovementPanel.
#include "panels.h"

#include "movement_edit.h"

#include <hba/ui/movement_panel.h>

#include <algorithm>
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
    static constexpr bool kHasModeChip = false;
    static constexpr bool kDynamicOnAccent = false;

    static constexpr hba::ui::MovementColumnWidths kColumns{
        24.0f, // switchW
        76.0f, // targetW
        56.0f, // followW
        60.0f, // rateW
        0.0f,  // modeW
        52.0f, // phaseW
        84.0f, // shapeW
        56.0f, // depthW
    };

    static unsigned defaultAccent() { return colour::motion; }
    static unsigned envAccent() { return colour::sub; }
    static ParamId envSourceParam() { return ParamId::envSource; }
    static ParamId envAttackParam() { return ParamId::envAttack; }
    static ParamId envReleaseParam() { return ParamId::envRelease; }
    static ParamId field(int lane, LaneField f) { return laneParam(lane, f); }
    static Params paramsOf(const Model& m) { return pf::ui::paramsOf(m); }
    static LaneCurves curvesOf(const Model& m) { return pf::ui::curvesOf(m); }
    static bool following(const Params& p, int lane) { return laneSource(p, lane) != lane; }
    static bool isEnvLane(const Params& p, int lane) { return laneEffectiveShape(p, lane) == LaneShape::env; }
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
        return static_cast<float>(
            lanePhase(m.visuals().ppq, laneLengthQuarters(p, lane), p[field(lane, LaneField::phase)]));
    }

    static double sampleLane(const Params& p, const LaneCurves& curves, int lane, double samplePpq, double) {
        return laneOutput(p, curves, lane, samplePpq);
    }
};
} // namespace

std::unique_ptr<Panel> makeMovementPanel(Context& context) {
    return std::make_unique<hba::ui::StudioMovementPanel<MovementTraits>>(context);
}

} // namespace pf::ui
