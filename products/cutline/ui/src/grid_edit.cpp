// SPDX-License-Identifier: AGPL-3.0-or-later
#include "grid_edit.h"

#include <algorithm>
#include <cmath>
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

int brushVelocity(Layer layer) noexcept { return layer == Layer::core ? 112 : (layer == Layer::detail ? 64 : 96); }

bool toggleStep(Pattern& pattern, bool fillView, int lane, int step, Layer brush) noexcept {
    const Step* s = stepAt(pattern, fillView, lane, step);
    if (s == nullptr)
        return false;
    return s->on() ? eraseStep(pattern, fillView, lane, step) : paintStep(pattern, fillView, lane, step, brush);
}

bool paintStep(Pattern& pattern, bool fillView, int lane, int step, Layer brush) noexcept {
    Step* s = stepAt(pattern, fillView, lane, step);
    if (s == nullptr || s->on())
        return false;
    *s = {};
    s->velocity = static_cast<std::uint8_t>(fillView ? kFillStepVelocity : brushVelocity(brush));
    s->layer = fillView ? Layer::groove : brush; // a fill step has no layer: sanitise keeps it Groove
    return true;
}

bool eraseStep(Pattern& pattern, bool fillView, int lane, int step) noexcept {
    Step* s = stepAt(pattern, fillView, lane, step);
    if (s == nullptr || !s->on())
        return false;
    *s = {};
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

bool setStepNudge(Step& step, int nudge) noexcept {
    if (!step.on())
        return false;
    const auto next = static_cast<std::int8_t>(std::clamp(nudge, -kMaxNudge, kMaxNudge));
    if (next == step.nudge)
        return false;
    step.nudge = next;
    return true;
}

bool toggleFlam(Step& step) noexcept {
    if (!step.on())
        return false;
    step.flam = !step.flam;
    return true;
}

int nudgeFromDrag(int startNudge, float dx, float pxPerUnit) noexcept {
    if (pxPerUnit <= 0.0f)
        return std::clamp(startNudge, -kMaxNudge, kMaxNudge);
    return std::clamp(startNudge + static_cast<int>(std::lround(dx / pxPerUnit)), -kMaxNudge, kMaxNudge);
}

bool setLanePocket(Pattern& pattern, int lane, int ms) noexcept {
    if (lane < 0 || lane >= kDrumLaneCount)
        return false;
    const auto next = static_cast<std::int8_t>(std::clamp(ms, -kMaxOffsetMs, kMaxOffsetMs));
    std::int8_t& offset = pattern.lanes[static_cast<std::size_t>(lane)].offsetMs;
    if (offset == next)
        return false;
    offset = next;
    return true;
}

std::string pocketLabel(int ms) {
    if (ms == 0)
        return {};
    return (ms > 0 ? "+" : "") + std::to_string(ms) + "ms";
}

HitTiming tickTiming(float swing, double bpm, int rateIdx) noexcept {
    HitTiming timing;
    timing.swing = static_cast<double>(std::clamp(swing, 0.0f, 1.0f));
    timing.beatsPerMs = std::clamp(bpm, 20.0, 400.0) / 60000.0 *
                        kSeqRateMultipliers[static_cast<std::size_t>(std::clamp(rateIdx, 0, 2))];
    return timing;
}

Tick tickAt(double beatsFromCellStart, bool grace) noexcept {
    constexpr double kCellBeats = 0.25;
    constexpr double kEps = 1e-9;
    const double at = beatsFromCellStart / kCellBeats;
    Tick tick;
    tick.grace = grace;
    if (at < -kEps) {
        tick.outside = -1;
        tick.at = 0.0f;
    } else if (at >= 1.0 - kEps) {
        tick.outside = 1;
        tick.at = 1.0f;
    } else {
        tick.at = static_cast<float>(std::max(0.0, at));
    }
    return tick;
}

void barTicks(const Pattern& pattern, const ResolvedBar& bar, const HitTiming& timing, BarTicks& out) noexcept {
    out = {};
    forEachHit(pattern, bar, 0.0, timing, [&out](const Hit& hit) {
        if (hit.lane < 0 || hit.lane >= kDrumLaneCount || hit.step < 0 || hit.step >= kStepsPerBar)
            return;
        CellTicks& cell = out[static_cast<std::size_t>(hit.lane)][static_cast<std::size_t>(hit.step)];
        if (cell.count >= static_cast<int>(cell.ticks.size()))
            return;
        const double cellStart = static_cast<double>(hit.step) * 0.25;
        cell.ticks[static_cast<std::size_t>(cell.count++)] = tickAt(hit.beat - cellStart, hit.grace);
    });
}

CellTicks slicerTicks(const Step& step, int gateStep, const HitTiming& timing) noexcept {
    if (!step.on() || gateStep < 0 || gateStep >= kMaxSlicerSteps)
        return {};
    // The gate step as one lit step of a bar: swing repeats every half beat, so its place in its bar is enough.
    ResolvedBar bar;
    ResolvedStep& r = bar.lanes[0][static_cast<std::size_t>(gateStep % kStepsPerBar)];
    r.velocity = step.velocity;
    r.ratchet = step.ratchet;
    r.nudge = step.nudge;
    r.written = true;
    BarTicks ticks;
    barTicks(Pattern{}, bar, timing, ticks);
    CellTicks cell = ticks[0][static_cast<std::size_t>(gateStep % kStepsPerBar)];
    if (gateStep == 0) // the phrase starts no note before its first step
        for (int i = 0; i < cell.count; ++i)
            if (cell.ticks[static_cast<std::size_t>(i)].outside < 0)
                cell.ticks[static_cast<std::size_t>(i)] = Tick{};
    return cell;
}

Phrase phraseOf(const PhraseInput& in) noexcept {
    Phrase phrase;
    phrase.length = in.fillEvery == 16 ? 16 : 8;
    const std::int64_t length = phrase.length;
    const std::int64_t now = in.playing ? std::max<std::int64_t>(0, in.absBar) : 0;
    phrase.start = (now / length) * length;
    const int bars = std::max(1, in.patternBars);
    for (int i = 0; i < phrase.length; ++i) {
        PhraseBar& cell = phrase.bars[static_cast<std::size_t>(i)];
        cell.absBar = phrase.start + i;
        cell.patternBar = static_cast<int>(cell.absBar % bars);
        cell.playing = in.playing && cell.absBar == now;
        cell.fill = fillScheduled(cell.absBar, in.fillEvery) || (in.fillNow && cell.absBar >= now);
        if (in.playing && in.pendingScene >= 0 && in.pendingOnBar && cell.absBar == now + 1)
            cell.pendingScene = in.pendingScene;
    }
    return phrase;
}

std::int64_t phraseBarAt(double ppq, double rate) noexcept {
    return static_cast<std::int64_t>(std::floor(ppq * rate / 4.0));
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
