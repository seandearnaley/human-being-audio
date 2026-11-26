// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/phrase.h"

#include "styles.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

struct Rng {
    std::uint32_t state = 0x9e3779b9U;

    explicit constexpr Rng(std::uint32_t seed) noexcept CL_NONBLOCKING : state(hba::dsp::laneHash(seed ^ 0x9e3779b9U)) {
    }

    constexpr std::uint32_t nextU32() noexcept CL_NONBLOCKING {
        state = hba::dsp::laneHash(state + 0x9e3779b9U);
        return state;
    }

    constexpr double uniform() noexcept CL_NONBLOCKING { return static_cast<double>(nextU32()) / 4294967296.0; }

    constexpr int integer(int lo, int hi) noexcept CL_NONBLOCKING {
        if (hi <= lo)
            return lo;
        const auto span = static_cast<std::uint32_t>(hi - lo + 1);
        return lo + static_cast<int>(nextU32() % span);
    }
};

void pushNote(PhrasePlan& out, const CompiledNote& note) noexcept CL_NONBLOCKING {
    if (out.noteCount >= kMaxPhraseNotes)
        return;
    out.notes[static_cast<std::size_t>(out.noteCount++)] = note;
}

void sortPlanNotes(PhrasePlan& out) noexcept CL_NONBLOCKING {
    for (int i = 1; i < out.noteCount; ++i) {
        const CompiledNote key = out.notes[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && out.notes[static_cast<std::size_t>(j)].warpedBeats > key.warpedBeats) {
            out.notes[static_cast<std::size_t>(j + 1)] = out.notes[static_cast<std::size_t>(j)];
            --j;
        }
        out.notes[static_cast<std::size_t>(j + 1)] = key;
    }
}

std::uint8_t applyDynamics(int baseVel, bool accented, bool ghost, float dynamics, double progress,
                           Rng& rng) noexcept CL_NONBLOCKING {
    const double d = static_cast<double>(std::clamp(dynamics, 0.0f, 1.0f));
    double v = static_cast<double>(baseVel);
    if (accented)
        v += 16.0 * d;
    if (ghost)
        v = 34.0 + (v - 34.0) * (1.0 - 0.35 * d);
    // Gentle bar/phrase swell arc + subtle human velocity jitter (+-6)
    const double arc = (1.0 - 0.16 * d) + 0.16 * d * (0.86 + 0.28 * std::sin(3.141592653589793 * progress));
    const double jitter = (rng.uniform() * 2.0 - 1.0) * (6.0 * d);
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v * arc + jitter)), 18, 127));
}

bool laneHasAnySolo(const SceneData& sc) noexcept CL_NONBLOCKING {
    for (const auto& l : sc.lanes) {
        if (l.solo)
            return true;
    }
    return false;
}

bool laneAudible(const SceneData& sc, int lane, bool anySolo) noexcept CL_NONBLOCKING {
    const auto& l = sc.lanes[static_cast<std::size_t>(lane)];
    if (anySolo)
        return l.solo && !l.muted;
    return !l.muted;
}

// Compute base generated hit & base velocity for a given drum (lane, stepIndex) before user step overrides.
void resolveAutoDrumStep(DrumStyle style, const styles::StyleKit& kit, int lane, int stepIdx, int laneLen,
                         float complexity, float ghosts, int variation, const LaneConfig& laneCfg,
                         bool& outHit, int& outVel, bool& outAccent, bool& outGhost) noexcept CL_NONBLOCKING {
    outHit = false;
    outVel = 96;
    outAccent = false;
    outGhost = (lane == 2);

    const std::uint32_t bit = 1U << static_cast<unsigned>(stepIdx & 31);
    const int effVar = laneCfg.locked && laneCfg.lockedVar > 0 ? static_cast<int>(laneCfg.lockedVar) : variation;

    // 1. Explicit Euclidean override on this lane or Euclidean Lab style
    if (laneCfg.euclidHits > 0 || style == DrumStyle::euclidean) {
        const int hits =
            laneCfg.euclidHits > 0 ? static_cast<int>(laneCfg.euclidHits) : styles::euclideanDefaultHits(lane, complexity);
        const int rot = laneCfg.euclidHits > 0 ? static_cast<int>(laneCfg.euclidRot)
                                               : styles::euclideanDefaultRot(lane, effVar);
        const std::uint32_t em = styles::euclidMask(hits, laneLen, rot);
        outHit = (em & bit) != 0U;
        if (lane == 2 && ghosts < 0.08f)
            outHit = false;
        outAccent = ((kit.accentMask[static_cast<std::size_t>(lane)] & bit) != 0U) || (stepIdx == 0);
        outVel = (lane == 0) ? 116 : (lane == 1 ? 114 : (lane == 2 ? 62 : (lane == 4 ? 86 : 98)));
        return;
    }

    const auto li = static_cast<std::size_t>(lane);
    // When a lane is shortened to a polymetric length (< 16), fold upper bar hits modulo laneLen
    // (or Euclidean fallback if empty) so short 5/7/11-step polymeters are always musically alive!
    auto foldMask = [&](std::uint32_t m) noexcept CL_NONBLOCKING -> std::uint32_t {
        if (laneLen >= 16 || m == 0U)
            return m;
        std::uint32_t folded = 0U;
        for (int b = 0; b < 32; ++b) {
            if ((m & (1U << static_cast<unsigned>(b))) != 0U)
                folded |= (1U << static_cast<unsigned>(b % laneLen));
        }
        return folded;
    };
    const std::uint32_t effCore = foldMask(kit.coreMask[li]);
    std::uint32_t effGroove = foldMask(kit.grooveMask[li]);
    const std::uint32_t effSpice = foldMask(kit.spiceMask[li]);
    if (laneLen < 16 && effCore == 0U && effGroove == 0U)
        effGroove = styles::euclidMask(std::max(2, (laneLen * 3 + 4) / 8), laneLen, effVar % laneLen);

    const bool inCore = (effCore & bit) != 0U;
    const bool inGroove = (effGroove & bit) != 0U;
    const bool inSpice = (effSpice & bit) != 0U;
    outAccent = (foldMask(kit.accentMask[li]) & bit) != 0U;

    // Deterministic per-cell hash for smooth density scaling & tasteful variation swaps
    const std::uint32_t cellHash =
        hba::dsp::laneHash(static_cast<std::uint32_t>(effVar) * 2654435761U ^
                           static_cast<std::uint32_t>(lane) * 2246822519U ^
                           static_cast<std::uint32_t>(stepIdx) * 3266489917U ^
                           (static_cast<std::uint32_t>(style) << 18U));
    const double u = static_cast<double>(cellHash) / 4294967296.0;

    // Curated variation spice (variation == 1 is the canonical signature groove; 2..64 add tasteful twists)
    bool varAdded = false;
    bool varDropped = false;
    if (effVar > 1) {
        if (lane == 0 && stepIdx != 0 && stepIdx != 16) {
            if (inSpice && u < 0.35)
                varAdded = true;
            if (!inCore && inGroove && u > 0.78)
                varDropped = true;
        } else if (lane == 2 || lane == 6 || lane == 7) {
            if (inSpice && u < 0.48)
                varAdded = true;
            if (!inCore && inGroove && u > 0.72)
                varDropped = true;
        } else if (lane == 4 || lane == 5) {
            if (inSpice && u < 0.35)
                varAdded = true;
        }
    }

    if (lane == 2) {
        // Ghost lane is directly governed by the GHOSTS macro (0% = silent, 100% = full ghost tapestry)
        if (ghosts < 0.04f) {
            outHit = false;
        } else if (inCore) {
            outHit = ghosts >= 0.22f || u < static_cast<double>(ghosts) * 4.0;
        } else if (inGroove || varAdded) {
            outHit = !varDropped && (static_cast<double>(ghosts) > 0.32 + 0.50 * u);
        } else if (inSpice) {
            outHit = static_cast<double>(ghosts) > 0.65 + 0.32 * u;
        }
        outVel = outAccent ? 76 : 56;
        return;
    }

    // Core lanes: smooth progression from stripped Kick+Snare backbone (Complex=0) -> full groove (0.5) -> rich spice (1.0)
    if (lane == 0 || lane == 1) {
        if (inCore)
            outHit = true;
        else if (inGroove || varAdded)
            outHit = !varDropped && (static_cast<double>(complexity) > 0.22 + 0.40 * u);
        else if (inSpice)
            outHit = static_cast<double>(complexity) > 0.64 + 0.34 * u;
        outVel = (lane == 0) ? (stepIdx == 0 || stepIdx == 16 ? 122 : 110) : (outAccent ? 118 : 104);
    } else if (lane == 3) {
        // Clap / Rim layer
        if (inCore)
            outHit = complexity >= 0.16f;
        else if (inGroove || varAdded)
            outHit = !varDropped && (static_cast<double>(complexity) > 0.34 + 0.38 * u);
        else if (inSpice)
            outHit = static_cast<double>(complexity) > 0.68 + 0.30 * u;
        outVel = outAccent ? 108 : 90;
    } else if (lane == 4 || lane == 5) {
        // Closed & Open Hats
        if (inCore)
            outHit = complexity >= 0.08f || ((stepIdx & 3) == 0);
        else if (inGroove || varAdded)
            outHit = !varDropped && (static_cast<double>(complexity) > 0.26 + 0.42 * u);
        else if (inSpice)
            outHit = static_cast<double>(complexity) > 0.60 + 0.36 * u;
        outVel = (lane == 5) ? (outAccent ? 104 : 92) : (outAccent ? 98 : ((stepIdx & 1) == 0 ? 86 : 64));
    } else {
        // Perc A (6) & Perc B / Crash (7)
        if (inCore)
            outHit = complexity >= 0.24f || (lane == 7 && stepIdx == 0 && complexity >= 0.12f);
        else if (inGroove || varAdded)
            outHit = !varDropped && (static_cast<double>(complexity) > 0.40 + 0.38 * u);
        else if (inSpice)
            outHit = static_cast<double>(complexity) > 0.66 + 0.32 * u;
        outVel = (lane == 7 && stepIdx == 0) ? 106 : (outAccent ? 100 : 82);
    }
}

} // namespace

double warpBeat(double beat, double grooveNorm, Attractor pocket) noexcept CL_NONBLOCKING {
    const double g = std::clamp(grooveNorm, -1.0, 1.0);
    if (std::abs(g) < 1.0e-5)
        return beat;

    if (pocket == Attractor::mpcSwing) {
        // Strictly monotonic MPC / UKG 16th-note swing:
        // Even 16ths (0, 0.5, 1.0, ...) stay locked; odd 16ths (0.25, 0.75, ...) shift by up to +-0.095 beats
        // (+0.095 => 0.345 / 0.50 = 69% classic MPC/UKG swing). Linear interpolation preserves strict monotonicity.
        const double pairIdx = std::floor(beat / 0.5);
        const double local = beat - pairIdx * 0.5; // [0, 0.5)
        const double mid = 0.25 + 0.095 * g;       // in [0.155, 0.345] -> strictly inside (0, 0.5)!
        double warpedLocal = 0.0;
        if (local <= 0.25) {
            warpedLocal = (local / 0.25) * mid;
        } else {
            warpedLocal = mid + ((local - 0.25) / 0.25) * (0.5 - mid);
        }
        return pairIdx * 0.5 + warpedLocal;
    }

    const double barIdx = std::floor(beat / 4.0);
    const double u = (beat - barIdx * 4.0) * 0.25; // [0, 1)

    double k = 4.0;
    double u0 = 0.0;
    switch (pocket) {
    case Attractor::mpcSwing:
    case Attractor::downbeat:
        k = 2.0;
        u0 = 0.0;
        break;
    case Attractor::backbeat:
        k = 2.0;
        u0 = 0.25;
        break;
    case Attractor::offbeat16:
        k = 4.0;
        u0 = 0.0;
        break;
    case Attractor::triplet:
        k = 3.0;
        u0 = 0.0;
        break;
    }

    // |alpha| <= 0.40 < 1.0 guarantees tau'(u) = 1 + alpha * cos(...) >= 0.60 > 0 everywhere.
    const double alpha = 0.40 * g;
    const double warpedU = u + (alpha / (kTwoPi * k)) * std::sin(kTwoPi * k * (u - u0));
    const double clampedU = std::clamp(warpedU, 0.0, 0.9999);
    return barIdx * 4.0 + clampedU * 4.0;
}

std::uint32_t phraseSignature(const Params& params, int sceneIdx, const HeldNotes& held, const PatternBank& bank,
                              double bpm) noexcept CL_NONBLOCKING {
    std::uint32_t h = hba::dsp::laneHash(static_cast<std::uint32_t>(sceneIdx + 1) * 0x517cc1b7U);
    const auto mix = [&](std::uint32_t v) noexcept CL_NONBLOCKING {
        h = hba::dsp::laneHash(h ^ (v * 0x9e3779b9U + 0x27d4eb2dU));
    };
    const auto q = [](float f) noexcept CL_NONBLOCKING {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(f * 1000.0f)));
    };

    mix(static_cast<std::uint32_t>(params.choice(ParamId::style)));
    mix(q(params[ParamId::gravity]));
    mix(q(params[ParamId::ghosts]));
    mix(q(params[ParamId::chop]));
    mix(q(params[ParamId::ratchet]));
    mix(q(params[ParamId::complexity]));
    mix(q(params[ParamId::fills]));
    mix(q(params[ParamId::dynamics]));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::engineMode)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::noteMap)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::slicerStyle)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::phraseBars)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::attractor)));
    mix(static_cast<std::uint32_t>(params.choice(ParamId::fillType)));
    mix(static_cast<std::uint32_t>(params.integer(ParamId::variation)));
    mix(q(params[ParamId::strumMs]));
    mix(q(params[ParamId::gateLen]));
    mix(static_cast<std::uint32_t>(std::lround(bpm * 10.0)));

    const auto mode = static_cast<EngineMode>(std::clamp(params.choice(ParamId::engineMode), 0, 1));
    if (mode == EngineMode::slicer) {
        mix(static_cast<std::uint32_t>(held.count));
        mix(static_cast<std::uint32_t>(held.lastPressed));
        for (int i = 0; i < held.count; ++i) {
            mix(static_cast<std::uint32_t>(held.notes[static_cast<std::size_t>(i)]) |
                (static_cast<std::uint32_t>(held.velocities[static_cast<std::size_t>(i)]) << 8U));
        }
        mix(static_cast<std::uint32_t>(bank.slicerLength));
        for (int i = 0; i < kMaxLaneSteps; ++i) {
            const auto& st = bank.slicerSteps[static_cast<std::size_t>(i)];
            if (st.hasAny()) {
                mix(static_cast<std::uint32_t>(i) |
                    (static_cast<std::uint32_t>(static_cast<int>(st.state) + 2) << 6U) |
                    (static_cast<std::uint32_t>(st.velOffset + 64) << 9U) |
                    (static_cast<std::uint32_t>(st.ratchet) << 17U) |
                    (static_cast<std::uint32_t>(st.microNudge + 32) << 21U) | (st.flam ? (1U << 28U) : 0U));
            }
        }
    } else {
        const SceneData sc = effectiveSceneData(bank, sceneIdx);
        mix((sc.customized ? 1U : 0U) | (static_cast<std::uint32_t>(sc.styleOverride + 2) << 2U) |
            (static_cast<std::uint32_t>(sc.fillTypeOverride + 2) << 7U) |
            (static_cast<std::uint32_t>(sc.variationOffset + 64) << 12U));
        mix(q(sc.complexityBias));
        mix(q(sc.chopBias));
        mix(q(sc.ghostBias));
        for (int l = 0; l < kDrumLaneCount; ++l) {
            const auto& lane = sc.lanes[static_cast<std::size_t>(l)];
            mix(static_cast<std::uint32_t>(lane.stepLength) | (static_cast<std::uint32_t>(lane.midiNote) << 6U) |
                (lane.muted ? (1U << 14U) : 0U) | (lane.solo ? (1U << 15U) : 0U) | (lane.locked ? (1U << 16U) : 0U) |
                (static_cast<std::uint32_t>(lane.euclidHits) << 17U) |
                (static_cast<std::uint32_t>(lane.euclidRot) << 23U) |
                (static_cast<std::uint32_t>(lane.lockedVar) << 28U));
            for (int stIdx = 0; stIdx < kMaxLaneSteps; ++stIdx) {
                const auto& st = lane.steps[static_cast<std::size_t>(stIdx)];
                if (st.hasAny()) {
                    mix(static_cast<std::uint32_t>((l << 5) | stIdx) |
                        (static_cast<std::uint32_t>(static_cast<int>(st.state) + 2) << 9U) |
                        (static_cast<std::uint32_t>(st.velOffset + 64) << 12U) |
                        (static_cast<std::uint32_t>(st.ratchet) << 20U) |
                        (static_cast<std::uint32_t>(st.microNudge + 32) << 24U) | (st.flam ? (1U << 30U) : 0U));
                }
            }
        }
    }
    return h;
}

void appendDrumFill(PhrasePlan& out, double fillStart, double fillEnd, double totalBeats, FillType fillType,
                    double groove, Attractor pocket, float dynamics, float ratchetAmt, int variation, NoteMap noteMap,
                    const SceneData& scene, std::uint32_t salt) noexcept CL_NONBLOCKING {
    const double len = std::max(0.25, fillEnd - fillStart);
    const bool anySolo = laneHasAnySolo(scene);
    Rng rng(salt ^ static_cast<std::uint32_t>(variation * 7919));

    const auto emit = [&](double b, int lane, int vel, double dur, bool rat, bool flam) noexcept CL_NONBLOCKING {
        if (!laneAudible(scene, lane, anySolo))
            return;
        if (b < fillStart - 1e-6 || b >= fillEnd - 1e-5)
            return;
        const double warped = std::clamp(warpBeat(b, groove, pocket), 0.0, totalBeats - 0.03);
        const double prog = std::clamp((b - fillStart) / len, 0.0, 1.0);
        CompiledNote n;
        n.startBeats = b;
        n.warpedBeats = warped;
        n.durationBeats = std::min(dur, std::max(0.03, totalBeats - warped - 0.01));
        n.noteNumber = effectiveLaneMidiNote(noteMap, lane, scene.lanes[static_cast<std::size_t>(lane)]);
        n.lane = static_cast<std::int8_t>(lane);
        n.stepIndex = static_cast<std::int16_t>(static_cast<int>(std::llround(b * 4.0)) & 31);
        n.velocity = applyDynamics(vel, prog > 0.78, lane == 2, dynamics, prog, rng);
        n.channel = 1;
        n.isGhost = (lane == 2);
        n.isRatchet = rat;
        n.isFill = true;
        n.isFlam = flam;
        pushNote(out, n);
    };

    switch (fillType) {
    case FillType::airCut: {
        // Clean drop-out breath before beat 1, with a crisp snare+crash pickup on the final 16th!
        emit(fillStart, 1, 114, 0.22, false, false);
        emit(fillStart, 5, 96, 0.35, false, false);
        const double pickup = fillEnd - 0.25;
        if (pickup > fillStart + 0.30) {
            emit(pickup, 2, 74, 0.14, false, true);
            emit(pickup - 0.05, 2, 48, 0.08, false, true);
            if (fillEnd - fillStart >= 1.5)
                emit(fillEnd - 0.5, 0, 112, 0.20, false, false);
        }
        break;
    }
    case FillType::halfDrop: {
        // Heavy halftime flam + snare/clap smack, breathing space, then punchy double-kick pickup
        emit(fillStart, 0, 122, 0.24, false, false);
        const double mid = fillStart + std::floor(len * 0.5 * 4.0) * 0.25;
        emit(mid - 0.05, 2, 62, 0.10, false, true);
        emit(mid, 1, 124, 0.28, false, true);
        emit(mid, 3, 112, 0.24, false, false);
        if (fillEnd - mid >= 0.75) {
            emit(fillEnd - 0.50, 0, 110, 0.18, false, false);
            emit(fillEnd - 0.25, 1, 116, 0.18, false, false);
            emit(fillEnd - 0.25, 5, 96, 0.24, false, false);
        }
        break;
    }
    case FillType::tomRun: {
        // Descending Perc B -> Perc A -> Snare -> Kick cascade
        const double step = ratchetAmt > 0.55f ? 0.1875 : 0.25;
        const std::array<int, 6> seq{7, 6, 6, 1, 3, 0};
        int idx = 0;
        for (double b = fillStart; b < fillEnd - 0.04; b += step) {
            const double prog = (b - fillStart) / len;
            const int lane = seq[static_cast<std::size_t>(std::min(idx * 6 / std::max(1, static_cast<int>(len / step)), 5))];
            const int vel = 78 + static_cast<int>(44.0 * prog);
            emit(b, lane, vel, step * 0.82, step < 0.22, false);
            if (idx == 0)
                emit(b, 0, 116, 0.22, false, false);
            ++idx;
        }
        break;
    }
    case FillType::drillChop: {
        // Bouncing-ball geometric BBCut/Drill snare+ghost+perc stutter
        constexpr std::array<double, 6> kSteps{0.25, 0.25, 0.1875, 0.1875, 0.125, 0.125};
        double b = fillStart;
        int idx = 0;
        while (b < fillEnd - 0.04) {
            const double prog = (b - fillStart) / len;
            const double step = kSteps[static_cast<std::size_t>(idx) % kSteps.size()];
            const int lane = (idx % 4 == 0) ? 1 : ((idx % 3 == 1) ? 2 : (idx % 5 == 0 ? 0 : 6));
            const int vel = 62 + static_cast<int>(60.0 * prog);
            emit(b, lane, vel, step * 0.78, step < 0.20, false);
            if ((idx % 4) == 0)
                emit(b, 4, 82, 0.12, false, false);
            b += step;
            ++idx;
        }
        break;
    }
    case FillType::autoFill:
    case FillType::snareRoll: {
        // Classic crescendo snare + ghost roll accelerating to 32nds at the crest
        double b = fillStart;
        int idx = 0;
        while (b < fillEnd - 0.04) {
            const double prog = (b - fillStart) / len;
            const double step = (prog > 0.62 && ratchetAmt > 0.30f) ? 0.125 : 0.25;
            const int lane = (prog < 0.35 && (idx & 1)) ? 2 : 1;
            const int vel = 60 + static_cast<int>(64.0 * prog);
            emit(b, lane, vel, step * 0.78, step <= 0.15, false);
            if (prog > 0.78 && (idx % 2 == 0))
                emit(b, 3, vel - 8, step * 0.75, true, false);
            if (idx == 0)
                emit(b, 0, 118, 0.22, false, false);
            b += step;
            ++idx;
        }
        break;
    }
    }
}

void compilePhrase(const Params& params, int sceneIdx, std::int64_t phraseIndex, const HeldNotes& held, PhrasePlan& out,
                   const PatternBank& bank, double bpm) noexcept CL_NONBLOCKING {
    out.phraseIndex = phraseIndex;
    out.activeScene = std::clamp(sceneIdx, 0, kSceneCount - 1);
    out.noteCount = 0;

    const auto barsIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::phraseBars), 0, 2));
    const int bars = kPhraseBarCounts[barsIdx];
    const double totalBeats = static_cast<double>(bars * 4);
    const int totalSteps16 = bars * 16;
    out.phraseBeats = totalBeats;
    out.fillStartBeat = totalBeats;
    out.signature = phraseSignature(params, out.activeScene, held, bank, bpm);

    const auto mode = static_cast<EngineMode>(std::clamp(params.choice(ParamId::engineMode), 0, 1));
    const auto pocket = static_cast<Attractor>(std::clamp(params.choice(ParamId::attractor), 0, 4));
    const auto noteMap = static_cast<NoteMap>(std::clamp(params.choice(ParamId::noteMap), 0, kNoteMapCount - 1));
    const double groove = static_cast<double>(std::clamp(params[ParamId::gravity], -1.0f, 1.0f));
    const float ghostsGlobal = std::clamp(params[ParamId::ghosts], 0.0f, 1.0f);
    const float chopGlobal = std::clamp(params[ParamId::chop], 0.0f, 1.0f);
    const float ratchetAmt = std::clamp(params[ParamId::ratchet], 0.0f, 1.0f);
    const float compGlobal = std::clamp(params[ParamId::complexity], 0.0f, 1.0f);
    const float fillsAmt = std::clamp(params[ParamId::fills], 0.0f, 1.0f);
    const float dynamics = std::clamp(params[ParamId::dynamics], 0.0f, 1.0f);
    const float gateLen = std::clamp(params[ParamId::gateLen], 0.05f, 1.0f);
    const double strumMs = static_cast<double>(std::clamp(params[ParamId::strumMs], 0.0f, 45.0f));
    const double safeBpm = std::clamp(bpm, 20.0, 400.0);
    const double msToBeats = safeBpm / 60000.0;

    const SceneData sc = effectiveSceneData(bank, out.activeScene);
    const int effStyleIdx =
        sc.styleOverride >= 0 ? std::clamp(static_cast<int>(sc.styleOverride), 0, kDrumStyleCount - 1)
                              : std::clamp(params.choice(ParamId::style), 0, kDrumStyleCount - 1);
    const auto style = static_cast<DrumStyle>(effStyleIdx);
    const styles::StyleKit& kit = styles::styleKit(style);

    const float complexity = std::clamp(compGlobal + sc.complexityBias, 0.0f, 1.0f);
    const float chopAmt = std::clamp(chopGlobal + sc.chopBias, 0.0f, 1.0f);
    const float ghosts = std::clamp(ghostsGlobal + sc.ghostBias, 0.0f, 1.0f);
    const int variation = std::clamp(params.integer(ParamId::variation) + static_cast<int>(sc.variationOffset), 1, 64);

    // Resolve lane metadata & visual step velocities for all 8 drum lanes (always updated for UI)
    for (int l = 0; l < kDrumLaneCount; ++l) {
        const auto li = static_cast<std::size_t>(l);
        const auto& laneCfg = sc.lanes[li];
        const int len = laneCfg.stepLength > 0 ? std::clamp(static_cast<int>(laneCfg.stepLength), 1, kMaxLaneSteps)
                                               : static_cast<int>(kit.defaultLen[li]);
        out.laneLengths[li] = static_cast<std::uint8_t>(len);
        out.laneMidiNotes[li] = effectiveLaneMidiNote(noteMap, l, laneCfg);
        out.gridVel[li].fill(0);
        out.gridRatchet[li].fill(0);
        out.gridFlam[li].fill(false);

        for (int st = 0; st < len; ++st) {
            bool hit = false;
            int vel = 96;
            bool acc = false;
            bool isGh = (l == 2);
            resolveAutoDrumStep(style, kit, l, st, len, complexity, ghosts, variation, laneCfg, hit, vel, acc, isGh);
            const auto& edit = laneCfg.steps[static_cast<std::size_t>(st)];
            if (edit.state == StepState::forceOn)
                hit = true;
            else if (edit.state == StepState::forceOff)
                hit = false;

            if (hit) {
                const int effVel = std::clamp(vel + (acc ? 10 : 0) + static_cast<int>(edit.velOffset), 18, 127);
                out.gridVel[li][static_cast<std::size_t>(st)] = static_cast<std::uint8_t>(effVel);
                out.gridRatchet[li][static_cast<std::size_t>(st)] = edit.ratchet;
                out.gridFlam[li][static_cast<std::size_t>(st)] = edit.flam;
            }
        }
    }

    // Resolve visual slicer step timeline (always populated cleanly for UI)
    out.slicerLength = std::clamp(bank.slicerLength, std::uint8_t{1}, static_cast<std::uint8_t>(kMaxLaneSteps));
    out.slicerVel.fill(0);
    out.slicerRatchet.fill(0);
    out.slicerFlam.fill(false);
    for (int st = 0; st < static_cast<int>(out.slicerLength); ++st) {
        const std::uint32_t bit = 1U << static_cast<unsigned>(st & 31);
        bool hit = (kit.slicerStabMask & bit) != 0U;
        const bool acc = (kit.slicerAccentMask & bit) != 0U;
        if (!hit && complexity > 0.65f) {
            const std::uint32_t h =
                hba::dsp::laneHash(static_cast<std::uint32_t>(variation) * 919U + static_cast<std::uint32_t>(st) * 313U);
            if ((static_cast<double>(h) / 4294967296.0) < static_cast<double>(complexity - 0.65f) * 1.1)
                hit = true;
        }
        if (hit && !acc && complexity < 0.35f && (st & 3) != 0)
            hit = false;
        const auto& ed = bank.slicerSteps[static_cast<std::size_t>(st)];
        if (ed.state == StepState::forceOn)
            hit = true;
        else if (ed.state == StepState::forceOff)
            hit = false;
        if (hit) {
            const int baseV = acc ? 116 : 92;
            out.slicerVel[static_cast<std::size_t>(st)] =
                static_cast<std::uint8_t>(std::clamp(baseV + static_cast<int>(ed.velOffset), 18, 127));
            out.slicerRatchet[static_cast<std::size_t>(st)] = ed.ratchet;
            out.slicerFlam[static_cast<std::size_t>(st)] = ed.flam;
        }
    }

    Rng rng(hba::dsp::laneHash(static_cast<std::uint32_t>(variation) * 2654435761U ^
                               static_cast<std::uint32_t>(effStyleIdx << 20U) ^
                               static_cast<std::uint32_t>(out.activeScene << 16U)));

    // =========================================================================
    // MODE 1: 8-LANE POLYMETRIC BREAKBEAT & PERCUSSION ENGINE
    // =========================================================================
    if (mode == EngineMode::drum) {
        // Fill window at the end of the phrase (driven smoothly by FILLS macro)
        double fillBeats = 0.0;
        if (fillsAmt >= 0.08f) {
            if (fillsAmt < 0.36f)
                fillBeats = 1.0;
            else if (fillsAmt < 0.72f)
                fillBeats = 2.0;
            else
                fillBeats = 3.0;
            fillBeats = std::min(fillBeats, totalBeats * 0.375);
        }
        const double fillStart = totalBeats - fillBeats;
        out.fillStartBeat = fillStart;

        const bool anySolo = laneHasAnySolo(sc);

        // Build BBCut / WarpCut beat-level slice remapping table across all bars (Bar 1 stays anchored!)
        // Each 1/2-beat (2 steps) slot can either play linearly or repeat/swap an earlier beat slice!
        std::array<int, 64> sliceSourceHalfBeat{};
        std::array<bool, 64> sliceIsChop{};
        const int totalHalfBeats = bars * 8;
        for (int hb = 0; hb < totalHalfBeats && hb < 64; ++hb) {
            sliceSourceHalfBeat[static_cast<std::size_t>(hb)] = hb;
            sliceIsChop[static_cast<std::size_t>(hb)] = false;
        }
        if (chopAmt > 0.06f) {
            for (int hb = 6; hb < totalHalfBeats && hb < 64; ++hb) {
                const double hbBeat = static_cast<double>(hb) * 0.5;
                if (hbBeat >= fillStart - 1e-5)
                    break;
                // Keep every Bar downbeat (hb % 8 == 0) solid at low/medium chop; allow wild swaps > 0.75
                if ((hb % 8) == 0 && chopAmt < 0.78f)
                    continue;
                if (rng.uniform() < static_cast<double>(chopAmt) * 0.42) {
                    const int barStartHb = (hb / 8) * 8;
                    const int choice = rng.integer(0, 3);
                    int src = hb;
                    if (choice == 0 && hb > barStartHb)
                        src = hb - 1; // Classic BBCut 1/2-beat stutter repeat!
                    else if (choice == 1)
                        src = barStartHb + 2; // Repeat beat 2 snare slice!
                    else if (choice == 2)
                        src = barStartHb; // Kick downbeat anticipation repeat!
                    else if (hb >= 2)
                        src = hb - 2; // 1-beat bounce repeat
                    sliceSourceHalfBeat[static_cast<std::size_t>(hb)] = src;
                    sliceIsChop[static_cast<std::size_t>(hb)] = (src != hb);
                }
            }
        }

        const double flamOffsetBeats = std::clamp(strumMs, 8.0, 26.0) * msToBeats;

        for (int s16 = 0; s16 < totalSteps16; ++s16) {
            const double rawBeat = static_cast<double>(s16) * 0.25;
            if (rawBeat >= fillStart - 1e-5)
                break;

            const int hb = std::min(s16 / 2, 63);
            const int srcS16 = sliceSourceHalfBeat[static_cast<std::size_t>(hb)] * 2 + (s16 & 1);
            const bool isChopped = sliceIsChop[static_cast<std::size_t>(hb)];
            const double prog = rawBeat / totalBeats;

            // Check Open Hat (lane 5) vs Closed Hat (lane 4) choke on the same step
            const int ohLen = static_cast<int>(out.laneLengths[5]);
            const int ohStep = srcS16 % ohLen;
            const bool ohActive = laneAudible(sc, 5, anySolo) && (out.gridVel[5][static_cast<std::size_t>(ohStep)] > 0);

            for (int l = 0; l < kDrumLaneCount; ++l) {
                if (!laneAudible(sc, l, anySolo))
                    continue;
                const auto li = static_cast<std::size_t>(l);
                const auto& laneCfg = sc.lanes[li];
                const int laneLen = static_cast<int>(out.laneLengths[li]);
                const int stIdx = srcS16 % laneLen;

                // Open Hat naturally supersedes Closed Hat on identical steps unless user painted CH explicitly
                if (l == 4 && ohActive && laneCfg.steps[static_cast<std::size_t>(stIdx)].state != StepState::forceOn)
                    continue;

                const std::uint8_t baseGridV = out.gridVel[li][static_cast<std::size_t>(stIdx)];
                if (baseGridV == 0)
                    continue;

                const auto& edit = laneCfg.steps[static_cast<std::size_t>(stIdx)];
                const double pocketBeats = kit.pocketMs[li] * std::abs(groove) * msToBeats;
                const double nudgeBeats = static_cast<double>(edit.microNudge) / 96.0;
                const double warped = std::clamp(warpBeat(rawBeat, groove, pocket) + pocketBeats + nudgeBeats, 0.0,
                                                 totalBeats - 0.03);

                // Determine ratchet bursts: explicit user step ratchet OR probabilistic musical ratchet on Hats/Perc/Ghosts
                int rc = static_cast<int>(edit.ratchet);
                if (rc == 0 && ratchetAmt > 0.14f) {
                    const bool eligibleLane = (l == 4 || l == 2 || l == 6 || (l == 1 && isChopped));
                    const bool eligiblePos = ((s16 & 3) == 3) || ((s16 & 7) == 6) || isChopped;
                    if (eligibleLane && eligiblePos && rng.uniform() < static_cast<double>(ratchetAmt) * 0.36) {
                        rc = (ratchetAmt > 0.68f && rng.uniform() < 0.45) ? 3 : 2;
                    }
                }

                const bool doFlam = edit.flam || ((l == 1 || l == 3) && ghosts > 0.72f && ((s16 % 16) == 12) &&
                                                  rng.uniform() < static_cast<double>(ghosts - 0.65f) * 0.9);
                const std::int8_t midiNote = out.laneMidiNotes[li];
                const bool accented = ((kit.accentMask[li] & (1U << static_cast<unsigned>(stIdx & 31))) != 0U);
                const std::uint8_t finalVel =
                    applyDynamics(static_cast<int>(baseGridV), accented, l == 2, dynamics, prog, rng);

                if (doFlam && warped > flamOffsetBeats + 0.005) {
                    CompiledNote fl;
                    fl.startBeats = std::max(0.0, rawBeat - flamOffsetBeats);
                    fl.warpedBeats = std::max(0.0, warped - flamOffsetBeats);
                    fl.durationBeats = 0.08;
                    fl.noteNumber = (l == 1) ? out.laneMidiNotes[2] : midiNote;
                    fl.lane = static_cast<std::int8_t>(l);
                    fl.stepIndex = static_cast<std::int16_t>(stIdx);
                    fl.velocity = static_cast<std::uint8_t>(std::max(24, static_cast<int>(finalVel) - 42));
                    fl.channel = 1;
                    fl.isGhost = true;
                    fl.isFlam = true;
                    fl.isChop = isChopped;
                    pushNote(out, fl);
                }

                if (rc >= 2) {
                    const double subStep = 0.25 / static_cast<double>(rc);
                    for (int r = 0; r < rc; ++r) {
                        const double rBeat = warped + static_cast<double>(r) * subStep;
                        if (rBeat >= totalBeats - 0.02)
                            break;
                        const double ramp = static_cast<double>(r) / static_cast<double>(rc - 1);
                        const int rv = std::clamp(static_cast<int>(finalVel) - 22 + static_cast<int>(26.0 * ramp), 24, 127);
                        CompiledNote sub;
                        sub.startBeats = rawBeat + static_cast<double>(r) * subStep;
                        sub.warpedBeats = rBeat;
                        sub.durationBeats = std::min(subStep * 0.78, std::max(0.02, totalBeats - rBeat - 0.01));
                        sub.noteNumber = midiNote;
                        sub.lane = static_cast<std::int8_t>(l);
                        sub.stepIndex = static_cast<std::int16_t>(stIdx);
                        sub.velocity = static_cast<std::uint8_t>(rv);
                        sub.channel = 1;
                        sub.isGhost = (l == 2);
                        sub.isRatchet = true;
                        sub.isChop = isChopped;
                        pushNote(out, sub);
                    }
                } else {
                    const double dur = (l == 5 || l == 7) ? 0.36 : (0.12 + 0.14 * static_cast<double>(gateLen));
                    CompiledNote n;
                    n.startBeats = rawBeat;
                    n.warpedBeats = warped;
                    n.durationBeats = std::min(dur, std::max(0.03, totalBeats - warped - 0.01));
                    n.noteNumber = midiNote;
                    n.lane = static_cast<std::int8_t>(l);
                    n.stepIndex = static_cast<std::int16_t>(stIdx);
                    n.velocity = finalVel;
                    n.channel = 1;
                    n.isGhost = (l == 2);
                    n.isChop = isChopped;
                    n.isFlam = doFlam;
                    pushNote(out, n);
                }
            }
        }

        // Append musical Fill archetype onto the turnaround window
        if (fillBeats >= 0.5) {
            const int reqFill = sc.fillTypeOverride >= 0 ? static_cast<int>(sc.fillTypeOverride)
                                                         : std::clamp(params.choice(ParamId::fillType), 0, 5);
            const FillType ft = (reqFill == 0) ? kit.defaultFill : static_cast<FillType>(reqFill);
            appendDrumFill(out, fillStart, totalBeats, totalBeats, ft, groove, pocket, dynamics, ratchetAmt, variation,
                           noteMap, sc, out.signature ^ 0xA511E9B3U);
        }

        sortPlanNotes(out);
        return;
    }

    // =========================================================================
    // MODE 2: LIVE MIDI SLICER & ARTICULATOR (ZERO INVENTED RANDOM NOTES!)
    // =========================================================================
    const auto slicerStyle = static_cast<SlicerStyle>(std::clamp(params.choice(ParamId::slicerStyle), 0, 4));
    if (slicerStyle == SlicerStyle::clipSlice || held.count <= 0) {
        // Clip Slice operates live in Engine::process() on the incoming 1-bar ring buffer;
        // other Slicer modes wait silently until you hold/send notes from Live 12!
        return;
    }

    const int slLen = static_cast<int>(out.slicerLength);
    const double strumStepBeats = strumMs * msToBeats;
    int arpCursor = 0;

    for (int s16 = 0; s16 < totalSteps16; ++s16) {
        const double rawBeat = static_cast<double>(s16) * 0.25;
        const int stIdx = s16 % slLen;
        const double prog = rawBeat / totalBeats;

        // Optional BBCut stutter slice repeat when CHOP > 0
        int effIdx = stIdx;
        bool isChopped = false;
        if (chopAmt > 0.10f && s16 >= 8 && (s16 % 4) >= 2 && rng.uniform() < static_cast<double>(chopAmt) * 0.35) {
            effIdx = (stIdx + slLen - 2) % slLen;
            isChopped = true;
        }

        if (slicerStyle == SlicerStyle::bassStab) {
            // Lowest held note plays the Style's Kick rhythm; upper chord tones play Snare/Stab rhythm!
            const int kickLen = static_cast<int>(out.laneLengths[0]);
            const int kickSt = s16 % kickLen;
            const bool bassHit = out.gridVel[0][static_cast<std::size_t>(kickSt)] > 0;
            const bool stabHit = out.slicerVel[static_cast<std::size_t>(effIdx)] > 0 && ((s16 % 16) != 0 || held.count == 1);

            const double warped = std::clamp(warpBeat(rawBeat, groove, pocket), 0.0, totalBeats - 0.03);
            if (bassHit) {
                CompiledNote bn;
                bn.startBeats = rawBeat;
                bn.warpedBeats = warped;
                bn.durationBeats = std::min(0.16 + 0.34 * static_cast<double>(gateLen), totalBeats - warped - 0.01);
                bn.noteNumber = static_cast<std::int8_t>(held.notes[0]);
                bn.lane = 0;
                bn.stepIndex = static_cast<std::int16_t>(kickSt);
                bn.velocity = applyDynamics(static_cast<int>(held.velocities[0]), (s16 % 16) == 0, false, dynamics,
                                            prog, rng);
                bn.channel = 1;
                bn.isChop = isChopped;
                pushNote(out, bn);
            }
            if (stabHit && held.count > 1) {
                const int upperCount = held.count - 1;
                const double stabDur = std::min(0.10 + 0.32 * static_cast<double>(gateLen), totalBeats - warped - 0.01);
                for (int vi = 1; vi < held.count; ++vi) {
                    const double strumOff = static_cast<double>(vi - 1) * strumStepBeats * 0.45;
                    const double startW = std::min(totalBeats - 0.02, warped + strumOff);
                    CompiledNote cn;
                    cn.startBeats = rawBeat + strumOff;
                    cn.warpedBeats = startW;
                    cn.durationBeats = std::max(0.03, std::min(stabDur, totalBeats - startW - 0.01));
                    cn.noteNumber = static_cast<std::int8_t>(held.notes[static_cast<std::size_t>(vi)]);
                    cn.lane = static_cast<std::int8_t>(std::min(vi, 7));
                    cn.stepIndex = static_cast<std::int16_t>(effIdx);
                    const int baseV = static_cast<int>(held.velocities[static_cast<std::size_t>(vi)]) -
                                      (upperCount > 2 && vi == 1 ? 8 : 0);
                    cn.velocity = applyDynamics(baseV, false, false, dynamics, prog, rng);
                    cn.channel = 1;
                    cn.isChop = isChopped;
                    pushNote(out, cn);
                }
            }
            continue;
        }

        const std::uint8_t stepV = out.slicerVel[static_cast<std::size_t>(effIdx)];
        if (stepV == 0)
            continue;

        const auto& ed = bank.slicerSteps[static_cast<std::size_t>(effIdx)];
        const double nudgeBeats = static_cast<double>(ed.microNudge) / 96.0;
        const double warped = std::clamp(warpBeat(rawBeat, groove, pocket) + nudgeBeats, 0.0, totalBeats - 0.03);

        // Compute distance to next active step for crisp musical gate scaling
        int gapSteps = 1;
        for (int g = 1; g < 8 && (s16 + g) < totalSteps16; ++g) {
            if (out.slicerVel[static_cast<std::size_t>((s16 + g) % slLen)] > 0)
                break;
            ++gapSteps;
        }
        const double stepGap = static_cast<double>(gapSteps) * 0.25;
        const double baseDur = std::min(stepGap * (0.18 + 0.80 * static_cast<double>(gateLen)),
                                        std::max(0.04, totalBeats - warped - 0.01));

        int rc = static_cast<int>(ed.ratchet);
        if (rc == 0 && ratchetAmt > 0.22f && ((s16 & 3) == 3 || isChopped) &&
            rng.uniform() < static_cast<double>(ratchetAmt) * 0.32) {
            rc = ratchetAmt > 0.70f ? 3 : 2;
        }

        if (slicerStyle == SlicerStyle::breakArp) {
            // Strictly cycle through YOUR held notes only!
            int voiceIdx = 0;
            const int patternDir = (variation - 1) % 3;
            if (patternDir == 0) {
                voiceIdx = arpCursor % held.count; // Up
            } else if (patternDir == 1) {
                voiceIdx = (held.count - 1) - (arpCursor % held.count); // Down
            } else {
                const int span = std::max(1, held.count * 2 - 2);
                const int p = arpCursor % span;
                voiceIdx = p < held.count ? p : (span - p); // Up-Down
            }
            ++arpCursor;

            int noteNum = static_cast<int>(held.notes[static_cast<std::size_t>(voiceIdx)]);
            bool ghostOct = false;
            if (ghosts > 0.52f && (s16 % 4 == 3) && noteNum <= 96) {
                noteNum += 12; // Clean octave echo of your exact pitch class
                ghostOct = true;
            }

            const int reps = std::max(1, rc);
            const double subStep = 0.25 / static_cast<double>(reps);
            for (int r = 0; r < reps; ++r) {
                const double startW = warped + static_cast<double>(r) * subStep;
                if (startW >= totalBeats - 0.02)
                    break;
                CompiledNote n;
                n.startBeats = rawBeat + static_cast<double>(r) * subStep;
                n.warpedBeats = startW;
                n.durationBeats =
                    reps > 1 ? std::min(subStep * 0.78, totalBeats - startW - 0.01) : std::min(baseDur, totalBeats - startW - 0.01);
                n.noteNumber = static_cast<std::int8_t>(noteNum);
                n.lane = static_cast<std::int8_t>(std::min(voiceIdx, 7));
                n.stepIndex = static_cast<std::int16_t>(effIdx);
                n.velocity = applyDynamics(static_cast<int>(stepV), stepV > 105, ghostOct, dynamics, prog, rng);
                n.channel = 1;
                n.isGhost = ghostOct;
                n.isRatchet = reps > 1;
                n.isChop = isChopped;
                pushNote(out, n);
            }
        } else {
            // Stab Chop OR Strum/Flam: play ALL your held notes cleanly in tune!
            const bool doStrum = (slicerStyle == SlicerStyle::strumFlam) || ed.flam;
            const bool revStrum = doStrum && ((s16 / 4) & 1) != 0;
            const int reps = std::max(1, rc);
            const double subStep = 0.25 / static_cast<double>(reps);

            for (int r = 0; r < reps; ++r) {
                const double baseW = warped + static_cast<double>(r) * subStep;
                if (baseW >= totalBeats - 0.02)
                    break;
                for (int vi = 0; vi < held.count; ++vi) {
                    const int order = revStrum ? (held.count - 1 - vi) : vi;
                    const double sOff = doStrum ? static_cast<double>(order) * strumStepBeats : 0.0;
                    const double startW = std::min(totalBeats - 0.02, baseW + sOff);
                    const double dur =
                        reps > 1 ? std::min(subStep * 0.78, totalBeats - startW - 0.01) : std::min(baseDur, totalBeats - startW - 0.01);
                    const int voiceVel =
                        (static_cast<int>(stepV) + static_cast<int>(held.velocities[static_cast<std::size_t>(vi)])) / 2 +
                        (doStrum && vi == held.count - 1 ? 8 : 0);

                    CompiledNote n;
                    n.startBeats = rawBeat + static_cast<double>(r) * subStep + sOff;
                    n.warpedBeats = startW;
                    n.durationBeats = std::max(0.03, dur);
                    n.noteNumber = static_cast<std::int8_t>(held.notes[static_cast<std::size_t>(vi)]);
                    n.lane = static_cast<std::int8_t>(std::min(vi, 7));
                    n.stepIndex = static_cast<std::int16_t>(effIdx);
                    n.velocity = applyDynamics(voiceVel, stepV > 105, false, dynamics, prog, rng);
                    n.channel = 1;
                    n.isRatchet = reps > 1;
                    n.isChop = isChopped;
                    n.isFlam = doStrum && vi > 0;
                    pushNote(out, n);
                }
            }
        }
    }

    sortPlanNotes(out);
}

} // namespace cl
