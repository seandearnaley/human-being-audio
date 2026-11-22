// SPDX-License-Identifier: AGPL-3.0-or-later
// Single source of truth for Cutline's parameter contract (54 total params,
// 8 performance macros first for Ableton Live / controller bank 1).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cl {

enum class ParamId : std::uint8_t {
    // Bank 1: eight performance macros
    grammar,
    gravity,
    ratchet,
    glide,
    density,
    mutate,
    motion,
    velArc,

    // Phrase, timing, pitch, and delivery controls
    outputMode,
    inputMode,
    phraseBars,
    seed,
    attractor,
    rootNote,
    octave,
    scale,
    bendRange,
    legato,
    shiftMs,
    audition,
    auditionGain,
    ccThinMs,

    // Four tempo-locked MIDI/MPE expression lanes
    lane1On,
    lane1Rate,
    lane1Phase,
    lane1Depth,
    lane1Shape,
    lane1Target,
    lane1Follow,
    lane1Ratio,
    lane2On,
    lane2Rate,
    lane2Phase,
    lane2Depth,
    lane2Shape,
    lane2Target,
    lane2Follow,
    lane2Ratio,
    lane3On,
    lane3Rate,
    lane3Phase,
    lane3Depth,
    lane3Shape,
    lane3Target,
    lane3Follow,
    lane3Ratio,
    lane4On,
    lane4Rate,
    lane4Phase,
    lane4Depth,
    lane4Shape,
    lane4Target,
    lane4Follow,
    lane4Ratio,
    count
};

inline constexpr int kParamCount = static_cast<int>(ParamId::count);
static_assert(kParamCount <= 64, "Cutline stays within Ableton Live's 64-parameter direct configure limit");

inline constexpr int kLaneCount = 4;

enum class LaneField : std::uint8_t { on, rate, phase, depth, shape, target, follow, ratio };
inline constexpr int kLaneFieldCount = 8;

enum class LaneTarget : std::uint8_t {
    pitchBend = 0,
    cc74Slide = 1,
    cc1Mod = 2,
    pressure = 3,
    cc11Expr = 4,
    cc2Breath = 5,
    cc71Res = 6,
    velocity = 7,
};
inline constexpr int kLaneTargetCount = 8;

constexpr ParamId laneParam(int lane, LaneField field) noexcept {
    return static_cast<ParamId>(static_cast<int>(ParamId::lane1On) + lane * kLaneFieldCount + static_cast<int>(field));
}

enum class Grammar : std::uint8_t { neuro = 0, bbcut11 = 1, warpcut = 2, halftime = 3, liquid = 4 };
enum class OutputMode : std::uint8_t { mono = 0, mpe = 1, drum = 2 };
enum class InputMode : std::uint8_t { seed = 0, liveMidi = 1 };
enum class Attractor : std::uint8_t { downbeat = 0, backbeat = 1, offbeat16 = 2, triplet = 3 };
enum class Scale : std::uint8_t { phrygian = 0, minor = 1, phrygianDom = 2, dorian = 3, pentMinor = 4, chromatic = 5 };
enum class LaneShape : std::uint8_t { sine, triangle, rampUp, rampDown, square, steps, custom };

enum class ParamKind : std::uint8_t { continuous, toggle, choice };
enum class Unit : std::uint8_t { none, percent, ms, degrees, dB, integer };

struct ParamSpec {
    std::string_view key;
    std::string_view name;
    ParamKind kind = ParamKind::continuous;
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    Unit unit = Unit::none;
    float skew = 1.0f;
    std::span<const std::string_view> choices = {};
    bool automatable = true;
};

namespace choices {
inline constexpr std::array<std::string_view, 2> offOn{"Off", "On"};
inline constexpr std::array<std::string_view, 5> grammar{"Neuro", "BBCut11", "WarpCut", "Halftime", "Liquid"};
inline constexpr std::array<std::string_view, 3> outputMode{"Mono Ch1", "MPE Zone", "Drum Kit"};
inline constexpr std::array<std::string_view, 2> inputMode{"Seed", "Live MIDI"};
inline constexpr std::array<std::string_view, 3> phraseBars{"2 bars", "4 bars", "8 bars"};
inline constexpr std::array<std::string_view, 4> attractor{"Downbeat", "Backbeat", "Offbeat 16", "Triplet"};
inline constexpr std::array<std::string_view, 12> rootNote{"C",  "C#", "D",  "D#", "E",  "F",
                                                           "F#", "G",  "G#", "A",  "A#", "B"};
inline constexpr std::array<std::string_view, 4> octave{"1", "2", "3", "4"};
inline constexpr std::array<std::string_view, 6> scale{"Phrygian", "Minor",    "Phryg Dom",
                                                       "Dorian",   "Pent Min", "Chromatic"};
inline constexpr std::array<std::string_view, 3> bendRange{"\u00b12 st", "\u00b112 st", "\u00b148 st"};
inline constexpr std::array<std::string_view, 4> ccThinMs{"1 ms", "2 ms", "4 ms", "8 ms"};
inline constexpr std::array<std::string_view, 12> laneRate{"8 bars", "4 bars", "2 bars", "1 bar", "1/2",   "1/4",
                                                           "1/4T",   "1/8",    "1/8T",   "1/16",  "1/16T", "1/32"};
inline constexpr std::array<std::string_view, 7> laneShape{"Sine",   "Triangle", "Ramp up", "Ramp down",
                                                           "Square", "Steps",    "Custom"};
inline constexpr std::array<std::string_view, 8> laneTarget{"Pitch Bend", "CC74 Slide", "CC1 Mod",  "Pressure",
                                                            "CC11 Expr",  "CC2 Breath", "CC71 Res", "Velocity"};
inline constexpr std::array<std::string_view, 5> laneFollow{"Own", "Lane 1", "Lane 2", "Lane 3", "Lane 4"};
inline constexpr std::array<std::string_view, 7> laneRatio{"\u00f74", "\u00f73", "\u00f72", "\u00d71",
                                                           "\u00d72", "\u00d73", "\u00d74"};
} // namespace choices

inline constexpr std::array<int, 3> kPhraseBarCounts{2, 4, 8};
inline constexpr std::array<int, 3> kBendRangeSemitones{2, 12, 48};
inline constexpr std::array<double, 4> kCcThinMsValues{1.0, 2.0, 4.0, 8.0};
inline constexpr std::array<double, 12> kLaneRateQuarters{32.0,    16.0, 8.0,     4.0,  2.0,     1.0,
                                                          2.0 / 3, 0.5,  1.0 / 3, 0.25, 1.0 / 6, 0.125};
inline constexpr std::array<double, 7> kLaneRatios{0.25, 1.0 / 3, 0.5, 1.0, 2.0, 3.0, 4.0};

namespace detail {
constexpr ParamSpec amount(std::string_view key, std::string_view name, float def) {
    return {.key = key, .name = name, .def = def, .unit = Unit::percent};
}
constexpr ParamSpec bipolar(std::string_view key, std::string_view name, float def) {
    return {.key = key, .name = name, .min = -1.0f, .def = def, .unit = Unit::percent};
}
constexpr ParamSpec toggle(std::string_view key, std::string_view name, bool def) {
    return {.key = key, .name = name, .kind = ParamKind::toggle, .def = def ? 1.0f : 0.0f, .choices = choices::offOn};
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
                           float skew = 1.0f) {
    return {.key = key, .name = name, .min = min, .max = max, .def = def, .unit = unit, .skew = skew};
}
} // namespace detail

#define CL_LANE(n, onDef, target, rate, phase, depth)                                                                  \
    detail::toggle("lane" #n "On", "Lane " #n, onDef),                                                                 \
        detail::choice("lane" #n "Rate", "Lane " #n " rate", choices::laneRate, rate, false),                          \
        detail::ranged("lane" #n "Phase", "Lane " #n " phase", 0.0f, 360.0f, phase, Unit::degrees),                    \
        detail::bipolar("lane" #n "Depth", "Lane " #n " depth", depth),                                                \
        detail::choice("lane" #n "Shape", "Lane " #n " shape", choices::laneShape, 0, false),                          \
        detail::choice("lane" #n "Target", "Lane " #n " target", choices::laneTarget, static_cast<int>(target),        \
                       false),                                                                                         \
        detail::choice("lane" #n "Follow", "Lane " #n " follow", choices::laneFollow, 0, false),                       \
        detail::choice("lane" #n "Ratio", "Lane " #n " ratio", choices::laneRatio, 3, false)

// Order must match ParamId.
inline constexpr std::array<ParamSpec, kParamCount> kParams{{
    detail::choice("grammar", "Grammar", choices::grammar, 0, true),
    detail::bipolar("gravity", "Gravity", 0.34f),
    detail::amount("ratchet", "Ratchet", 0.55f),
    detail::amount("glide", "Glide", 0.68f),
    detail::amount("density", "Density", 0.62f),
    detail::amount("mutate", "Mutate", 0.48f),
    detail::amount("motion", "Motion", 0.75f),
    detail::amount("velArc", "Vel arc", 0.65f),

    detail::choice("outputMode", "Output", choices::outputMode, 0, false),
    detail::choice("inputMode", "Input", choices::inputMode, 0, false),
    detail::choice("phraseBars", "Bars", choices::phraseBars, 1, false),
    detail::ranged("seed", "Seed", 1.0f, 999.0f, 407.0f, Unit::integer),
    detail::choice("attractor", "Pocket", choices::attractor, 1, true),
    detail::choice("rootNote", "Root", choices::rootNote, 5, false),
    detail::choice("octave", "Octave", choices::octave, 1, false),
    detail::choice("scale", "Scale", choices::scale, 0, false),
    detail::choice("bendRange", "Bend rng", choices::bendRange, 1, false),
    detail::amount("legato", "Legato", 0.55f),
    detail::ranged("shiftMs", "Shift", -25.0f, 25.0f, 0.0f, Unit::ms),
    detail::toggle("audition", "Audition", true),
    detail::ranged("auditionGain", "Aud gain", -36.0f, 0.0f, -9.0f, Unit::dB),
    detail::choice("ccThinMs", "CC rate", choices::ccThinMs, 1, false),

    CL_LANE(1, true, LaneTarget::pitchBend, 1, 0.0f, 0.32f),
    CL_LANE(2, true, LaneTarget::cc74Slide, 2, 0.0f, 0.76f),
    CL_LANE(3, true, LaneTarget::cc1Mod, 3, 90.0f, 0.58f),
    CL_LANE(4, true, LaneTarget::pressure, 1, 45.0f, 0.64f),
}};

#undef CL_LANE

constexpr const ParamSpec& spec(ParamId id) noexcept { return kParams[static_cast<std::size_t>(id)]; }

struct Params {
    std::array<float, kParamCount> values{};

    constexpr Params() noexcept {
        for (std::size_t i = 0; i < kParams.size(); ++i)
            values[i] = kParams[i].def;
    }

    constexpr float operator[](ParamId id) const noexcept { return values[static_cast<std::size_t>(id)]; }
    constexpr float& operator[](ParamId id) noexcept { return values[static_cast<std::size_t>(id)]; }

    constexpr bool on(ParamId id) const noexcept { return (*this)[id] >= 0.5f; }
    constexpr int choice(ParamId id) const noexcept { return static_cast<int>((*this)[id] + 0.5f); }
    constexpr int integer(ParamId id) const noexcept { return static_cast<int>((*this)[id] + 0.5f); }
};

std::optional<ParamId> findParam(std::string_view key);
float clampToRange(const ParamSpec& spec, float real);
float toNormalised(const ParamSpec& spec, float real);
float fromNormalised(const ParamSpec& spec, float normalised);
std::string formatValue(const ParamSpec& spec, float real);

} // namespace cl
