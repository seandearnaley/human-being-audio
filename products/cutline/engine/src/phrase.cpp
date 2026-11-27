// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/phrase.h"

#include "cl/hits.h"
#include "cl/resolve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cl {

namespace {

void pushNote(SlicerPlan& out, const SlicerNote& note) noexcept CL_NONBLOCKING {
    if (out.noteCount < kMaxSlicerNotes)
        out.notes[static_cast<std::size_t>(out.noteCount++)] = note;
}

void sortNotes(SlicerPlan& out) noexcept CL_NONBLOCKING {
    for (int i = 1; i < out.noteCount; ++i) {
        const SlicerNote key = out.notes[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && out.notes[static_cast<std::size_t>(j)].beat > key.beat) {
            out.notes[static_cast<std::size_t>(j + 1)] = out.notes[static_cast<std::size_t>(j)];
            --j;
        }
        out.notes[static_cast<std::size_t>(j + 1)] = key;
    }
}

} // namespace

void compileSlicer(const Params& params, const GrooveState& groove, int scene, const HeldNotes& held, double bpm,
                   SlicerPlan& out) noexcept CL_NONBLOCKING {
    const auto sceneIdx = static_cast<std::size_t>(std::clamp(scene, 0, kSceneCount - 1));
    const Pattern& pattern = groove.bank.scenes[sceneIdx];
    const int totalSteps = patternSteps(pattern);
    const double totalBeats = static_cast<double>(totalSteps) * 0.25;
    out.phraseBeats = totalBeats;
    out.noteCount = 0;

    const auto style = static_cast<SlicerStyle>(std::clamp(params.choice(ParamId::slicerStyle), 0, 4));
    if (style == SlicerStyle::clipSlice || held.count <= 0)
        return;

    const double swing = static_cast<double>(params[ParamId::swing]);
    const float dynamics = params[ParamId::dynamics];
    const double gateLen = static_cast<double>(std::clamp(params[ParamId::gateLen], 0.05f, 1.0f));
    const auto rateIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::seqRate), 0, 2));
    const double beatsPerMs = std::clamp(bpm, 20.0, 400.0) / 60000.0 * kSeqRateMultipliers[rateIdx];
    const double strumBeats = static_cast<double>(std::clamp(params[ParamId::strumMs], 0.0f, 45.0f)) * beatsPerMs;
    const int gateLength = std::clamp(static_cast<int>(groove.bank.slicerLength), 1, kMaxSlicerSteps);
    const auto& gate = groove.bank.slicerSteps;

    ResolvedBar kickBar;
    int resolvedBar = -1;
    int arpCursor = 0;

    for (int s16 = 0; s16 < totalSteps; ++s16) {
        const int gateIdx = s16 % gateLength;
        const Step& step = gate[static_cast<std::size_t>(gateIdx)];
        const double start = swingBeat(static_cast<double>(s16) * 0.25, swing);
        const double next = swingBeat(static_cast<double>(s16 + 1) * 0.25, swing);

        if (style == SlicerStyle::bassStab) {
            // The lowest held note follows the kick lane of the resolved bar; the rest play the gate.
            const int bar = s16 / kStepsPerBar;
            if (bar != resolvedBar) {
                resolveBar(pattern, groove.maps[sceneIdx], bar, {params[ParamId::density], dynamics, false}, kickBar);
                resolvedBar = bar;
            }
            const double at = std::clamp(start, 0.0, totalBeats - 0.03);
            if (kickBar.lanes[0][static_cast<std::size_t>(s16 % kStepsPerBar)].velocity > 0) {
                SlicerNote bass;
                bass.beat = at;
                bass.durBeats = std::min(0.16 + 0.34 * gateLen, totalBeats - at - 0.01);
                bass.note = static_cast<std::int8_t>(held.notes[0]);
                bass.voice = 0;
                bass.step = static_cast<std::int16_t>(s16 % kStepsPerBar);
                bass.velocity = applyDynamics(static_cast<int>(held.velocities[0]), dynamics);
                pushNote(out, bass);
            }
            if (step.on() && (s16 % kStepsPerBar) != 0 && held.count > 1) {
                const double stabDur = std::min(0.10 + 0.32 * gateLen, totalBeats - at - 0.01);
                for (int vi = 1; vi < held.count; ++vi) {
                    const double startAt =
                        std::min(totalBeats - 0.02, at + static_cast<double>(vi - 1) * strumBeats * 0.45);
                    SlicerNote stab;
                    stab.beat = startAt;
                    stab.durBeats = std::max(0.03, std::min(stabDur, totalBeats - startAt - 0.01));
                    stab.note = static_cast<std::int8_t>(held.notes[static_cast<std::size_t>(vi)]);
                    stab.voice = static_cast<std::int8_t>(std::min(vi, 7));
                    stab.step = static_cast<std::int16_t>(gateIdx);
                    const int base = static_cast<int>(held.velocities[static_cast<std::size_t>(vi)]) -
                                     (held.count > 3 && vi == 1 ? 8 : 0);
                    stab.velocity = applyDynamics(base, dynamics);
                    pushNote(out, stab);
                }
            }
            continue;
        }

        if (!step.on())
            continue;

        const double at = std::clamp(start + static_cast<double>(step.nudge) / 96.0, 0.0, totalBeats - 0.03);

        // The gate length scales with the distance to the next lit step.
        int gapSteps = 1;
        for (int g = 1; g < 8 && (s16 + g) < totalSteps; ++g) {
            if (gate[static_cast<std::size_t>((s16 + g) % gateLength)].on())
                break;
            ++gapSteps;
        }
        const double baseDur = std::min(static_cast<double>(gapSteps) * 0.25 * (0.18 + 0.80 * gateLen),
                                        std::max(0.04, totalBeats - at - 0.01));

        const int reps = std::clamp(static_cast<int>(step.ratchet), 1, 4);
        const double subStep = (next - start) / static_cast<double>(reps);

        if (style == SlicerStyle::breakArp) {
            const int voice = arpCursor % held.count;
            ++arpCursor;
            for (int r = 0; r < reps; ++r) {
                const double startAt = at + static_cast<double>(r) * subStep;
                if (startAt >= totalBeats - 0.02)
                    break;
                SlicerNote n;
                n.beat = startAt;
                n.durBeats = reps > 1 ? std::min(subStep * 0.78, totalBeats - startAt - 0.01)
                                      : std::min(baseDur, totalBeats - startAt - 0.01);
                n.note = static_cast<std::int8_t>(held.notes[static_cast<std::size_t>(voice)]);
                n.voice = static_cast<std::int8_t>(std::min(voice, 7));
                n.step = static_cast<std::int16_t>(gateIdx);
                n.velocity = applyDynamics(static_cast<int>(step.velocity), dynamics);
                pushNote(out, n);
            }
            continue;
        }

        // Stab Chop, or Strum / Flam: every held note. A flam on the step strums it.
        const bool strum = (style == SlicerStyle::strumFlam) || step.flam;
        const bool reverse = strum && ((s16 / 4) & 1) != 0;
        for (int r = 0; r < reps; ++r) {
            const double base = at + static_cast<double>(r) * subStep;
            if (base >= totalBeats - 0.02)
                break;
            for (int vi = 0; vi < held.count; ++vi) {
                const int order = reverse ? (held.count - 1 - vi) : vi;
                const double startAt =
                    std::min(totalBeats - 0.02, base + (strum ? static_cast<double>(order) * strumBeats : 0.0));
                const double dur = reps > 1 ? std::min(subStep * 0.78, totalBeats - startAt - 0.01)
                                            : std::min(baseDur, totalBeats - startAt - 0.01);
                const int velocity = (static_cast<int>(step.velocity) +
                                      static_cast<int>(held.velocities[static_cast<std::size_t>(vi)])) /
                                         2 +
                                     (strum && vi == held.count - 1 ? 8 : 0);
                SlicerNote n;
                n.beat = startAt;
                n.durBeats = std::max(0.03, dur);
                n.note = static_cast<std::int8_t>(held.notes[static_cast<std::size_t>(vi)]);
                n.voice = static_cast<std::int8_t>(std::min(vi, 7));
                n.step = static_cast<std::int16_t>(gateIdx);
                n.velocity = applyDynamics(velocity, dynamics);
                pushNote(out, n);
            }
        }
    }

    sortNotes(out);
}

bool SlicerInputs::sameAs(const SlicerInputs& other) const noexcept CL_NONBLOCKING {
    return valid && other.valid && grooveRevision == other.grooveRevision && scene == other.scene &&
           style == other.style && rateIdx == other.rateIdx && density == other.density && dynamics == other.dynamics &&
           swing == other.swing && gateLen == other.gateLen && strumMs == other.strumMs && bpm == other.bpm &&
           held.sameAs(other.held);
}

SlicerInputs slicerInputs(const Params& params, int scene, const HeldNotes& held, double bpm,
                          std::uint32_t grooveRevision) noexcept CL_NONBLOCKING {
    SlicerInputs in;
    in.valid = true;
    in.grooveRevision = grooveRevision;
    in.scene = scene;
    in.style = params.choice(ParamId::slicerStyle);
    in.rateIdx = params.choice(ParamId::seqRate);
    in.density = params[ParamId::density];
    in.dynamics = params[ParamId::dynamics];
    in.swing = params[ParamId::swing];
    in.gateLen = params[ParamId::gateLen];
    in.strumMs = params[ParamId::strumMs];
    in.bpm = bpm;
    in.held = held;
    return in;
}

} // namespace cl
