// SPDX-License-Identifier: AGPL-3.0-or-later
// Datum parameter catalog: 56 automatable + 6 non-automatable voice-bar parameters.
#pragma once

#include "dt/nonblocking.h"

#include <hba/core/param_math.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace dt {

inline constexpr int kScopePoints = 256;
inline constexpr int kKitSlots = 4;
inline constexpr int kMaxSeqSteps = 32;

enum class ParamKind : std::uint8_t { continuous, choice, toggle };
enum class CoreTopology : std::uint8_t { sine = 0, twinT = 1, harmonic = 2, membrane = 3 };
enum class BeaterType : std::uint8_t { felt = 0, wood = 1, plastic = 2, leather = 3, metal = 4, dirac = 5 };
enum class NoiseMode : std::uint8_t { lp = 0, bp = 1, hp = 2 };
enum class ShaperType : std::uint8_t { tube = 0, tape = 1, soft = 2, hard = 3, fold = 4, asym = 5 };
enum class KeyTrackMode : std::uint8_t { kit = 0, chromatic = 1 };
enum class ChokeMode : std::uint8_t { mono = 0, splitSub = 1, poly4 = 2 };
enum class HeadroomMode : std::uint8_t { zero = 0, schroeder = 1, peakMin = 2 };
enum class RefBassMode : std::uint8_t { off = 0, dnb172 = 1, half140 = 2, tech132 = 3, sustained = 4 };

enum class ParamId : std::size_t {
    // 0. Hero Macros (SCULPT)
    tuneHz = 0,
    punchAmt,
    lengthMs,
    clickAmt,
    drive,
    tight,

    // 1. CORE (Nord-philosophy pure synthesis)
    coreTopology,
    startPhase,
    harm2,
    harm3,
    harm4,
    harm5,
    harm6,
    harmTilt,
    tension,
    cavityHz,
    cavityQ,
    pitchDepth,

    // 2. IMPACT (Pure-DSP beater + 2-op FM + dynamic noise + optional WAV)
    beaterType,
    beaterHard,
    beaterTone,
    fmDepth,
    fmRatio,
    fmDecayMs,
    noiseMode,
    noiseLevel,
    noiseHz,
    noiseQ,
    noiseDecayMs,
    clickWavMix,

    // 3. SHAPE (Phase-native 3-band + envelope-native transient + 8x ADAA2 + Box-Cut)
    subLevel,
    bodyLevel,
    clickLevel,
    envAttack,
    envPunch,
    envTail,
    shaperType,
    shaperGlue,
    boxCutHz,
    boxCutDb,
    boxZeroPhase,
    airWidth,

    // 4. MASTER & SUB-LINK
    phaseRotate,
    disperseAmt,
    disperseHz,
    outputDb,
    ceilingDb,
    limitOn,
    humanize,
    velToPunch,

    // 5. COMPOSE (4-slot kit + context sequencer)
    activeSlot,
    slotMorph,
    seqRun,
    seqSteps,
    seqRate,
    seqSwing,

    // 6. Studio Voice Footer Bar (non-automatable, staying cleanly <= 64 automatable)
    keyTrack,
    chokeMode,
    zeroCross,
    headroomMode,
    quality,
    refBassMode,

    count
};

inline constexpr std::size_t kParamCount = static_cast<std::size_t>(ParamId::count);

struct ParamSpec {
    ParamId id{};
    std::string_view key;
    std::string_view name;
    std::string_view unit;
    ParamKind kind = ParamKind::continuous;
    float low = 0.0f;
    float high = 1.0f;
    float def = 0.0f;
    float skew = 1.0f;
    std::span<const std::string_view> choices{};
    bool automatable = true;
};

namespace choices {
inline constexpr std::array<std::string_view, 4> coreTopology{"Sine", "Twin-T", "Harmonic", "Membrane"};
inline constexpr std::array<std::string_view, 6> beaterType{"Felt", "Wood", "Plastic", "Leather", "Metal", "Dirac"};
inline constexpr std::array<std::string_view, 3> noiseMode{"LP", "BP", "HP"};
inline constexpr std::array<std::string_view, 6> shaperType{"Tube", "Tape", "Soft", "Hard", "Fold", "Asym"};
inline constexpr std::array<std::string_view, 4> activeSlot{"A · Main", "B · Ghost", "C · Double", "D · Sub"};
inline constexpr std::array<std::string_view, 2> seqSteps{"16", "32"};
inline constexpr std::array<std::string_view, 3> seqRate{"1/16", "1/16T", "1/32"};
inline constexpr std::array<std::string_view, 2> keyTrack{"Kit", "Chromatic"};
inline constexpr std::array<std::string_view, 3> chokeMode{"Mono", "Split Sub", "Poly 4"};
inline constexpr std::array<std::string_view, 3> headroomMode{"Zero", "Schroeder", "Peak-Min"};
inline constexpr std::array<std::string_view, 3> quality{"4\xc3\x97", "8\xc3\x97", "16\xc3\x97"};
inline constexpr std::array<std::string_view, 5> refBassMode{"Off", "172 DnB", "140 Half", "132 Tech", "Sustained"};
} // namespace choices

inline constexpr std::array<ParamSpec, kParamCount> kParamSpecs{{
    // 0. Hero Macros
    {ParamId::tuneHz, "tuneHz", "Tune", "Hz", ParamKind::continuous, 28.0f, 120.0f, 52.0f, 0.45f},
    {ParamId::punchAmt, "punchAmt", "Punch", "%", ParamKind::continuous, 0.0f, 1.0f, 0.62f, 1.0f},
    {ParamId::lengthMs, "lengthMs", "Length", "ms", ParamKind::continuous, 45.0f, 900.0f, 145.0f, 0.38f},
    {ParamId::clickAmt, "clickAmt", "Click", "%", ParamKind::continuous, 0.0f, 1.0f, 0.48f, 1.0f},
    {ParamId::drive, "drive", "Drive", "%", ParamKind::continuous, 0.0f, 1.0f, 0.36f, 1.0f},
    {ParamId::tight, "tight", "Tight", "%", ParamKind::continuous, 0.0f, 1.0f, 0.35f, 1.0f},

    // 1. CORE
    {ParamId::coreTopology, "coreTopology", "Core", "", ParamKind::choice, 0.0f, 3.0f, 2.0f, 1.0f,
     choices::coreTopology},
    {ParamId::startPhase, "startPhase", "Start Phase", "\xc2\xb0", ParamKind::continuous, 0.0f, 360.0f, 0.0f, 1.0f},
    {ParamId::harm2, "harm2", "H2", "%", ParamKind::continuous, 0.0f, 1.0f, 0.34f, 1.0f},
    {ParamId::harm3, "harm3", "H3", "%", ParamKind::continuous, 0.0f, 1.0f, 0.22f, 1.0f},
    {ParamId::harm4, "harm4", "H4", "%", ParamKind::continuous, 0.0f, 1.0f, 0.12f, 1.0f},
    {ParamId::harm5, "harm5", "H5", "%", ParamKind::continuous, 0.0f, 1.0f, 0.06f, 1.0f},
    {ParamId::harm6, "harm6", "H6", "%", ParamKind::continuous, 0.0f, 1.0f, 0.03f, 1.0f},
    {ParamId::harmTilt, "harmTilt", "Tilt", "%", ParamKind::continuous, 0.0f, 1.0f, 0.68f, 1.0f},
    {ParamId::tension, "tension", "Tension", "%", ParamKind::continuous, 0.0f, 1.0f, 0.32f, 1.0f},
    {ParamId::cavityHz, "cavityHz", "Cavity Hz", "Hz", ParamKind::continuous, 45.0f, 180.0f, 74.0f, 0.55f},
    {ParamId::cavityQ, "cavityQ", "Cavity", "%", ParamKind::continuous, 0.0f, 1.0f, 0.26f, 1.0f},
    {ParamId::pitchDepth, "pitchDepth", "Pitch Depth", "%", ParamKind::continuous, 0.0f, 1.0f, 0.65f, 1.0f},

    // 2. IMPACT
    {ParamId::beaterType, "beaterType", "Beater", "", ParamKind::choice, 0.0f, 5.0f, 1.0f, 1.0f, choices::beaterType},
    {ParamId::beaterHard, "beaterHard", "Hardness", "%", ParamKind::continuous, 0.0f, 1.0f, 0.58f, 1.0f},
    {ParamId::beaterTone, "beaterTone", "Beater Hz", "Hz", ParamKind::continuous, 250.0f, 12000.0f, 3200.0f, 0.35f},
    {ParamId::fmDepth, "fmDepth", "FM Depth", "%", ParamKind::continuous, 0.0f, 1.0f, 0.30f, 1.0f},
    {ParamId::fmRatio, "fmRatio", "FM Ratio", "x", ParamKind::continuous, 0.5f, 8.0f, 2.35f, 0.55f},
    {ParamId::fmDecayMs, "fmDecayMs", "FM Decay", "ms", ParamKind::continuous, 2.0f, 65.0f, 11.0f, 0.45f},
    {ParamId::noiseMode, "noiseMode", "Noise Mode", "", ParamKind::choice, 0.0f, 2.0f, 1.0f, 1.0f, choices::noiseMode},
    {ParamId::noiseLevel, "noiseLevel", "Noise", "%", ParamKind::continuous, 0.0f, 1.0f, 0.24f, 1.0f},
    {ParamId::noiseHz, "noiseHz", "Noise Hz", "Hz", ParamKind::continuous, 400.0f, 14000.0f, 4400.0f, 0.35f},
    {ParamId::noiseQ, "noiseQ", "Noise Q", "%", ParamKind::continuous, 0.0f, 1.0f, 0.35f, 1.0f},
    {ParamId::noiseDecayMs, "noiseDecayMs", "Noise ms", "ms", ParamKind::continuous, 3.0f, 120.0f, 16.0f, 0.45f},
    {ParamId::clickWavMix, "clickWavMix", "WAV Mix", "%", ParamKind::continuous, 0.0f, 1.0f, 0.0f, 1.0f},

    // 3. SHAPE
    {ParamId::subLevel, "subLevel", "Sub", "%", ParamKind::continuous, 0.0f, 1.25f, 0.92f, 1.0f},
    {ParamId::bodyLevel, "bodyLevel", "Body", "%", ParamKind::continuous, 0.0f, 1.25f, 0.86f, 1.0f},
    {ParamId::clickLevel, "clickLevel", "Click", "%", ParamKind::continuous, 0.0f, 1.25f, 0.74f, 1.0f},
    {ParamId::envAttack, "envAttack", "Attack", "%", ParamKind::continuous, -1.0f, 1.0f, 0.18f, 1.0f},
    {ParamId::envPunch, "envPunch", "Body", "%", ParamKind::continuous, -1.0f, 1.0f, 0.15f, 1.0f},
    {ParamId::envTail, "envTail", "Tail", "%", ParamKind::continuous, -1.0f, 1.0f, 0.0f, 1.0f},
    {ParamId::shaperType, "shaperType", "Shaper", "", ParamKind::choice, 0.0f, 5.0f, 0.0f, 1.0f, choices::shaperType},
    {ParamId::shaperGlue, "shaperGlue", "Glue", "%", ParamKind::continuous, 0.0f, 1.0f, 0.20f, 1.0f},
    {ParamId::boxCutHz, "boxCutHz", "Box Hz", "Hz", ParamKind::continuous, 180.0f, 520.0f, 310.0f, 0.65f},
    {ParamId::boxCutDb, "boxCutDb", "Box Cut", "dB", ParamKind::continuous, 0.0f, 18.0f, 3.5f, 1.0f},
    {ParamId::boxZeroPhase, "boxZeroPhase", "Zero-Ph Dip", "", ParamKind::toggle, 0.0f, 1.0f, 1.0f, 1.0f},
    {ParamId::airWidth, "airWidth", "Air Width", "%", ParamKind::continuous, 0.0f, 1.0f, 0.18f, 1.0f},

    // 4. MASTER & SUB-LINK
    {ParamId::phaseRotate, "phaseRotate", "Phase Rot", "\xc2\xb0", ParamKind::continuous, 0.0f, 360.0f, 0.0f, 1.0f},
    {ParamId::disperseAmt, "disperseAmt", "Disperser", "%", ParamKind::continuous, 0.0f, 1.0f, 0.14f, 1.0f},
    {ParamId::disperseHz, "disperseHz", "Disp Hz", "Hz", ParamKind::continuous, 90.0f, 1200.0f, 340.0f, 0.45f},
    {ParamId::outputDb, "outputDb", "Output", "dB", ParamKind::continuous, -18.0f, 6.0f, 0.0f, 1.0f},
    {ParamId::ceilingDb, "ceilingDb", "Ceiling", "dB", ParamKind::continuous, -6.0f, 0.0f, -0.3f, 1.0f},
    {ParamId::limitOn, "limitOn", "Limit", "", ParamKind::toggle, 0.0f, 1.0f, 1.0f, 1.0f},
    {ParamId::humanize, "humanize", "Humanize", "%", ParamKind::continuous, 0.0f, 1.0f, 0.0f, 1.0f},
    {ParamId::velToPunch, "velToPunch", "Vel Punch", "%", ParamKind::continuous, 0.0f, 1.0f, 0.55f, 1.0f},

    // 5. COMPOSE
    {ParamId::activeSlot, "activeSlot", "Slot", "", ParamKind::choice, 0.0f, 3.0f, 0.0f, 1.0f, choices::activeSlot},
    {ParamId::slotMorph, "slotMorph", "Morph", "%", ParamKind::continuous, 0.0f, 1.0f, 0.0f, 1.0f},
    {ParamId::seqRun, "seqRun", "Play Seq", "", ParamKind::toggle, 0.0f, 1.0f, 0.0f, 1.0f},
    {ParamId::seqSteps, "seqSteps", "Steps", "", ParamKind::choice, 0.0f, 1.0f, 0.0f, 1.0f, choices::seqSteps},
    {ParamId::seqRate, "seqRate", "Rate", "", ParamKind::choice, 0.0f, 2.0f, 0.0f, 1.0f, choices::seqRate},
    {ParamId::seqSwing, "seqSwing", "Swing", "%", ParamKind::continuous, 0.0f, 0.5f, 0.0f, 1.0f},

    // 6. Voice Bar (non-automatable)
    {ParamId::keyTrack, "keyTrack", "Track", "", ParamKind::choice, 0.0f, 1.0f, 0.0f, 1.0f, choices::keyTrack, false},
    {ParamId::chokeMode, "chokeMode", "Choke", "", ParamKind::choice, 0.0f, 2.0f, 0.0f, 1.0f, choices::chokeMode,
     false},
    {ParamId::zeroCross, "zeroCross", "Zero-X", "", ParamKind::toggle, 0.0f, 1.0f, 1.0f, 1.0f, {}, false},
    {ParamId::headroomMode, "headroomMode", "Headroom", "", ParamKind::choice, 0.0f, 2.0f, 2.0f, 1.0f,
     choices::headroomMode, false},
    {ParamId::quality, "quality", "OS", "", ParamKind::choice, 0.0f, 2.0f, 0.0f, 1.0f, choices::quality, false},
    {ParamId::refBassMode, "refBassMode", "Ref Sub", "", ParamKind::choice, 0.0f, 4.0f, 0.0f, 1.0f,
     choices::refBassMode, false},
}};

static_assert(
    [] {
        std::size_t autoCount = 0;
        for (std::size_t i = 0; i < kParamCount; ++i) {
            if (static_cast<std::size_t>(kParamSpecs[i].id) != i)
                return false;
            if (kParamSpecs[i].automatable)
                ++autoCount;
        }
        return autoCount <= 64;
    }(),
    "ParamSpec table must match enum order and stay <= 64 automatable parameters.");

inline constexpr const ParamSpec& spec(ParamId id) noexcept DT_NONBLOCKING {
    return kParamSpecs[static_cast<std::size_t>(id)];
}

inline constexpr const ParamSpec& specOf(ParamId id) noexcept DT_NONBLOCKING { return spec(id); }

inline float clampParam(const ParamSpec& s, float v) noexcept DT_NONBLOCKING {
    return hba::core::clampParam(v, s.low, s.high, s.def, s.kind == ParamKind::continuous);
}

inline float clampToRange(const ParamSpec& s, float v) noexcept DT_NONBLOCKING { return clampParam(s, v); }

inline float toNormalised(const ParamSpec& s, float real) noexcept DT_NONBLOCKING {
    return hba::core::toNormalisedJuceSkew(real, s.low, s.high, s.def, s.skew, s.kind == ParamKind::continuous);
}

inline float fromNormalised(const ParamSpec& s, float norm) noexcept DT_NONBLOCKING {
    return hba::core::fromNormalisedJuceSkew(norm, s.low, s.high, s.def, s.skew, s.kind == ParamKind::continuous);
}

struct Params {
    std::array<float, kParamCount> values{};

    static Params defaults() noexcept DT_NONBLOCKING {
        Params p;
        for (std::size_t i = 0; i < kParamCount; ++i)
            p.values[i] = kParamSpecs[i].def;
        return p;
    }

    float& operator[](ParamId id) noexcept DT_NONBLOCKING { return values[static_cast<std::size_t>(id)]; }
    float operator[](ParamId id) const noexcept DT_NONBLOCKING { return values[static_cast<std::size_t>(id)]; }

    float real(ParamId id) const noexcept DT_NONBLOCKING { return values[static_cast<std::size_t>(id)]; }
    void set(ParamId id, float v) noexcept DT_NONBLOCKING {
        values[static_cast<std::size_t>(id)] = clampParam(spec(id), v);
    }
    int choice(ParamId id) const noexcept DT_NONBLOCKING {
        const auto& s = spec(id);
        return std::clamp(static_cast<int>(real(id) + 0.5f), static_cast<int>(s.low), static_cast<int>(s.high));
    }
    bool flag(ParamId id) const noexcept DT_NONBLOCKING { return real(id) >= 0.5f; }
};

std::string formatValue(const ParamSpec& s, float real);
std::string noteNameForHz(float hz);

} // namespace dt
