// SPDX-License-Identifier: AGPL-3.0-or-later
// Prism Vein canonical parameter catalog: 45 automatable parameters (<= 64),
// zero duplicated controls across COLOR, LATTICE, TopBar, and StudioVoiceBar.
#pragma once

#include "pv/nonblocking.h"

#include <hba/core/param_math.h>
#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pv {

inline constexpr int kLaneCount = 4;
inline constexpr int kSpectrumBins = 96;
inline constexpr std::uint16_t kDefaultChordMask = 0x01A9u; // F min9: C(0), D#(3), F(5), G(7), G#(8)

enum class ParamKind : std::uint8_t { continuous, choice, toggle };
enum class EngineMode : std::uint8_t { chordLattice = 0, carrierMorph = 1, harmonicComb = 2, glassFreeze = 3 };
enum class GritType : std::uint8_t { tube = 0, tape = 1, fold = 2, buchla = 3, clip = 4 };
enum class PitchSource : std::uint8_t { internal = 0, midi = 1, midiPlusKeys = 2 };
enum class TuningMode : std::uint8_t { equal = 0, just = 1, neuro = 2 };
enum class LatencyMode : std::uint8_t { stft1024 = 0, live0ms = 1 };
enum class LaneField : std::uint8_t { on = 0, rate, phase, depth, shape, target, follow, ratio, mode, hz };
inline constexpr int kLaneFieldCount = 10;

enum class LaneTarget : std::uint8_t { color = 0, decay, tame, shift, grit, tilt, fan, morph };
inline constexpr int kLaneTargetCount = 8;

enum class LaneShape : std::uint8_t { sine = 0, triangle, rampUp, rampDown, square, steps, custom };
using LaneMode = hba::dsp::LaneMode;

enum class ParamId : std::size_t {
    // 0. Hero Macros (COLOR surface — first bank for hosts/controllers)
    color = 0,
    decay,
    tame,
    shift,
    grit,
    subLockHz,

    // 1. Engine & Performance Strip (COLOR surface)
    engineMode,
    transientShield,
    stereoFan,
    motion,

    // 2. TopBar Performance Pills
    freeze,
    midiHold,
    demoAudition,

    // 3. LATTICE — Col 01 SPECTRUM
    harmonics,
    tilt,
    qSharpness,
    detune,
    octaveShift,
    glideMs,

    // 4. LATTICE — Col 02 CARRIER · SHIELD
    morphBlend,
    carrierTilt,
    shieldSens,
    inHarmMix,
    shimmer,

    // 5. LATTICE — Col 03 TAME · GRIT
    tameFocusHz,
    tameSelect,
    gritType,
    airDb,
    subLevelDb,

    // 6. LATTICE — Col 04 OUTPUT
    inputDb,
    outputDb,
    ceilingDb,
    limitOn,
    velToColor,

    // 7. LATTICE — 4 Modulation Lanes (10 fields each; on/phase/depth automatable)
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

    // 8. StudioVoiceBar Footer (non-automatable)
    pitchSource,
    tuningMode,
    latencyMode,
    subMono,
    quality,

    count
};

inline constexpr std::size_t kParamCount = static_cast<std::size_t>(ParamId::count);

constexpr ParamId laneParam(int lane, LaneField field) noexcept PV_NONBLOCKING {
    return static_cast<ParamId>(static_cast<std::size_t>(ParamId::lane1On) +
                                static_cast<std::size_t>(lane * kLaneFieldCount) + static_cast<std::size_t>(field));
}

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
inline constexpr std::array<std::string_view, 2> offOn{"Off", "On"};
inline constexpr std::array<std::string_view, 4> engineMode{"Chord Lattice", "Carrier Morph", "Harmonic Comb",
                                                            "Glass Freeze"};
inline constexpr std::array<std::string_view, 5> octaveShift{"-2 Oct", "-1 Oct", "0 Oct", "+1 Oct", "+2 Oct"};
inline constexpr std::array<std::string_view, 5> gritType{"Tube", "Tape", "Fold", "Buchla", "Clip"};
inline constexpr std::array<std::string_view, 3> pitchSource{"Internal", "MIDI", "MIDI + Keys"};
inline constexpr std::array<std::string_view, 3> tuningMode{"Equal", "Just", "Neuro"};
inline constexpr std::array<std::string_view, 2> latencyMode{"STFT 1024", "Live 0ms"};
inline constexpr std::array<std::string_view, 3> quality{"1\xc3\x97", "2\xc3\x97", "4\xc3\x97"};

inline constexpr auto& laneRate = hba::dsp::kLaneRateLabels;
inline constexpr std::array<std::string_view, 7> laneShape{"Sine",   "Triangle", "Ramp up", "Ramp down",
                                                           "Square", "Steps",    "Custom"};
inline constexpr std::array<std::string_view, 8> laneTarget{"Color", "Decay", "Tame", "Shift",
                                                            "Grit",  "Tilt",  "Fan",  "Morph"};
inline constexpr auto& laneFollow = hba::dsp::kLaneFollowLabels;
inline constexpr auto& laneRatio = hba::dsp::kLaneRatioLabels;
inline constexpr auto& laneMode = hba::dsp::kLaneModeLabels;
} // namespace choices

inline constexpr std::array<int, 3> kQualityFactors{1, 2, 4};

#define PV_LANE_SPECS(N, onDef, targetDef, rateDef, phaseDef, depthDef)                                                \
    {ParamId::lane##N##On,  "lane" #N "On", "Lane " #N,     "",  ParamKind::toggle, 0.0f, 1.0f,                        \
     (onDef) ? 1.0f : 0.0f, 1.0f,           choices::offOn, true},                                                     \
        {ParamId::lane##N##Rate,                                                                                       \
         "lane" #N "Rate",                                                                                             \
         "Lane " #N " Rate",                                                                                           \
         "",                                                                                                           \
         ParamKind::choice,                                                                                            \
         0.0f,                                                                                                         \
         11.0f,                                                                                                        \
         static_cast<float>(rateDef),                                                                                  \
         1.0f,                                                                                                         \
         choices::laneRate,                                                                                            \
         false},                                                                                                       \
        {ParamId::lane##N##Phase,                                                                                      \
         "lane" #N "Phase",                                                                                            \
         "Lane " #N " Phase",                                                                                          \
         "\xc2\xb0",                                                                                                   \
         ParamKind::continuous,                                                                                        \
         0.0f,                                                                                                         \
         360.0f,                                                                                                       \
         phaseDef,                                                                                                     \
         1.0f,                                                                                                         \
         {},                                                                                                           \
         true},                                                                                                        \
        {ParamId::lane##N##Depth,                                                                                      \
         "lane" #N "Depth",                                                                                            \
         "Lane " #N " Depth",                                                                                          \
         "%",                                                                                                          \
         ParamKind::continuous,                                                                                        \
         -1.0f,                                                                                                        \
         1.0f,                                                                                                         \
         depthDef,                                                                                                     \
         1.0f,                                                                                                         \
         {},                                                                                                           \
         true},                                                                                                        \
        {ParamId::lane##N##Shape,                                                                                      \
         "lane" #N "Shape",                                                                                            \
         "Lane " #N " Shape",                                                                                          \
         "",                                                                                                           \
         ParamKind::choice,                                                                                            \
         0.0f,                                                                                                         \
         6.0f,                                                                                                         \
         0.0f,                                                                                                         \
         1.0f,                                                                                                         \
         choices::laneShape,                                                                                           \
         false},                                                                                                       \
        {ParamId::lane##N##Target,                                                                                     \
         "lane" #N "Target",                                                                                           \
         "Lane " #N " Target",                                                                                         \
         "",                                                                                                           \
         ParamKind::choice,                                                                                            \
         0.0f,                                                                                                         \
         7.0f,                                                                                                         \
         static_cast<float>(targetDef),                                                                                \
         1.0f,                                                                                                         \
         choices::laneTarget,                                                                                          \
         false},                                                                                                       \
        {ParamId::lane##N##Follow,                                                                                     \
         "lane" #N "Follow",                                                                                           \
         "Lane " #N " Follow",                                                                                         \
         "",                                                                                                           \
         ParamKind::choice,                                                                                            \
         0.0f,                                                                                                         \
         4.0f,                                                                                                         \
         0.0f,                                                                                                         \
         1.0f,                                                                                                         \
         choices::laneFollow,                                                                                          \
         false},                                                                                                       \
        {ParamId::lane##N##Ratio,                                                                                      \
         "lane" #N "Ratio",                                                                                            \
         "Lane " #N " Ratio",                                                                                          \
         "",                                                                                                           \
         ParamKind::choice,                                                                                            \
         0.0f,                                                                                                         \
         6.0f,                                                                                                         \
         3.0f,                                                                                                         \
         1.0f,                                                                                                         \
         choices::laneRatio,                                                                                           \
         false},                                                                                                       \
        {ParamId::lane##N##Mode,                                                                                       \
         "lane" #N "Mode",                                                                                             \
         "Lane " #N " Mode",                                                                                           \
         "",                                                                                                           \
         ParamKind::choice,                                                                                            \
         0.0f,                                                                                                         \
         4.0f,                                                                                                         \
         0.0f,                                                                                                         \
         1.0f,                                                                                                         \
         choices::laneMode,                                                                                            \
         false},                                                                                                       \
    {                                                                                                                  \
        ParamId::lane##N##Hz, "lane" #N "Hz", "Lane " #N " Hz", "Hz", ParamKind::continuous, hba::dsp::kLaneMinHz,     \
            hba::dsp::kLaneMaxHz, hba::dsp::kLaneDefaultHz, 0.35f, {}, false                                           \
    }

inline constexpr std::array<ParamSpec, kParamCount> kParamSpecs{{
    // 0. Hero Macros (COLOR)
    {ParamId::color, "color", "Color", "%", ParamKind::continuous, 0.0f, 1.0f, 0.80f, 1.0f},
    {ParamId::decay, "decay", "Decay", "%", ParamKind::continuous, 0.0f, 1.0f, 0.44f, 1.0f},
    {ParamId::tame, "tame", "Tame", "%", ParamKind::continuous, 0.0f, 1.0f, 0.48f, 1.0f},
    {ParamId::shift, "shift", "Shift", "st", ParamKind::continuous, -24.0f, 24.0f, 0.0f, 1.0f},
    {ParamId::grit, "grit", "Grit", "%", ParamKind::continuous, 0.0f, 1.0f, 0.24f, 1.0f},
    {ParamId::subLockHz, "subLockHz", "Sub Lock", "Hz", ParamKind::continuous, 50.0f, 240.0f, 105.0f, 0.50f},

    // 1. Engine & Performance Strip (COLOR)
    {ParamId::engineMode, "engineMode", "Engine", "", ParamKind::choice, 0.0f, 3.0f, 0.0f, 1.0f, choices::engineMode},
    {ParamId::transientShield, "transientShield", "Shield", "%", ParamKind::continuous, 0.0f, 1.0f, 0.58f, 1.0f},
    {ParamId::stereoFan, "stereoFan", "Fan", "%", ParamKind::continuous, 0.0f, 1.0f, 0.52f, 1.0f},
    {ParamId::motion, "motion", "Motion", "%", ParamKind::continuous, 0.0f, 1.0f, 0.50f, 1.0f},

    // 2. TopBar Pills
    {ParamId::freeze, "freeze", "Freeze", "", ParamKind::toggle, 0.0f, 1.0f, 0.0f, 1.0f, choices::offOn},
    {ParamId::midiHold, "midiHold", "Hold", "", ParamKind::toggle, 0.0f, 1.0f, 0.0f, 1.0f, choices::offOn},
    {ParamId::demoAudition, "demoAudition", "Demo", "", ParamKind::toggle, 0.0f, 1.0f, 0.0f, 1.0f, choices::offOn,
     false},

    // 3. LATTICE — Col 01 SPECTRUM
    {ParamId::harmonics, "harmonics", "Harmonics", "", ParamKind::continuous, 1.0f, 16.0f, 10.0f, 1.0f},
    {ParamId::tilt, "tilt", "Tilt", "%", ParamKind::continuous, -1.0f, 1.0f, 0.16f, 1.0f},
    {ParamId::qSharpness, "qSharpness", "Focus Q", "%", ParamKind::continuous, 0.0f, 1.0f, 0.64f, 1.0f},
    {ParamId::detune, "detune", "Detune", "ct", ParamKind::continuous, 0.0f, 50.0f, 9.0f, 0.70f},
    {ParamId::octaveShift, "octaveShift", "Octave", "", ParamKind::choice, 0.0f, 4.0f, 2.0f, 1.0f,
     choices::octaveShift},
    {ParamId::glideMs, "glideMs", "Glide", "ms", ParamKind::continuous, 0.0f, 300.0f, 24.0f, 0.45f},

    // 4. LATTICE — Col 02 CARRIER · SHIELD
    {ParamId::morphBlend, "morphBlend", "Morph", "%", ParamKind::continuous, 0.0f, 1.0f, 0.62f, 1.0f},
    {ParamId::carrierTilt, "carrierTilt", "SC Tilt", "%", ParamKind::continuous, -1.0f, 1.0f, 0.0f, 1.0f},
    {ParamId::shieldSens, "shieldSens", "Sens", "%", ParamKind::continuous, 0.0f, 1.0f, 0.58f, 1.0f},
    {ParamId::inHarmMix, "inHarmMix", "Source", "%", ParamKind::continuous, 0.0f, 1.0f, 0.16f, 1.0f},
    {ParamId::shimmer, "shimmer", "Shimmer", "%", ParamKind::continuous, 0.0f, 1.0f, 0.18f, 1.0f},

    // 5. LATTICE — Col 03 TAME · GRIT
    {ParamId::tameFocusHz, "tameFocusHz", "Tame Hz", "Hz", ParamKind::continuous, 800.0f, 9000.0f, 3400.0f, 0.42f},
    {ParamId::tameSelect, "tameSelect", "Select", "%", ParamKind::continuous, 0.0f, 1.0f, 0.58f, 1.0f},
    {ParamId::gritType, "gritType", "Shaper", "", ParamKind::choice, 0.0f, 4.0f, 0.0f, 1.0f, choices::gritType},
    {ParamId::airDb, "airDb", "Air", "dB", ParamKind::continuous, -6.0f, 9.0f, 1.5f, 1.0f},
    {ParamId::subLevelDb, "subLevelDb", "Sub Gain", "dB", ParamKind::continuous, -12.0f, 6.0f, 0.0f, 1.0f},

    // 6. LATTICE — Col 04 OUTPUT
    {ParamId::inputDb, "inputDb", "Input", "dB", ParamKind::continuous, -18.0f, 18.0f, 0.0f, 1.0f},
    {ParamId::outputDb, "outputDb", "Output", "dB", ParamKind::continuous, -18.0f, 12.0f, 0.0f, 1.0f},
    {ParamId::ceilingDb, "ceilingDb", "Ceiling", "dB", ParamKind::continuous, -6.0f, 0.0f, -0.3f, 1.0f},
    {ParamId::limitOn, "limitOn", "Limit", "", ParamKind::toggle, 0.0f, 1.0f, 1.0f, 1.0f, choices::offOn},
    {ParamId::velToColor, "velToColor", "Vel Color", "%", ParamKind::continuous, 0.0f, 1.0f, 0.35f, 1.0f},

    // 7. 4 Modulation Lanes
    PV_LANE_SPECS(1, true, LaneTarget::color, 3, 0.0f, 0.25f),
    PV_LANE_SPECS(2, false, LaneTarget::shift, 4, 90.0f, 0.30f),
    PV_LANE_SPECS(3, false, LaneTarget::tilt, 2, 180.0f, 0.35f),
    PV_LANE_SPECS(4, false, LaneTarget::fan, 5, 270.0f, 0.28f),

    // 8. StudioVoiceBar Footer (non-automatable)
    {ParamId::pitchSource, "pitchSource", "Source", "", ParamKind::choice, 0.0f, 2.0f, 2.0f, 1.0f, choices::pitchSource,
     false},
    {ParamId::tuningMode, "tuningMode", "Tuning", "", ParamKind::choice, 0.0f, 2.0f, 0.0f, 1.0f, choices::tuningMode,
     false},
    {ParamId::latencyMode, "latencyMode", "Latency", "", ParamKind::choice, 0.0f, 1.0f, 0.0f, 1.0f,
     choices::latencyMode, false},
    {ParamId::subMono, "subMono", "Sub Mono", "", ParamKind::toggle, 0.0f, 1.0f, 1.0f, 1.0f, choices::offOn, false},
    {ParamId::quality, "quality", "OS", "", ParamKind::choice, 0.0f, 2.0f, 1.0f, 1.0f, choices::quality, false},
}};

#undef PV_LANE_SPECS

static_assert(
    [] {
        std::size_t autoCount = 0;
        for (std::size_t i = 0; i < kParamCount; ++i) {
            const auto& s = kParamSpecs[i];
            if (static_cast<std::size_t>(s.id) != i)
                return false;
            if (s.key.empty() || s.name.empty())
                return false;
            if (!(s.low < s.high) || s.def < s.low || s.def > s.high)
                return false;
            if (s.kind != ParamKind::continuous && s.choices.size() != static_cast<std::size_t>(s.high) + 1)
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (kParamSpecs[j].key == s.key)
                    return false;
            if (s.automatable)
                ++autoCount;
        }
        return autoCount <= 64;
    }(),
    "Prism Vein ParamSpec table must be ordered, unique, valid, and <= 64 automatable parameters.");

inline constexpr const ParamSpec& spec(ParamId id) noexcept PV_NONBLOCKING {
    return kParamSpecs[static_cast<std::size_t>(id)];
}

inline constexpr const ParamSpec& specOf(ParamId id) noexcept PV_NONBLOCKING { return spec(id); }

inline float clampParam(const ParamSpec& s, float v) noexcept PV_NONBLOCKING {
    return hba::core::clampParam(v, s.low, s.high, s.def, s.kind == ParamKind::continuous);
}

inline float clampToRange(const ParamSpec& s, float v) noexcept PV_NONBLOCKING { return clampParam(s, v); }

inline float toNormalised(const ParamSpec& s, float real) noexcept PV_NONBLOCKING {
    return hba::core::toNormalisedJuceSkew(real, s.low, s.high, s.def, s.skew, s.kind == ParamKind::continuous);
}

inline float fromNormalised(const ParamSpec& s, float norm) noexcept PV_NONBLOCKING {
    return hba::core::fromNormalisedJuceSkew(norm, s.low, s.high, s.def, s.skew, s.kind == ParamKind::continuous);
}

struct Params {
    std::array<float, kParamCount> values{};

    static constexpr Params defaults() noexcept PV_NONBLOCKING {
        Params p;
        for (std::size_t i = 0; i < kParamCount; ++i)
            p.values[i] = kParamSpecs[i].def;
        return p;
    }

    constexpr float& operator[](ParamId id) noexcept PV_NONBLOCKING { return values[static_cast<std::size_t>(id)]; }
    constexpr float operator[](ParamId id) const noexcept PV_NONBLOCKING {
        return values[static_cast<std::size_t>(id)];
    }
    constexpr float real(ParamId id) const noexcept PV_NONBLOCKING { return values[static_cast<std::size_t>(id)]; }
    void set(ParamId id, float v) noexcept PV_NONBLOCKING {
        values[static_cast<std::size_t>(id)] = clampParam(spec(id), v);
    }
    constexpr int choice(ParamId id) const noexcept PV_NONBLOCKING {
        const auto& s = spec(id);
        return std::clamp(static_cast<int>(real(id) + 0.5f), static_cast<int>(s.low), static_cast<int>(s.high));
    }
    constexpr bool on(ParamId id) const noexcept PV_NONBLOCKING { return real(id) >= 0.5f; }
    constexpr bool flag(ParamId id) const noexcept PV_NONBLOCKING { return on(id); }
};

struct ChordPreset {
    std::string_view label;
    std::uint16_t mask;
};

inline constexpr std::array<ChordPreset, 8> kChordPresets{{
    {"F m9", 0x01A9u},    // C, D#, F, G, G#
    {"Eb M9", 0x059Au},   // D, D#, F, G, A#
    {"F Phryg", 0x0161u}, // C, F, F#, G#
    {"Ab Lyd", 0x0998u},  // D, D#, G, G#, A#
    {"C m11", 0x04ADu},   // C, D, D#, F, G, A#
    {"D sus2", 0x0214u},  // D, E, A
    {"Tritone", 0x0861u}, // C, F, F#, B
    {"F 5th", 0x0021u},   // C, F
}};

std::optional<ParamId> findParam(std::string_view key);
std::string formatValue(const ParamSpec& s, float real);
std::string describeChordMask(std::uint16_t mask);

} // namespace pv
