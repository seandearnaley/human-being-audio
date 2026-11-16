// The parameter table. The engine's Params, the plugin's host parameters and the
// interface's labels all derive from it, so a parameter is defined exactly once.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace tf {

enum class ParamId : std::uint8_t {
    // The eight macros come first, in knob order: hosts and controllers map a
    // plugin's first eight parameters to their first bank.
    coupling, beat, reach, tract, warp, sculpt, motion, width,
    glide, phaseReset, beatLock,
    aDetune, bDetune, aRatio, bRatio, aPhase, bPhase, aPm, bPm,
    fbTime, fbAmount,
    vowelA, vowelB, morph,
    foldType, drive, asym, weight,
    sculptDepth, sculptCount, sculptSpread, sculptRes,
    midGain, sideGain, xover,
    subFreq, fit, subLevel, growlLevel, squash,
    limit, ceiling, quality,
    lane1On, lane1Rate, lane1Phase, lane1Depth, lane1Shape, lane1Target, lane1Follow, lane1Ratio,
    lane2On, lane2Rate, lane2Phase, lane2Depth, lane2Shape, lane2Target, lane2Follow, lane2Ratio,
    lane3On, lane3Rate, lane3Phase, lane3Depth, lane3Shape, lane3Target, lane3Follow, lane3Ratio,
    lane4On, lane4Rate, lane4Phase, lane4Depth, lane4Shape, lane4Target, lane4Follow, lane4Ratio,
    count
};

inline constexpr int kParamCount = static_cast<int>(ParamId::count);
inline constexpr int kLaneCount = 4;

enum class LaneField : std::uint8_t { on, rate, phase, depth, shape, target, follow, ratio };
inline constexpr int kLaneFieldCount = 8;

// What a lane moves. Order matches choices::laneTarget.
enum class LaneTarget : std::uint8_t { phase, morph, drive, beat, coupling, reach, weight, sculpt, fit, width };
inline constexpr int kLaneTargetCount = 10;

constexpr ParamId laneParam(int lane, LaneField field) {
    return static_cast<ParamId>(static_cast<int>(ParamId::lane1On) + lane * kLaneFieldCount + static_cast<int>(field));
}

enum class ParamKind : std::uint8_t { continuous, toggle, choice };

// Unit decides how a value is shown. `percent` shows a 0..1 (or -1..1) value
// multiplied by 100.
enum class Unit : std::uint8_t { none, percent, ms, cents, degrees, dB, hz, ratio, harmonic };

struct ParamSpec {
    std::string_view key;
    std::string_view name;
    ParamKind kind = ParamKind::continuous;
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    Unit unit = Unit::none;
    // real = min + (max - min) * normalised^skew. Zero means logarithmic.
    float skew = 1.0f;
    std::span<const std::string_view> choices = {};
    bool automatable = true;
};

namespace choices {
inline constexpr std::array<std::string_view, 2> offOn { "Off", "On" };
// Feedback delay in cycles of the note: the loop is a comb on its harmonics.
inline constexpr std::array<std::string_view, 5> fbTime { "1/4 cyc", "1/2 cyc", "1 cyc", "2 cyc", "4 cyc" };
// What the Beat macro selects while Beat lock is on: one roll per this length.
inline constexpr std::array<std::string_view, 9> beatLock {
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32"
};
inline constexpr std::array<std::string_view, 6> vowel { "a", "æ", "e", "i", "o", "u" };
inline constexpr std::array<std::string_view, 3> foldType { "Fold", "Sine", "Tube" };
inline constexpr std::array<std::string_view, 6> sculptCount { "1", "2", "3", "4", "5", "6" };
inline constexpr std::array<std::string_view, 3> quality { "2×", "4×", "8×" };
inline constexpr std::array<std::string_view, 12> laneRate {
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"
};
// "Custom" plays the lane's drawn curve.
inline constexpr std::array<std::string_view, 7> laneShape {
    "Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Steps", "Custom"
};
inline constexpr std::array<std::string_view, 10> laneTarget {
    "Phase", "Morph", "Drive", "Beat", "Coupling", "Reach", "Weight", "Sculpt", "Fit", "Width"
};
// A lane plays its own shape, or follows another lane's shape at a ratio of its rate.
inline constexpr std::array<std::string_view, 5> laneFollow { "Own", "Lane 1", "Lane 2", "Lane 3", "Lane 4" };
inline constexpr std::array<std::string_view, 7> laneRatio { "\u00f74", "\u00f73", "\u00f72", "\u00d71", "\u00d72", "\u00d73", "\u00d74" };
}

// Feedback delay per choice, in cycles of the note.
inline constexpr std::array<double, 5> kFbCycles { 0.25, 0.5, 1.0, 2.0, 4.0 };
// Quarter notes per roll for each Beat lock division.
inline constexpr std::array<double, 9> kBeatLockQuarters { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
inline constexpr std::array<double, 12> kLaneRateQuarters {
    32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 2.0 / 3, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125
};
inline constexpr std::array<int, 3> kQualityFactors { 2, 4, 8 };
// How many times faster than its leader a following lane runs.
inline constexpr std::array<double, 7> kLaneRatios { 0.25, 1.0 / 3, 0.5, 1.0, 2.0, 3.0, 4.0 };

enum class FoldType : std::uint8_t { fold, sine, tube };
enum class LaneShape : std::uint8_t { sine, triangle, rampUp, rampDown, square, steps, custom };

namespace detail {
constexpr ParamSpec amount(std::string_view key, std::string_view name, float def) {
    return { .key = key, .name = name, .def = def, .unit = Unit::percent };
}
constexpr ParamSpec bipolar(std::string_view key, std::string_view name, float def) {
    return { .key = key, .name = name, .min = -1.0f, .def = def, .unit = Unit::percent };
}
constexpr ParamSpec toggle(std::string_view key, std::string_view name, bool def) {
    return { .key = key, .name = name, .kind = ParamKind::toggle, .def = def ? 1.0f : 0.0f, .choices = choices::offOn };
}
constexpr ParamSpec choice(std::string_view key, std::string_view name, std::span<const std::string_view> list, int def,
                           bool automatable = true) {
    return { .key = key, .name = name, .kind = ParamKind::choice, .max = static_cast<float>(list.size() - 1),
             .def = static_cast<float>(def), .choices = list, .automatable = automatable };
}
constexpr ParamSpec ranged(std::string_view key, std::string_view name, float min, float max, float def, Unit unit,
                           float skew = 1.0f) {
    return { .key = key, .name = name, .min = min, .max = max, .def = def, .unit = unit, .skew = skew };
}
}

// Only a lane's depth and phase are automatable: the rest shapes the lane rather
// than performs it, and Live shows an empty device panel above 64 parameters.
#define TF_LANE(n, target, rate, phase, depth)                                                                   \
    detail::toggle("lane" #n "On", "Lane " #n, true),                                                            \
    detail::choice("lane" #n "Rate", "Lane " #n " rate", choices::laneRate, rate, false),                        \
    detail::ranged("lane" #n "Phase", "Lane " #n " phase", 0.0f, 360.0f, phase, Unit::degrees),                  \
    detail::bipolar("lane" #n "Depth", "Lane " #n " depth", depth),                                              \
    detail::choice("lane" #n "Shape", "Lane " #n " shape", choices::laneShape, 0, false),                        \
    detail::choice("lane" #n "Target", "Lane " #n " target", choices::laneTarget, static_cast<int>(target), false), \
    detail::choice("lane" #n "Follow", "Lane " #n " follow", choices::laneFollow, 0, false),                     \
    detail::choice("lane" #n "Ratio", "Lane " #n " ratio", choices::laneRatio, 3, false)

// Order must match ParamId.
inline constexpr std::array<ParamSpec, kParamCount> kParams { {
    detail::amount("coupling", "Coupling", 0.72f),
    detail::amount("beat", "Beat", 0.38f),
    detail::ranged("reach", "Reach", 2.0f, 256.0f, 96.0f, Unit::harmonic, 0.0f),
    detail::amount("tract", "Tract", 0.61f),
    detail::amount("warp", "Warp", 0.46f),
    detail::ranged("sculpt", "Sculpt", 1.5f, 48.0f, 6.0f, Unit::harmonic, 0.0f),
    detail::amount("motion", "Motion", 0.78f),
    detail::amount("width", "Width", 0.64f),
    detail::ranged("glide", "Glide", 0.0f, 500.0f, 12.5f, Unit::ms, 3.0f),
    detail::toggle("phaseReset", "Phase reset", true),
    detail::toggle("beatLock", "Beat lock", false),
    detail::ranged("aDetune", "A detune", -50.0f, 50.0f, 19.0f, Unit::cents),
    detail::ranged("bDetune", "B detune", -50.0f, 50.0f, -19.0f, Unit::cents),
    detail::ranged("aRatio", "A ratio", 0.25f, 8.0f, 1.0f, Unit::ratio, 0.0f),
    detail::ranged("bRatio", "B ratio", 0.25f, 8.0f, 1.0f, Unit::ratio, 0.0f),
    detail::ranged("aPhase", "A phase", 0.0f, 360.0f, 0.0f, Unit::degrees),
    detail::ranged("bPhase", "B phase", 0.0f, 360.0f, 180.0f, Unit::degrees),
    detail::bipolar("aPm", "A PM", 0.62f),
    detail::bipolar("bPm", "B PM", -0.48f),
    detail::choice("fbTime", "Feedback time", choices::fbTime, 2),
    detail::amount("fbAmount", "Feedback", 0.49f),
    detail::choice("vowelA", "Vowel A", choices::vowel, 1),
    detail::choice("vowelB", "Vowel B", choices::vowel, 5),
    detail::amount("morph", "Morph", 0.63f),
    detail::choice("foldType", "Fold type", choices::foldType, 0),
    detail::amount("drive", "Drive", 0.42f),
    detail::amount("asym", "Asym", 0.18f),
    detail::amount("weight", "Weight", 0.3f),
    detail::bipolar("sculptDepth", "Sculpt depth", -0.5f),
    detail::choice("sculptCount", "Sculpt bands", choices::sculptCount, 2),
    detail::ranged("sculptSpread", "Sculpt spread", 1.2f, 4.0f, 2.0f, Unit::ratio, 0.0f),
    detail::amount("sculptRes", "Sculpt resonance", 0.5f),
    detail::ranged("midGain", "Mid", -12.0f, 12.0f, 0.0f, Unit::dB),
    detail::ranged("sideGain", "Side", -12.0f, 12.0f, 4.2f, Unit::dB),
    detail::ranged("xover", "Crossover", 60.0f, 300.0f, 100.0f, Unit::hz, 0.0f),
    detail::ranged("subFreq", "Sub low-pass", 40.0f, 200.0f, 120.0f, Unit::hz, 0.0f),
    detail::ranged("fit", "Fit", 0.0f, 360.0f, 0.0f, Unit::degrees),
    detail::amount("subLevel", "Sub", 0.5f),
    detail::amount("growlLevel", "Growl", 0.76f),
    detail::amount("squash", "Squash", 0.3f),
    detail::toggle("limit", "Limit", true),
    detail::ranged("ceiling", "Ceiling", -12.0f, 0.0f, -0.3f, Unit::dB),
    detail::choice("quality", "Quality", choices::quality, 1, false),
    TF_LANE(1, LaneTarget::phase, 7, 0.0f, 0.62f),
    TF_LANE(2, LaneTarget::morph, 5, 90.0f, 0.79f),
    TF_LANE(3, LaneTarget::drive, 7, 180.0f, 0.46f),
    TF_LANE(4, LaneTarget::beat, 4, 270.0f, 0.38f),
} };

#undef TF_LANE

constexpr const ParamSpec& spec(ParamId id) { return kParams[static_cast<std::size_t>(id)]; }

// The table is checked at compile time: a bad entry fails the build.
namespace detail {
constexpr bool tableIsValid() {
    int automatable = 0;
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        const auto& p = kParams[i];
        if (p.key.empty() || p.name.empty()) return false;
        if (!(p.min < p.max)) return false;
        if (p.def < p.min || p.def > p.max) return false;
        if (p.skew < 0.0f) return false;
        if (p.skew == 0.0f && p.min <= 0.0f) return false;
        if (p.kind != ParamKind::continuous && p.choices.size() != static_cast<std::size_t>(p.max) + 1) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (kParams[j].key == p.key) return false;
        automatable += p.automatable ? 1 : 0;
    }
    // Ableton Live shows an empty device panel above 64 automatable parameters.
    return automatable <= 64;
}
}
static_assert(detail::tableIsValid(), "parameter table is inconsistent");
static_assert(spec(ParamId::lane4Shape).key == "lane4Shape", "kParams order must match ParamId");
static_assert(spec(ParamId::lane4Ratio).key == "lane4Ratio" && spec(laneParam(2, LaneField::target)).key == "lane3Target", "lane layout");
static_assert(choices::laneTarget.size() == kLaneTargetCount && kLaneRatios.size() == choices::laneRatio.size());
static_assert(spec(ParamId::quality).key == "quality", "kParams order must match ParamId");
static_assert(spec(ParamId::reach).key == "reach" && spec(ParamId::beatLock).key == "beatLock", "kParams order");
static_assert(spec(ParamId::weight).key == "weight" && spec(ParamId::width).key == "width", "kParams order");
static_assert(spec(ParamId::sculpt).key == "sculpt" && spec(ParamId::sculptRes).key == "sculptRes", "kParams order");
static_assert(static_cast<int>(ParamId::width) == 7, "the eight macros are the first eight parameters");
static_assert(spec(ParamId::fit).key == "fit" && spec(ParamId::glide).key == "glide", "kParams order");
static_assert(kFbCycles.size() == choices::fbTime.size());
static_assert(kBeatLockQuarters.size() == choices::beatLock.size());
static_assert(kLaneRateQuarters.size() == choices::laneRate.size());

// Real parameter values, indexed by ParamId. Choices and toggles hold their index.
struct Params {
    std::array<float, kParamCount> values {};

    constexpr float operator[](ParamId id) const { return values[static_cast<std::size_t>(id)]; }
    constexpr float& operator[](ParamId id) { return values[static_cast<std::size_t>(id)]; }
    constexpr int choice(ParamId id) const { return static_cast<int>((*this)[id] + 0.5f); }
    constexpr bool on(ParamId id) const { return (*this)[id] >= 0.5f; }

    static constexpr Params defaults() {
        Params p;
        for (std::size_t i = 0; i < kParams.size(); ++i) p.values[i] = kParams[i].def;
        return p;
    }
};

std::optional<ParamId> findParam(std::string_view key);

// Normalised 0..1 position <-> real value. Choices and toggles snap to steps.
float toNormalised(const ParamSpec& spec, float real);
float fromNormalised(const ParamSpec& spec, float normalised);
float clampToRange(const ParamSpec& spec, float real);

// "72 %", "+7.2 ct", "100 Hz", "æ", "On".
std::string formatValue(const ParamSpec& spec, float real);

}
