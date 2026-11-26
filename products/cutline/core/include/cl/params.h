// SPDX-License-Identifier: AGPL-3.0-or-later
// Single source of truth for Cutline 2.0's parameter contract (48 total params,
// 8 high-leverage musical macros first for Ableton Live / Push bank 1, plus
// full automatable X/Y playable articulation surface).
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
    // Bank 1: Eight high-leverage musical macros (Push / Live bank 1)
    style,
    gravity, // Display: "Groove" (keeps key "gravity" for recall & automation stability)
    ghosts,
    chop,
    ratchet,
    complexity,
    fills,
    dynamics,

    // Mode, kit routing, slicer, pocket, scenes, and timing
    engineMode,
    noteMap,
    slicerStyle,
    phraseBars,
    seqRate, // Global sequence rate: 0.5x (Half-Time), 1x (Normal), 2x (Double-Time)
    attractor,
    fillType,
    variation,
    scene,
    sceneQuantize,
    padMidi,
    slicerLatch,
    strumMs,
    gateLen,
    shiftMs,
    audition,
    auditionGain,
    motion,

    // Playable 2D X/Y Performance Surface (all automatable in Live 12 / Push!)
    padProgram, // 0..7: Slice, Roll, Bounce, Swing, Poly, Stutter, Morph, Drop
    padX,       // 0..1 continuous horizontal expression
    padY,       // 0..1 continuous vertical expression
    padTouch,   // Live engagement (mouse drag / automation / MIDI C3-G3)
    padHold,    // Latch engagement hands-free
    padCcMode,  // Expressive XY -> CC bridge (Off, CC74+CC1, CC74+Vel, CC1+AT)

    // Two optional tempo-locked CC / Velocity expression lanes (zero Pitch Bend by default!)
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

    ccThinMs,
    count
};

inline constexpr int kParamCount = static_cast<int>(ParamId::count);
static_assert(kParamCount <= 64, "Cutline stays within Ableton Live's 64-parameter direct configure limit");

inline constexpr int kLaneCount = 2;
inline constexpr int kPerfPadCount = 8;
inline constexpr int kSceneCount = 4;

enum class LaneField : std::uint8_t { on, rate, phase, depth, shape, target, follow, ratio };
inline constexpr int kLaneFieldCount = 8;

enum class LaneTarget : std::uint8_t {
    cc74Slide = 0,
    cc1Mod = 1,
    cc11Expr = 2,
    pressure = 3,
    cc71Res = 4,
    velocity = 5,
    pitchScoop = 6,
};
inline constexpr int kLaneTargetCount = 7;

constexpr ParamId laneParam(int lane, LaneField field) noexcept {
    return static_cast<ParamId>(static_cast<int>(ParamId::lane1On) + lane * kLaneFieldCount + static_cast<int>(field));
}

enum class DrumStyle : std::uint8_t {
    amen = 0,
    think = 1,
    neuro = 2,
    halftime = 3,
    ukg = 4,
    footwork = 5,
    stepper = 6,
    electro = 7,
    drill = 8,
    afro = 9,
    trap = 10,
    euclidean = 11,
};
inline constexpr int kDrumStyleCount = 12;

enum class EngineMode : std::uint8_t { drum = 0, slicer = 1 };
enum class NoteMap : std::uint8_t {
    liveRack = 0,
    strikeBody = 1,
    chromatic = 2,
    addictiveDrums = 3,
    toontrack = 4,
    bfd3 = 5,
    ggd = 6,
    ssd5 = 7,
    kontakt = 8,
    xlnXo = 9,
};
inline constexpr int kNoteMapCount = 10;
enum class SlicerStyle : std::uint8_t { stabChop = 0, strumFlam = 1, breakArp = 2, bassStab = 3, clipSlice = 4 };
enum class SeqRate : std::uint8_t { half = 0, normal = 1, doubleTime = 2 };
enum class Attractor : std::uint8_t { mpcSwing = 0, backbeat = 1, downbeat = 2, offbeat16 = 3, triplet = 4 };
enum class FillType : std::uint8_t { autoFill = 0, snareRoll = 1, tomRun = 2, drillChop = 3, halfDrop = 4, airCut = 5 };
enum class Quantize : std::uint8_t { instant = 0, sixteenth = 1, quarter = 2, oneBar = 3 };
enum class PadProgram : std::uint8_t {
    slice = 0,
    roll = 1,
    bounce = 2,
    swing = 3,
    poly = 4,
    stutter = 5,
    morph = 6,
    drop = 7,
};
enum class PadCcMode : std::uint8_t { off = 0, cc74AndCc1 = 1, cc74AndVel = 2, cc1AndAt = 3 };
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
inline constexpr std::array<std::string_view, 12> style{"Amen",    "Think",   "Neuro", "Halftime", "UKG",  "Footwork",
                                                        "Stepper", "Electro", "Drill", "Afro",     "Trap", "Euclidean"};
inline constexpr std::array<std::string_view, 2> engineMode{"Drum Engine", "MIDI Slicer"};
inline constexpr std::array<std::string_view, kNoteMapCount> noteMap{
    "Live Rack / GM", "Strike Body",   "Chromatic",  "Addictive Drums 2", "Toontrack (SD3/EZD)",
    "BFD3",           "GetGood Drums", "Slate SSD5", "NI Studio / Abbey", "XLN XO"};
inline constexpr std::array<std::string_view, 5> slicerStyle{"Stab Chop", "Strum / Flam", "Break Arp", "Bass / Stab",
                                                             "Clip Slice"};
inline constexpr std::array<std::string_view, 3> phraseBars{"2 bars", "4 bars", "8 bars"};
inline constexpr std::array<std::string_view, 3> seqRate{"0.5\u00d7", "1\u00d7", "2\u00d7"};
inline constexpr std::array<std::string_view, 5> attractor{"MPC Swing", "Backbeat", "Downbeat", "Offbeat 16",
                                                           "Triplet"};
inline constexpr std::array<std::string_view, 6> fillType{"Auto",       "Snare Roll", "Tom Run",
                                                          "Drill Chop", "Half Drop",  "Air Cut"};
inline constexpr std::array<std::string_view, 4> scene{"A", "B", "C", "D"};
inline constexpr std::array<std::string_view, 4> sceneQuantize{"Instant", "1/16", "1/4", "1 Bar"};
inline constexpr std::array<std::string_view, 8> padProgram{"Slice", "Roll",    "Bounce", "Swing",
                                                            "Poly",  "Stutter", "Morph",  "Drop"};
inline constexpr std::array<std::string_view, 4> padCcMode{"Off", "CC74 + CC1", "CC74 + Vel", "CC1 + AT"};
inline constexpr std::array<std::string_view, 4> ccThinMs{"1 ms", "2 ms", "4 ms", "8 ms"};
inline constexpr std::array<std::string_view, 12> laneRate{"8 bars", "4 bars", "2 bars", "1 bar", "1/2",   "1/4",
                                                           "1/4T",   "1/8",    "1/8T",   "1/16",  "1/16T", "1/32"};
inline constexpr std::array<std::string_view, 7> laneShape{"Sine",   "Triangle", "Ramp up", "Ramp down",
                                                           "Square", "Steps",    "Custom"};
inline constexpr std::array<std::string_view, 7> laneTarget{"CC74 Slide", "CC1 Mod",  "CC11 Expr",  "Pressure",
                                                            "CC71 Res",   "Velocity", "Pitch Scoop"};
inline constexpr std::array<std::string_view, 3> laneFollow{"Own", "Lane 1", "Lane 2"};
inline constexpr std::array<std::string_view, 7> laneRatio{"\u00f74", "\u00f73", "\u00f72", "\u00d71",
                                                           "\u00d72", "\u00d73", "\u00d74"};
} // namespace choices

inline constexpr std::array<int, 3> kPhraseBarCounts{2, 4, 8};
inline constexpr std::array<double, 3> kSeqRateMultipliers{0.5, 1.0, 2.0};
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
    // Bank 1: 8 High-Leverage Musical Macros
    detail::choice("style", "Style", choices::style, 0, true),
    detail::bipolar("gravity", "Gravity", 0.24f),
    detail::amount("ghosts", "Ghosts", 0.58f),
    detail::amount("chop", "Chop", 0.22f),
    detail::amount("ratchet", "Ratchet", 0.30f),
    detail::amount("complexity", "Complex", 0.58f),
    detail::amount("fills", "Fills", 0.48f),
    detail::amount("dynamics", "Dynamics", 0.66f),

    // Mode, routing, timing, scenes & preview
    detail::choice("engineMode", "Mode", choices::engineMode, 0, false),
    detail::choice("noteMap", "Kit map", choices::noteMap, 0, false),
    detail::choice("slicerStyle", "Slicer", choices::slicerStyle, 0, true),
    detail::choice("phraseBars", "Bars", choices::phraseBars, 1, false),
    detail::choice("seqRate", "Rate", choices::seqRate, 1, true),
    detail::choice("attractor", "Pocket", choices::attractor, 0, true),
    detail::choice("fillType", "Fill", choices::fillType, 0, true),
    detail::ranged("variation", "Var", 1.0f, 64.0f, 1.0f, Unit::integer),
    detail::choice("scene", "Scene", choices::scene, 0, true),
    detail::choice("sceneQuantize", "Quant", choices::sceneQuantize, 1, false), // 1/16 snappy live response!
    detail::toggle("padMidi", "Pad MIDI", true, false),
    detail::toggle("slicerLatch", "Hold MIDI", false, false),
    detail::ranged("strumMs", "Strum", 0.0f, 45.0f, 14.0f, Unit::ms),
    detail::amount("gateLen", "Gate", 0.60f),
    detail::ranged("shiftMs", "Shift", -25.0f, 25.0f, 0.0f, Unit::ms),
    detail::toggle("audition", "Audition", true, false),
    detail::ranged("auditionGain", "Aud gain", -36.0f, 0.0f, -6.0f, Unit::dB),
    detail::amount("motion", "Expr amt", 0.45f),

    // Playable X/Y Performance Surface (all automatable in Live 12 / Push!)
    detail::choice("padProgram", "Pad mode", choices::padProgram, 4, true), // Lands on iconic POLY!
    detail::amount("padX", "Pad X", 0.62f),
    detail::amount("padY", "Pad Y", 0.64f),
    detail::toggle("padTouch", "Pad touch", false, true),
    detail::toggle("padHold", "Hold", false, true),
    detail::choice("padCcMode", "XY \u2192 CC", choices::padCcMode, 1, false),

    // 2 Optional Expression Lanes (CC74 & Velocity — ZERO Pitch Bend wobble by default)
    CL_LANE(1, true, LaneTarget::cc74Slide, 2, 0.0f, 0.48f),
    CL_LANE(2, false, LaneTarget::velocity, 3, 0.0f, 0.36f),

    detail::choice("ccThinMs", "CC rate", choices::ccThinMs, 1, false),
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
