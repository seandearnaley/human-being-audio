// The four movement lanes, powered by hba::ui::StudioMovementPanel.
#include "panels.h"

#include "movement_edit.h"

#include <hba/ui/movement_panel.h>

#include <algorithm>
#include <cmath>

namespace tf::ui {

namespace {
struct MovementTraits {
    using Context = tf::ui::Context;
    using Panel = tf::ui::Panel;
    using ParamFrame = tf::ui::ParamFrame;
    using Chip = tf::ui::Chip;
    using Params = tf::Params;
    using ParamId = tf::ParamId;
    using LaneField = tf::LaneField;
    using LaneShape = tf::LaneShape;
    using LaneCurves = tf::LaneCurves;

    static constexpr int kLaneCount = tf::kLaneCount;
    static constexpr int kLaneFieldCount = tf::kLaneFieldCount;
    static constexpr bool kHasEnvLane = false;
    static constexpr bool kHasModeChip = true;
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
    static ParamId field(int lane, LaneField f) { return laneParam(lane, f); }
    static Params paramsOf(const Model& m) { return tf::ui::paramsOf(m); }
    static LaneCurves curvesOf(const Model& m) { return tf::ui::curvesOf(m); }
    static bool following(const Params& p, int lane) { return laneSource(p, lane) != lane; }
    static unsigned laneHue(const Params&, int) { return colour::motion; }

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
        const auto& v = m.visuals();
        return static_cast<float>(lanePhaseFor(p, lane, v.ppq, v.notePpq));
    }

    static NoteTiming noteTiming(const Params& p, int lane, double notePpq, double windowQuarters) {
        const LaneMode mode = laneModeOf(p, lane);
        const bool noteClock = mode != LaneMode::sync;
        const double noteWindow =
            mode == LaneMode::oneShot ? 0.0 : std::floor(notePpq / windowQuarters) * windowQuarters;
        const float normPos = static_cast<float>(std::clamp((notePpq - noteWindow) / windowQuarters, 0.0, 1.0));
        return {noteClock, noteWindow, normPos};
    }

    static double sampleLane(const Params& p, const LaneCurves& curves, int lane, double samplePpq,
                             double sampleNotePpq) {
        return laneOutput(p, curves, lane, samplePpq, sampleNotePpq);
    }

    static unsigned modeAccent(const Params& p, int lane, bool on, unsigned dim) {
        return (on && laneModeOf(p, lane) != LaneMode::sync) ? colour::master : dim;
    }
};
} // namespace

std::unique_ptr<Panel> makeMovementPanel(Context& context) {
    return std::make_unique<hba::ui::StudioMovementPanel<MovementTraits>>(context);
}

} // namespace tf::ui
