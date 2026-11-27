// SPDX-License-Identifier: AGPL-3.0-or-later
// Single source of truth for Lineage parameters: core, DSP engine, JUCE host, and Visage UI.
#pragma once

#include <hba/dsp/lane_math.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ln {

enum class ParamId : std::uint8_t {
    // Bank 1 (indices 0..7): primary performance macros for DAWs & controllers.
    filt1Hz,
    drive,
    tone,
    filt2Hz,
    genMix,
    strataPos,
    motion,
    subLevel,

    // Gen 0 · Reese / Sync Source & Sub Lock
    coreMode,
    unison,
    detune,
    glide,
    attack,
    release,
    subWarmth,
    subSplit,
    subPhase,
    octave,

    // Gen 1 · Serial Sculpting Bench & Controlled Density
    filt1Mode,
    filt1Res,
    driveType,
    asym,
    filt2Mode,
    filt2Res,
    disperse,
    density,
    width,
    postDrive,
    postTone,

    // Gen 2 · Resampled Strata Playback, X/Y Pad & Output
    strataSpan,
    strataDir,
    strataPitch,
    printBars,
    strataBlend,
    strataFade,
    strataKeytrack,
    droneNote,
    playMode,
    glideMode,
    bendRange,
    padProgram,
    padX,
    padY,
    padTouch,
    padHold,
    outputGain,
    limit,
    ceiling,
    quality,

    // Unified 4-lane Free-Hz / Sync movement engine
    lane1On,
    lane1Rate,
    lane1Phase,
    lane1Depth,
    lane1Shape,
    lane1Target,
    lane1Follow,
    lane1Ratio,
    lane1Mode,
    lane1Hz,
    lane2On,
    lane2Rate,
    lane2Phase,
    lane2Depth,
    lane2Shape,
    lane2Target,
    lane2Follow,
    lane2Ratio,
    lane2Mode,
    lane2Hz,
    lane3On,
    lane3Rate,
    lane3Phase,
    lane3Depth,
    lane3Shape,
    lane3Target,
    lane3Follow,
    lane3Ratio,
    lane3Mode,
    lane3Hz,
    lane4On,
    lane4Rate,
    lane4Phase,
    lane4Depth,
    lane4Shape,
    lane4Target,
    lane4Follow,
    lane4Ratio,
    lane4Mode,
    lane4Hz,

    count
};

inline constexpr int kParamCount = static_cast<int>(ParamId::count);
inline constexpr int kLaneCount = 4;

enum class LaneField : std::uint8_t { on, rate, phase, depth, shape, target, follow, ratio, mode, hz };
inline constexpr int kLaneFieldCount = 10;

enum class LaneTarget : std::uint8_t {
    filt1Hz,
    filt2Hz,
    tone,
    drive,
    strataPos,
    strataSpan,
    genMix,
    disperse,
    subPhase,
    width,
    density
};
inline constexpr int kLaneTargetCount = 11;

constexpr ParamId laneParam(int lane, LaneField field) {
    return static_cast<ParamId>(static_cast<int>(ParamId::lane1On) + lane * kLaneFieldCount + static_cast<int>(field));
}

enum class ParamKind : std::uint8_t { continuous, toggle, choice };
enum class Unit : std::uint8_t { none, percent, ms, cents, semitones, degrees, dB, hz };

struct ParamSpec {
    std::string_view key;
    std::string_view name;
    ParamKind kind = ParamKind::continuous;
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    Unit unit = Unit::none;
    // real = min + (max - min) * normalised^skew; 0.0f means logarithmic.
    float skew = 1.0f;
    std::span<const std::string_view> choices = {};
    bool automatable = true;
};

enum class CoreMode : std::uint8_t { analogReese, softSync, hardSync, tzfm, pulseReese, subFold };
enum class Filt1Mode : std::uint8_t { lp24, bp12, notch, comb, formant };
enum class DriveType : std::uint8_t { tube, fold, buchla, sine, clip };
enum class Filt2Mode : std::uint8_t { notch, dualNotch, lp12, bp12, phaser };
enum class StrataDir : std::uint8_t { forward, pingPong, reverse, scrub };
enum class StrataBlend : std::uint8_t { blend, hiStrata, loStrata };
enum class PlayMode : std::uint8_t { mono, legato, poly4 };
enum class GlideMode : std::uint8_t { always, legato };
enum class PadProgram : std::uint8_t { sculpt, reese, strata, disperse, abyss };
inline constexpr int kPadProgramCount = 5;

enum class LaneShape : std::uint8_t { sine, triangle, rampUp, rampDown, square, steps, custom };
using LaneMode = hba::dsp::LaneMode;

namespace choices {
inline constexpr std::array<std::string_view, 2> offOn{"Off", "On"};
inline constexpr std::array<std::string_view, 6> coreMode{"Analog Reese", "Soft Sync",   "Hard Sync",
                                                          "TZFM",         "Pulse Reese", "Sub Fold"};
inline constexpr std::array<std::string_view, 4> octave{"-2 oct", "-1 oct", "0 oct", "+1 oct"};
inline constexpr std::array<std::string_view, 5> filt1Mode{"LP 24", "BP 12", "Notch", "Comb", "Formant"};
inline constexpr std::array<std::string_view, 5> driveType{"Tube", "Fold", "Buchla", "Sine", "Clip"};
inline constexpr std::array<std::string_view, 5> filt2Mode{"Notch", "Dual Notch", "LP 12", "BP 12", "Phaser"};
inline constexpr std::array<std::string_view, 4> strataDir{"Forward", "Ping-Pong", "Reverse", "Scrub"};
inline constexpr std::array<std::string_view, 3> strataBlend{"Blend", "Hi-Strata", "Lo-Strata"};
inline constexpr std::array<std::string_view, 3> printBars{"1 bar", "2 bars", "4 bars"};
inline constexpr std::array<std::string_view, 12> droneNote{"C1",  "C#1", "D1",  "D#1", "E1",  "F1",
                                                            "F#1", "G1",  "G#1", "A1",  "A#1", "B1"};
inline constexpr std::array<std::string_view, 3> playMode{"Mono", "Legato", "Poly 4"};
inline constexpr std::array<std::string_view, 2> glideMode{"Always", "Legato"};
inline constexpr std::array<std::string_view, 5> bendRange{"\xc2\xb1"
                                                           "2 st",
                                                           "\xc2\xb1"
                                                           "7 st",
                                                           "\xc2\xb1"
                                                           "12 st",
                                                           "\xc2\xb1"
                                                           "24 st",
                                                           "\xc2\xb1"
                                                           "48 st"};
inline constexpr std::array<std::string_view, 5> padProgram{"Sculpt", "Reese", "Strata", "Disperse", "Abyss"};
inline constexpr std::array<std::string_view, 3> quality{"2\u00d7", "4\u00d7", "8\u00d7"};

inline constexpr auto& laneRate = hba::dsp::kLaneRateLabels;
inline constexpr std::array<std::string_view, 7> laneShape{"Sine",   "Triangle", "Ramp up", "Ramp down",
                                                           "Square", "Steps",    "Custom"};
inline constexpr std::array<std::string_view, 11> laneTarget{"Filter 1",   "Filter 2",    "Tone",    "Drive",
                                                             "Strata Pos", "Strata Span", "Gen Mix", "Disperse",
                                                             "Sub Phase",  "Width",       "Density"};
inline constexpr auto& laneFollow = hba::dsp::kLaneFollowLabels;
inline constexpr auto& laneRatio = hba::dsp::kLaneRatioLabels;
inline constexpr auto& laneMode = hba::dsp::kLaneModeLabels;
} // namespace choices

inline constexpr std::array<int, 3> kQualityFactors{2, 4, 8};
inline constexpr std::array<int, 3> kPrintBarCounts{1, 2, 4};
inline constexpr std::array<int, 4> kOctaveSemitones{-24, -12, 0, 12};
inline constexpr std::array<int, 5> kBendSemitones{2, 7, 12, 24, 48};
using hba::dsp::kLaneRateQuarters;
using hba::dsp::kLaneRatios;

struct PadCaptions {
    std::string_view xLabel;
    std::string_view yLabel;
};

constexpr PadCaptions padCaptions(PadProgram program) {
    switch (program) {
    case PadProgram::sculpt:
        return {"FILTER 1 ORBIT", "ADAA DRIVE & BLOOM"};
    case PadProgram::reese:
        return {"SYNC / DETUNE TEAR", "NOTCH CANYON & WIDTH"};
    case PadProgram::strata:
        return {"STRATA SCRUB POS", "GEN MIX & APERTURE"};
    case PadProgram::disperse:
        return {"DISPERSION PIVOT", "SMEAR & DENSITY"};
    case PadProgram::abyss:
        return {"SUB WARMTH & FIT", "LOW-PASS DIVE"};
    }
    return {"X", "Y"};
}

namespace detail {
constexpr ParamSpec amount(std::string_view key, std::string_view name, float def, bool automatable = true) {
    return {.key = key, .name = name, .def = def, .unit = Unit::percent, .automatable = automatable};
}
constexpr ParamSpec bipolar(std::string_view key, std::string_view name, float def, bool automatable = true) {
    return {.key = key, .name = name, .min = -1.0f, .def = def, .unit = Unit::percent, .automatable = automatable};
}
constexpr ParamSpec toggle(std::string_view key, std::string_view name, bool def, bool automatable = true) {
    return {.key = key,
            .name = name,
            .kind = ParamKind::toggle,
            .def = def ? 1.0f : 0.0f,
            .choices = choices::offOn,
            .automatable = automatable};
}
constexpr ParamSpec choice(std::string_view key, std::string_view name, std::span<const std::string_view> list, int def,
                           bool automatable = true) {
    return {.key = key,
            .name = name,
            .kind = ParamKind::choice,
            .max = static_cast<float>(list.size() - 1),
            .def = static_cast<float>(def),
            .choices = list,
            .automatable = automatable};
}
constexpr ParamSpec ranged(std::string_view key, std::string_view name, float min, float max, float def, Unit unit,
                           float skew = 1.0f, bool automatable = true) {
    return {.key = key,
            .name = name,
            .min = min,
            .max = max,
            .def = def,
            .unit = unit,
            .skew = skew,
            .automatable = automatable};
}
} // namespace detail

#define LN_LANE(n, onDef, target, rate, phase, depth)                                                                  \
    detail::toggle("lane" #n "On", "Lane " #n, onDef),                                                                 \
        detail::choice("lane" #n "Rate", "Lane " #n " rate", choices::laneRate, rate, false),                          \
        detail::ranged("lane" #n "Phase", "Lane " #n " phase", 0.0f, 360.0f, phase, Unit::degrees),                    \
        detail::bipolar("lane" #n "Depth", "Lane " #n " depth", depth),                                                \
        detail::choice("lane" #n "Shape", "Lane " #n " shape", choices::laneShape, 0, false),                          \
        detail::choice("lane" #n "Target", "Lane " #n " target", choices::laneTarget, static_cast<int>(target),        \
                       false),                                                                                         \
        detail::choice("lane" #n "Follow", "Lane " #n " follow", choices::laneFollow, 0, false),                       \
        detail::choice("lane" #n "Ratio", "Lane " #n " ratio", choices::laneRatio, 3, false),                          \
        detail::choice("lane" #n "Mode", "Lane " #n " mode", choices::laneMode, 0, false),                             \
        detail::ranged("lane" #n "Hz", "Lane " #n " Hz", hba::dsp::kLaneMinHz, hba::dsp::kLaneMaxHz,                   \
                       hba::dsp::kLaneDefaultHz, Unit::hz, 0.0f, false)

// Order must match ParamId.
inline constexpr std::array<ParamSpec, kParamCount> kParams{{
    // Bank 1 performance macros
    detail::ranged("filt1Hz", "Filter 1", 24.0f, 18000.0f, 920.0f, Unit::hz, 0.0f),
    detail::amount("drive", "Drive", 0.38f),
    detail::amount("tone", "Tone", 0.42f),
    detail::ranged("filt2Hz", "Filter 2", 60.0f, 14000.0f, 1850.0f, Unit::hz, 0.0f),
    detail::amount("genMix", "Gen Mix", 0.0f),
    detail::amount("strataPos", "Strata Pos", 0.0f),
    detail::amount("motion", "Motion", 0.60f),
    detail::amount("subLevel", "Sub Level", 0.78f),

    // Gen 0 · Source & Sub
    detail::choice("coreMode", "Core", choices::coreMode, 0),
    detail::amount("unison", "Unison", 0.68f),
    detail::ranged("detune", "Detune", 0.0f, 65.0f, 24.0f, Unit::cents, 1.5f),
    detail::ranged("glide", "Glide", 0.0f, 400.0f, 38.0f, Unit::ms, 2.5f),
    detail::ranged("attack", "Attack", 0.5f, 500.0f, 4.0f, Unit::ms, 3.0f),
    detail::ranged("release", "Release", 5.0f, 2000.0f, 95.0f, Unit::ms, 3.0f),
    detail::amount("subWarmth", "Sub Warmth", 0.26f),
    detail::ranged("subSplit", "Sub Split", 55.0f, 220.0f, 105.0f, Unit::hz, 0.0f),
    detail::ranged("subPhase", "Sub Phase", 0.0f, 360.0f, 0.0f, Unit::degrees),
    detail::choice("octave", "Octave", choices::octave, 2, false),

    // Gen 1 · Serial Sculpt & Density
    detail::choice("filt1Mode", "Filt 1 Mode", choices::filt1Mode, 0),
    detail::amount("filt1Res", "Filt 1 Res", 0.34f),
    detail::choice("driveType", "Shaper", choices::driveType, 0),
    detail::amount("asym", "Asym", 0.16f),
    detail::choice("filt2Mode", "Filt 2 Mode", choices::filt2Mode, 1),
    detail::amount("filt2Res", "Filt 2 Res", 0.48f),
    detail::amount("disperse", "Disperse", 0.24f),
    detail::amount("density", "Density", 0.36f),
    detail::amount("width", "Width", 0.68f),
    detail::amount("postDrive", "Post Drive", 0.18f),
    detail::bipolar("postTone", "Post Tone", 0.0f),

    // Gen 2 · Strata Playback, X/Y Pad & Output
    detail::ranged("strataSpan", "Strata Span", 0.05f, 1.0f, 1.0f, Unit::percent),
    detail::choice("strataDir", "Strata Dir", choices::strataDir, 1),
    detail::ranged("strataPitch", "Strata Pitch", -24.0f, 24.0f, 0.0f, Unit::semitones),
    detail::choice("printBars", "Print Bars", choices::printBars, 1, false),
    detail::choice("strataBlend", "Strata Blend", choices::strataBlend, 0),
    detail::ranged("strataFade", "Loop Xfade", 2.0f, 80.0f, 14.0f, Unit::ms, 1.8f),
    detail::toggle("strataKeytrack", "Keytrack", true, false),
    detail::choice("droneNote", "Drone Key", choices::droneNote, 5, false),
    detail::choice("playMode", "Play Mode", choices::playMode, 0, false),
    detail::choice("glideMode", "Portamento", choices::glideMode, 0, false),
    detail::choice("bendRange", "Bend Range", choices::bendRange, 0, false),
    detail::choice("padProgram", "Pad World", choices::padProgram, 0, false),
    detail::amount("padX", "Pad X", 0.50f),
    detail::amount("padY", "Pad Y", 0.50f),
    detail::toggle("padTouch", "Pad Touch", false, false),
    detail::toggle("padHold", "Drone", false, false),
    detail::ranged("outputGain", "Output", -24.0f, 12.0f, 0.0f, Unit::dB),
    detail::toggle("limit", "Limit", true, false),
    detail::ranged("ceiling", "Ceiling", -12.0f, 0.0f, -0.3f, Unit::dB),
    detail::choice("quality", "Quality", choices::quality, 1, false),

    // 4 Unified Movement Lanes
    LN_LANE(1, true, LaneTarget::filt1Hz, 3, 0.0f, 0.38f),
    LN_LANE(2, true, LaneTarget::filt2Hz, 2, 90.0f, 0.46f),
    LN_LANE(3, false, LaneTarget::tone, 4, 180.0f, 0.30f),
    LN_LANE(4, false, LaneTarget::strataPos, 2, 0.0f, 0.45f),
}};

#undef LN_LANE

constexpr const ParamSpec& spec(ParamId id) { return kParams[static_cast<std::size_t>(id)]; }

namespace detail {
constexpr bool tableIsValid() {
    int automatable = 0;
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        const auto& p = kParams[i];
        if (p.key.empty() || p.name.empty())
            return false;
        if (!(p.min < p.max))
            return false;
        if (p.def < p.min || p.def > p.max)
            return false;
        if (p.skew < 0.0f)
            return false;
        if (p.skew == 0.0f && p.min <= 0.0f)
            return false;
        if (p.kind != ParamKind::continuous && p.choices.size() != static_cast<std::size_t>(p.max) + 1)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (kParams[j].key == p.key)
                return false;
        automatable += p.automatable ? 1 : 0;
    }
    return automatable <= 64;
}
} // namespace detail

static_assert(detail::tableIsValid(), "Lineage parameter table is inconsistent or exceeds 64 automatable params");
static_assert(static_cast<int>(ParamId::subLevel) == 7, "the eight macros are the first eight parameters");
static_assert(spec(ParamId::quality).key == "quality", "kParams order must match ParamId");
static_assert(spec(ParamId::lane4Hz).key == "lane4Hz", "lane layout");
static_assert(choices::laneTarget.size() == kLaneTargetCount);
static_assert(choices::laneMode.size() == 5);

struct Params {
    std::array<float, kParamCount> values{};

    constexpr float operator[](ParamId id) const { return values[static_cast<std::size_t>(id)]; }
    constexpr float& operator[](ParamId id) { return values[static_cast<std::size_t>(id)]; }
    constexpr int choice(ParamId id) const { return static_cast<int>((*this)[id] + 0.5f); }
    constexpr bool on(ParamId id) const { return (*this)[id] >= 0.5f; }

    static constexpr Params defaults() {
        Params p;
        for (std::size_t i = 0; i < kParams.size(); ++i)
            p.values[i] = kParams[i].def;
        return p;
    }
};

std::optional<ParamId> findParam(std::string_view key);
float toNormalised(const ParamSpec& spec, float real);
float fromNormalised(const ParamSpec& spec, float normalised);
float clampToRange(const ParamSpec& spec, float real);
std::string formatValue(const ParamSpec& spec, float real);

} // namespace ln
