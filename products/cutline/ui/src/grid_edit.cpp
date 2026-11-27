// SPDX-License-Identifier: AGPL-3.0-or-later
#include "grid_edit.h"

#include <algorithm>
#include <cstddef>

namespace cl::ui::grid {

Cell cellOf(const ResolvedStep& step) noexcept {
    Cell cell;
    if (!step.written)
        return cell;
    cell.layer = step.layer;
    cell.ratchet = step.ratchet;
    cell.flam = step.flam;
    if (step.velocity == 0) {
        cell.look = CellLook::outline;
        return cell;
    }
    cell.velocity = step.velocity;
    if (step.fromFill)
        cell.look = CellLook::fill;
    else
        cell.look = step.layer == Layer::core ? CellLook::core
                                              : (step.layer == Layer::detail ? CellLook::detail : CellLook::groove);
    return cell;
}

Cell fillCellOf(const ResolvedStep& step) noexcept {
    Cell cell = cellOf(step);
    if (cell.look != CellLook::empty && cell.look != CellLook::fill) {
        cell.look = CellLook::outline;
        cell.velocity = 0;
    }
    return cell;
}

float heightFraction(Layer layer) noexcept {
    return layer == Layer::core ? 1.0f : (layer == Layer::detail ? 0.44f : 0.72f);
}

View resolveView(const GrooveState& groove, int scene, float density, float dynamics, int liveFillBar) noexcept {
    const auto idx = static_cast<std::size_t>(std::clamp(scene, 0, kSceneCount - 1));
    const Pattern& pattern = groove.bank.scenes[idx];
    View view;
    view.bars = patternBars(pattern);
    for (int bar = 0; bar < view.bars; ++bar)
        resolveBar(pattern, groove.maps[idx], bar, {density, dynamics, bar == liveFillBar},
                   view.bar[static_cast<std::size_t>(bar)]);
    return view;
}

ResolvedBar resolveFillView(const GrooveState& groove, int scene, float density, float dynamics) noexcept {
    const auto idx = static_cast<std::size_t>(std::clamp(scene, 0, kSceneCount - 1));
    const Pattern& pattern = groove.bank.scenes[idx];
    ResolvedBar bar;
    resolveBar(pattern, groove.maps[idx], patternBars(pattern) - 1, {density, dynamics, true}, bar);
    return bar;
}

bool cellAt(const Geometry& geometry, float x, float y, int& lane, int& step) noexcept {
    if (geometry.stepW <= 0.0f || geometry.rowH <= 0.0f || x < geometry.gridX || y < geometry.gridY)
        return false;
    const int column = static_cast<int>((x - geometry.gridX) / geometry.stepW);
    const int row = static_cast<int>((y - geometry.gridY) / geometry.rowH);
    if (column >= geometry.steps || row >= geometry.rows)
        return false;
    lane = row;
    step = column;
    return true;
}

const Step* stepAt(const Pattern& pattern, bool fillView, int lane, int step) noexcept {
    if (lane < 0 || lane >= kDrumLaneCount || step < 0)
        return nullptr;
    const auto li = static_cast<std::size_t>(lane);
    if (fillView) {
        if (step >= kStepsPerBar || step < fillWindowStart(pattern.fill))
            return nullptr;
        return &pattern.fill.steps[li][static_cast<std::size_t>(step)];
    }
    if (step >= patternSteps(pattern))
        return nullptr;
    return &pattern.lanes[li].steps[static_cast<std::size_t>(storedStep(pattern, lane, step))];
}

Step* stepAt(Pattern& pattern, bool fillView, int lane, int step) noexcept {
    return const_cast<Step*>(stepAt(static_cast<const Pattern&>(pattern), fillView, lane, step));
}

bool toggleStep(Pattern& pattern, bool fillView, int lane, int step) noexcept {
    Step* s = stepAt(pattern, fillView, lane, step);
    if (s == nullptr)
        return false;
    if (s->on()) {
        *s = {};
    } else {
        *s = {};
        s->velocity = 96;
        s->layer = Layer::groove;
    }
    return true;
}

bool setStepVelocity(Step& step, int velocity) noexcept {
    if (!step.on())
        return false;
    const auto next = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    if (next == step.velocity)
        return false;
    step.velocity = next;
    return true;
}

bool cycleRatchet(Step& step) noexcept {
    if (!step.on())
        return false;
    step.ratchet = static_cast<std::uint8_t>(step.ratchet >= 4 ? 1 : step.ratchet + 1);
    return true;
}

bool setStepLayer(Step& step, Layer layer) noexcept {
    if (!step.on() || step.layer == layer)
        return false;
    step.layer = layer;
    return true;
}

bool setFillBeats(Pattern& pattern, int beats) noexcept {
    if ((beats != 1 && beats != 2 && beats != 4) || static_cast<int>(pattern.fill.beats) == beats)
        return false;
    pattern.fill.beats = static_cast<std::uint8_t>(beats);
    sanitise(pattern); // steps before the new window are no longer part of the fill
    return true;
}

bool setLaneLength(Pattern& pattern, int lane, int steps) noexcept {
    if (lane < 0 || lane >= kDrumLaneCount || steps < 1)
        return false;
    const int total = patternSteps(pattern);
    const int clamped = std::clamp(steps, 1, total);
    const auto next = static_cast<std::uint8_t>(clamped >= total ? 0 : clamped);
    std::uint8_t& length = pattern.lanes[static_cast<std::size_t>(lane)].length;
    if (length == next)
        return false;
    length = next;
    return true;
}

} // namespace cl::ui::grid
