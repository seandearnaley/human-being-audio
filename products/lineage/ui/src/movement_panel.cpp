// SPDX-License-Identifier: AGPL-3.0-or-later
// Unified 4-lane Free-Hz / Sync movement panel powered by hba::ui::StudioMovementPanel.
#include "panels.h"

#include "movement_edit.h"

#include <hba/ui/movement_panel.h>

#include <algorithm>
#include <cmath>

namespace ln::ui {

namespace {
struct MovementTraits {
    using Context = ln::ui::Context;
    using Panel = ln::ui::Panel;
    using ParamFrame = ln::ui::ParamFrame;
    using Chip = ln::ui::Chip;
    using Params = ln::Params;
    using ParamId = ln::ParamId;
    using LaneField = ln::LaneField;
    using LaneShape = ln::LaneShape;
    using LaneCurves = ln::LaneCurves;

    static constexpr int kLaneCount = ln::kLaneCount;
    static constexpr int kLaneFieldCount = ln::kLaneFieldCount;
    static constexpr bool kHasEnvLane = false;
    static constexpr bool kHasModeChip = true;
    static constexpr bool kHasFreeHz = true;
    static constexpr bool kDynamicOnAccent = false;

    static constexpr hba::ui::MovementColumnWidths kColumns{
        24.0f, // switchW
        82.0f, // targetW
        54.0f, // followW
        58.0f, // rateW
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

    static unsigned defaultAccent() { return colour::abyss; }
    static unsigned freeAccent() { return colour::strata; }
    static ParamId field(int lane, LaneField f) { return laneParam(lane, f); }
    static Params paramsOf(const Model& m) { return ln::ui::paramsOf(m); }
    static LaneCurves curvesOf(const Model& m) { return ln::ui::curvesOf(m); }
    static bool following(const Params& p, int lane) { return laneSource(p, lane) != lane; }
    static bool isFreeLane(const Params& p, int lane) { return laneUsesFreeClock(p, lane); }
    static bool isFreeRetrig(const Params& p, int lane) { return laneEffectiveMode(p, lane) == LaneMode::freeRetrig; }
    static double freeSeconds(Model& m) { return m.visuals().freeSec; }
    static double noteSeconds(Model& m) { return m.visuals().noteSec; }
    static unsigned laneHue(const Params&, int) { return colour::abyss; }

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
        return laneOutput(p, curves, lane, samplePpq, sampleNotePpq);
    }

    static double sampleLaneFree(const Params& p, const LaneCurves& curves, int lane, double sampleFreeSec,
                                 double sampleNoteSec) {
        return laneOutput(p, curves, lane, 0.0, 0.0, sampleFreeSec, sampleNoteSec);
    }

    static unsigned modeAccent(const Params& p, int lane, bool on, unsigned dim) {
        return (on && laneEffectiveMode(p, lane) != LaneMode::sync) ? colour::strata : dim;
    }
};
} // namespace

std::unique_ptr<Panel> makeMovementPanel(Context& context) {
    return std::make_unique<hba::ui::StudioMovementPanel<MovementTraits>>(context);
}

} // namespace ln::ui
