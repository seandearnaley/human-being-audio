// The parameter table for Pole Field. The engine, host parameters and interface
// all derive from this single definition, verified at compile time.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pf {

enum class ParamId : std::uint8_t {
    // The eight performance macros come first so hosts and controllers map them
    // directly to bank 1.
    morphX, morphY, morphZ, freq, res, drive, motion, mix,
    cube, transform, topology, stages, spread, pitchTrack, glide,
    foldType, asym, drivePos,
    subLock, subSplit, fit, inputGain, outputGain, limit, ceiling, quality,
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

// What a lane modulates. Order matches choices::laneTarget.
enum class LaneTarget : std::uint8_t {
    morphX, morphY, morphZ, freq, res, transform, drive, spread, mix, fit
};
inline constexpr int kLaneTargetCount = 10;

constexpr ParamId laneParam(int lane, LaneField field) {
    return static_cast<ParamId>(static_cast<int>(ParamId::lane1On) + lane * kLaneFieldCount + static_cast<int>(field));
}

enum class ParamKind : std::uint8_t { continuous, toggle, choice };

enum class Unit : std::uint8_t { none, percent, ms, semitones, degrees, dB, hz };

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
inline constexpr std::array<std::string_view, 8> cube {
    "Neuro Chew", "Vocal Matrix", "Phase Razor", "Metal Reactor",
    "Break Sculpt", "Acid Ladder", "Subterranean", "Imprint Cube"
};
inline constexpr std::array<std::string_view, 3> topology { "Serial", "Hybrid", "Parallel" };
inline constexpr std::array<std::string_view, 4> stages { "4P", "8P", "12P", "14P" };
inline constexpr std::array<std::string_view, 5> foldType { "Tube", "Clip", "Fold", "Buchla", "Sine" };
inline constexpr std::array<std::string_view, 3> drivePos { "Pre", "Inter", "Post" };
inline constexpr std::array<std::string_view, 3> quality { "2\u00d7", "4\u00d7", "8\u00d7" };
inline constexpr std::array<std::string_view, 12> laneRate {
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"
};
inline constexpr std::array<std::string_view, 7> laneShape {
    "Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Steps", "Custom"
};
inline constexpr std::array<std::string_view, 10> laneTarget {
    "Morph X", "Morph Y", "Morph Z", "Freq", "Res", "Transform", "Drive", "Spread", "Mix", "Fit"
};
inline constexpr std::array<std::string_view, 5> laneFollow { "Own", "Lane 1", "Lane 2", "Lane 3", "Lane 4" };
inline constexpr std::array<std::string_view, 7> laneRatio {
    "\u00f74", "\u00f73", "\u00f72", "\u00d71", "\u00d72", "\u00d73", "\u00d74"
};
}

inline constexpr std::array<int, 4> kStageCounts { 2, 4, 6, 7 };
inline constexpr std::array<int, 3> kQualityFactors { 2, 4, 8 };
inline constexpr std::array<double, 12> kLaneRateQuarters {
    32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 2.0 / 3, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125
};
inline constexpr std::array<double, 7> kLaneRatios { 0.25, 1.0 / 3, 0.5, 1.0, 2.0, 3.0, 4.0 };

enum class Topology : std::uint8_t { serial, hybrid, parallel };
enum class FoldType : std::uint8_t { tube, clip, fold, buchla, sine };
enum class DrivePos : std::uint8_t { pre, inter, post };
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

#define PF_LANE(n, onDef, target, rate, phase, depth)                                                              \
    detail::toggle("lane" #n "On", "Lane " #n, onDef),                                                             \
    detail::choice("lane" #n "Rate", "Lane " #n " rate", choices::laneRate, rate, false),                          \
    detail::ranged("lane" #n "Phase", "Lane " #n " phase", 0.0f, 360.0f, phase, Unit::degrees),                    \
    detail::bipolar("lane" #n "Depth", "Lane " #n " depth", depth),                                                \
    detail::choice("lane" #n "Shape", "Lane " #n " shape", choices::laneShape, 0, false),                          \
    detail::choice("lane" #n "Target", "Lane " #n " target", choices::laneTarget, static_cast<int>(target), false),\
    detail::choice("lane" #n "Follow", "Lane " #n " follow", choices::laneFollow, 0, false),                       \
    detail::choice("lane" #n "Ratio", "Lane " #n " ratio", choices::laneRatio, 3, false)

// Order must match ParamId.
inline constexpr std::array<ParamSpec, kParamCount> kParams { {
    detail::amount("morphX", "Morph X", 0.36f),
    detail::amount("morphY", "Morph Y", 0.28f),
    detail::amount("morphZ", "Morph Z", 0.18f),
    detail::ranged("freq", "Freq", -36.0f, 36.0f, 0.0f, Unit::semitones),
    detail::amount("res", "Resonance", 0.62f),
    detail::amount("drive", "Drive", 0.34f),
    detail::amount("motion", "Motion", 0.55f),
    detail::amount("mix", "Mix", 1.0f),

    detail::choice("cube", "Cube", choices::cube, 0),
    detail::bipolar("transform", "Transform", 0.0f),
    detail::choice("topology", "Topology", choices::topology, 0),
    detail::choice("stages", "Stages", choices::stages, 3),
    detail::amount("spread", "Spread", 0.22f),
    detail::toggle("pitchTrack", "Pitch track", false),
    detail::ranged("glide", "Glide", 0.0f, 300.0f, 18.0f, Unit::ms, 3.0f),

    detail::choice("foldType", "Shaper", choices::foldType, 0),
    detail::amount("asym", "Asym", 0.14f),
    detail::choice("drivePos", "Drive pos", choices::drivePos, 1),

    detail::toggle("subLock", "Sub lock", true),
    detail::ranged("subSplit", "Sub split", 40.0f, 250.0f, 110.0f, Unit::hz, 0.0f),
    detail::ranged("fit", "Fit", 0.0f, 360.0f, 0.0f, Unit::degrees),
    detail::ranged("inputGain", "Input", -24.0f, 24.0f, 0.0f, Unit::dB),
    detail::ranged("outputGain", "Output", -24.0f, 24.0f, 0.0f, Unit::dB),
    detail::toggle("limit", "Limit", true),
    detail::ranged("ceiling", "Ceiling", -12.0f, 0.0f, -0.3f, Unit::dB),
    detail::choice("quality", "Quality", choices::quality, 1, false),

    PF_LANE(1, true, LaneTarget::morphX, 4, 0.0f, 0.42f),
    PF_LANE(2, true, LaneTarget::morphY, 3, 90.0f, 0.34f),
    PF_LANE(3, false, LaneTarget::freq, 5, 180.0f, 0.22f),
    PF_LANE(4, false, LaneTarget::transform, 7, 270.0f, 0.30f),
} };

#undef PF_LANE

constexpr const ParamSpec& spec(ParamId id) { return kParams[static_cast<std::size_t>(id)]; }

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
    return automatable <= 64;
}
}
static_assert(detail::tableIsValid(), "parameter table is inconsistent");
static_assert(static_cast<int>(ParamId::mix) == 7, "the eight macros are the first eight parameters");
static_assert(spec(ParamId::quality).key == "quality", "kParams order must match ParamId");
static_assert(spec(ParamId::lane4Ratio).key == "lane4Ratio", "lane layout");
static_assert(choices::laneTarget.size() == kLaneTargetCount);
static_assert(kLaneRateQuarters.size() == choices::laneRate.size());
static_assert(kLaneRatios.size() == choices::laneRatio.size());
static_assert(kStageCounts.size() == choices::stages.size());

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

float toNormalised(const ParamSpec& spec, float real);
float fromNormalised(const ParamSpec& spec, float normalised);
float clampToRange(const ParamSpec& spec, float real);
std::string formatValue(const ParamSpec& spec, float real);

}
