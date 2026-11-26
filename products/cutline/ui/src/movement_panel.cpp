// SPDX-License-Identifier: AGPL-3.0-or-later
// Four multi-hue expression lanes, powered by hba::ui::StudioMovementPanel.
#include "panels.h"

#include "movement_edit.h"

#include <hba/ui/movement_panel.h>

#include <algorithm>

namespace cl::ui {

namespace {
unsigned targetAccent(LaneTarget target) {
    switch (target) {
    case LaneTarget::pitchBend:
        return colour::cyan;
    case LaneTarget::cc74Slide:
        return colour::iris;
    case LaneTarget::cc1Mod:
        return colour::amber;
    case LaneTarget::pressure:
        return colour::vermilion;
    case LaneTarget::cc11Expr:
        return colour::emerald;
    case LaneTarget::cc2Breath:
        return colour::cyan;
    case LaneTarget::cc71Res:
        return colour::amber;
    case LaneTarget::velocity:
        return colour::emerald;
    }
    return colour::iris;
}

struct MovementTraits {
    using Context = cl::ui::Context;
    using Panel = cl::ui::Panel;
    using ParamFrame = cl::ui::ParamFrame;
    using Chip = cl::ui::Chip;
    using Params = cl::Params;
    using ParamId = cl::ParamId;
    using LaneField = cl::LaneField;
    using LaneShape = cl::LaneShape;
    using LaneCurves = cl::LaneCurves;

    static constexpr int kLaneCount = cl::kLaneCount;
    static constexpr int kLaneFieldCount = cl::kLaneFieldCount;
    static constexpr bool kHasEnvLane = false;
    static constexpr bool kHasModeChip = false;
    static constexpr bool kDynamicOnAccent = true;

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

    static unsigned defaultAccent() { return colour::iris; }
    static ParamId field(int lane, LaneField f) { return laneParam(lane, f); }
    static Params paramsOf(const Model& m) { return cl::ui::paramsOf(m); }
    static LaneCurves curvesOf(const Model& m) { return cl::ui::curvesOf(m); }
    static bool following(const Params& p, int lane) { return laneSource(p, lane) != lane; }
    static unsigned laneHue(const Params& p, int lane) { return targetAccent(laneTargetOf(p, lane)); }

    static LaneShape shapeOf(const Params& p, int lane) {
        return static_cast<LaneShape>(
            std::clamp(p.choice(field(lane, LaneField::shape)), 0, static_cast<int>(LaneShape::custom)));
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

} // namespace cl::ui
