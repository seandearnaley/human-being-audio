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

// One lit gate step played as a chord: held notes `firstVoice` and up, `reps` times across the step. Each voice
// starts `order * spreadBeats` after the hit (`reverse` strums down), and the velocity blends the step's with the
// held note's; `accent` lifts the top note of a strum.
struct ChordHit {
    double at = 0.0;      // the step's start, nudged and clamped inside the phrase
    double subStep = 0.0; // the ratchet interval
    int reps = 1;
    double baseDur = 0.1; // the note length when the step does not ratchet
    int firstVoice = 0;
    double spreadBeats = 0.0;
    bool reverse = false;
    bool accent = false;
};

void emitChord(SlicerPlan& out, const HeldNotes& held, const Step& step, int gateIdx, const ChordHit& c,
               double totalBeats, float dynamics) noexcept CL_NONBLOCKING {
    for (int r = 0; r < c.reps; ++r) {
        const double base = c.at + static_cast<double>(r) * c.subStep;
        if (base >= totalBeats - 0.02)
            break;
        for (int vi = c.firstVoice; vi < held.count; ++vi) {
            const int order = c.reverse ? (held.count - 1 - vi) : (vi - c.firstVoice);
            const double startAt = std::min(totalBeats - 0.02, base + static_cast<double>(order) * c.spreadBeats);
            const double dur = c.reps > 1 ? std::min(c.subStep * 0.78, totalBeats - startAt - 0.01)
                                          : std::min(c.baseDur, totalBeats - startAt - 0.01);
            const int velocity =
                (static_cast<int>(step.velocity) + static_cast<int>(held.velocities[static_cast<std::size_t>(vi)])) /
                    2 +
                (c.accent && vi == held.count - 1 ? 8 : 0);
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
            // The lowest held note follows the kick lane of the resolved bar, with the kick step's ratchet and nudge.
            const int bar = s16 / kStepsPerBar;
            if (bar != resolvedBar) {
                resolveBar(pattern, groove.maps[sceneIdx], bar, {params[ParamId::density], dynamics, false}, kickBar);
                resolvedBar = bar;
            }
            const ResolvedStep& kick = kickBar.lanes[0][static_cast<std::size_t>(s16 % kStepsPerBar)];
            if (kick.velocity > 0) {
                const double kickAt =
                    std::clamp(start + static_cast<double>(kick.nudge) / 96.0, 0.0, totalBeats - 0.03);
                const int kickReps = std::clamp(static_cast<int>(kick.ratchet), 1, 4);
                const double kickSub = (next - start) / static_cast<double>(kickReps);
                for (int r = 0; r < kickReps; ++r) {
                    const double startAt = kickAt + static_cast<double>(r) * kickSub;
                    if (startAt >= totalBeats - 0.02)
                        break;
                    SlicerNote bass;
                    bass.beat = startAt;
                    bass.durBeats = kickReps > 1 ? std::min(kickSub * 0.78, totalBeats - startAt - 0.01)
                                                 : std::min(0.16 + 0.34 * gateLen, totalBeats - startAt - 0.01);
                    bass.note = static_cast<std::int8_t>(held.notes[0]);
                    bass.voice = 0;
                    bass.step = static_cast<std::int16_t>(s16 % kStepsPerBar);
                    bass.velocity = applyDynamics(static_cast<int>(held.velocities[0]), dynamics);
                    pushNote(out, bass);
                }
            }
        }

        if (!step.on())
            continue;

        const double at = std::clamp(start + static_cast<double>(step.nudge) / 96.0, 0.0, totalBeats - 0.03);
        const int reps = std::clamp(static_cast<int>(step.ratchet), 1, 4);
        const double subStep = (next - start) / static_cast<double>(reps);
        // Strum / Flam strums every step, and a flam strums its step: down on odd beats.
        const bool strum = (style == SlicerStyle::strumFlam) || step.flam;
        const bool reverse = strum && ((s16 / 4) & 1) != 0;

        if (style == SlicerStyle::bassStab) {
            // The other held notes play the gate, except on the first step of each bar, which is the bass's.
            if ((s16 % kStepsPerBar) == 0 || held.count <= 1)
                continue;
            ChordHit stab;
            stab.at = at;
            stab.subStep = subStep;
            stab.reps = reps;
            stab.baseDur = std::min(0.10 + 0.32 * gateLen, totalBeats - at - 0.01);
            stab.firstVoice = 1;
            stab.spreadBeats = step.flam ? strumBeats : strumBeats * 0.45; // a light spread unless flammed
            stab.reverse = reverse;
            stab.accent = step.flam;
            emitChord(out, held, step, gateIdx, stab, totalBeats, dynamics);
            continue;
        }

        // The gate length scales with the distance to the next lit step.
        int gapSteps = 1;
        for (int g = 1; g < 8 && (s16 + g) < totalSteps; ++g) {
            if (gate[static_cast<std::size_t>((s16 + g) % gateLength)].on())
                break;
            ++gapSteps;
        }
        const double baseDur = std::min(static_cast<double>(gapSteps) * 0.25 * (0.18 + 0.80 * gateLen),
                                        std::max(0.04, totalBeats - at - 0.01));

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

        // Stab Chop, or Strum / Flam: every held note.
        ChordHit chop;
        chop.at = at;
        chop.subStep = subStep;
        chop.reps = reps;
        chop.baseDur = baseDur;
        chop.spreadBeats = strum ? strumBeats : 0.0;
        chop.reverse = reverse;
        chop.accent = strum;
        emitChord(out, held, step, gateIdx, chop, totalBeats, dynamics);
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
