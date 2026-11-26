// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/params.h"

#include <hba/core/param_math.h>

#include <cstdio>
#include <sstream>

namespace tg {

namespace {
constexpr std::array<std::string_view, 3> kModeChoices{"Spiral", "Copedent", "Mobius"};
constexpr std::array<std::string_view, 12> kKeyChoices{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
constexpr std::array<std::string_view, 9> kScaleChoices{"Phrygian",   "Aeolian",    "Harmonic Min",
                                                        "Dorian",     "Lydian #11", "Whole Tone",
                                                        "Diminished", "Hirajoshi",  "Chromatic"};
constexpr std::array<std::string_view, 3> kQualityChoices{"Live 2x", "Studio 4x", "Ultra 8x"};
constexpr std::array<std::string_view, 3> kSpiralDirChoices{"Rise", "Fall", "Pendulum"};
constexpr std::array<std::string_view, 6> kLoopBarChoices{"1/2 Bar", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars"};
constexpr std::array<std::string_view, 6> kLane2BarChoices{"1/4 Bar", "1/2 Bar", "1 Bar", "2 Bars", "4 Bars", "8 Bars"};
constexpr std::array<std::string_view, 5> kSpiralRatioChoices{"Octave (2:1)", "Fifth (3:2)", "Tritone", "Minor 3rd",
                                                              "Bohlen-Pierce"};
constexpr std::array<std::string_view, 6> kSpiralStackChoices{"Single Bass",  "Power Fifth",  "Phrygian b9",
                                                              "Tritone Dyad", "Quartal Dark", "Minor 9th"};
constexpr std::array<std::string_view, 8> kVoicingChoices{"Phrygian Tension", "Unison Bloom",     "Neo-Riemann PLR",
                                                          "Neuro Cluster",    "Slide Steel Dark", "Quartal Drift",
                                                          "Mobius Dim7",      "Mobius Whole-Tone"};
constexpr std::array<std::string_view, 3> kGravityChoices{"Scale Wells", "Just Intonation", "Hybrid"};
constexpr std::array<std::string_view, 4> kStaggerChoices{"Inner First", "Low to High", "High to Low", "Outside In"};
constexpr std::array<std::string_view, 7> kTimbreChoices{"Reese Saw", "Pure Sine",  "Formant Choir", "Wavefold FM",
                                                         "Pulse Sub", "Glass Bell", "Super Organ"};
constexpr std::array<std::string_view, 5> kDriveModelChoices{"Tube", "Buchla Fold", "Tri Fold", "Sine Fold", "Clip"};
constexpr std::array<std::string_view, 4> kFilterModeChoices{"LP 12", "LP 24", "Bandpass", "Neuro Notch"};
constexpr std::array<std::string_view, 7> kLaneTargetChoices{"Morph",   "Bell Center", "Filter", "Drive",
                                                             "Gravity", "Risset Gate", "Space"};
constexpr std::array<std::string_view, 3> kMpeRangeChoices{"12 st", "24 st", "48 st"};
constexpr std::array<std::string_view, 6> kPadProgramChoices{"Spiral", "Mobius", "Copedent", "Abyss", "Risset", "Void"};

ParamSpec cont(ParamId id, std::string_view key, std::string_view name, float low, float high, float def,
               float skew = 1.0f, std::string_view unit = {}) {
    return {id, key, name, ParamKind::continuous, low, high, def, skew, unit, {}, true};
}

ParamSpec choice(ParamId id, std::string_view key, std::string_view name, std::span<const std::string_view> choices,
                 int def, bool automatable = true) {
    return {
        id,   key, name,    ParamKind::choice, 0.0f, static_cast<float>(choices.size() - 1), static_cast<float>(def),
        1.0f, {},  choices, automatable};
}

ParamSpec toggle(ParamId id, std::string_view key, std::string_view name, bool def) {
    return {id, key, name, ParamKind::toggle, 0.0f, 1.0f, def ? 1.0f : 0.0f, 1.0f, {}, {}, true};
}
} // namespace

const std::array<ParamSpec, kParamCount>& paramSpecs() {
    static const std::array<ParamSpec, kParamCount> kSpecs = {
        choice(ParamId::mode, "mode", "Mode", kModeChoices, 0),
        choice(ParamId::rootKey, "rootKey", "Root Key", kKeyChoices, 5), // F
        choice(ParamId::scale, "scale", "Scale", kScaleChoices, 0),      // Phrygian
        choice(ParamId::quality, "quality", "Quality", kQualityChoices, 1, false),

        choice(ParamId::spiralDir, "spiralDir", "Direction", kSpiralDirChoices, 0),
        choice(ParamId::loopBars, "loopBars", "Loop Length", kLoopBarChoices, 3), // 4 Bars
        choice(ParamId::spiralRatio, "spiralRatio", "Spiral Ratio", kSpiralRatioChoices, 0),
        choice(ParamId::spiralStack, "spiralStack", "Spiral Stack", kSpiralStackChoices, 0),
        cont(ParamId::bellCenter, "bellCenter", "Bell Center", 80.0f, 2400.0f, 320.0f, 0.35f, "Hz"),
        cont(ParamId::bellWidth, "bellWidth", "Bell Width", 35.0f, 100.0f, 80.0f, 1.0f, "%"),
        cont(ParamId::phaseOffset, "phaseOffset", "Phase Offset", 0.0f, 100.0f, 0.0f, 1.0f, "%"),

        choice(ParamId::voicingPack, "voicingPack", "Voicing Pack", kVoicingChoices, 0),
        cont(ParamId::morph, "morph", "Morph", 0.0f, 100.0f, 0.0f, 1.0f, "%"),
        toggle(ParamId::morphAuto, "morphAuto", "Auto-Morph", true),
        cont(ParamId::gravity, "gravity", "Scale Gravity", 0.0f, 100.0f, 55.0f, 1.0f, "%"),
        choice(ParamId::gravityMode, "gravityMode", "Gravity Mode", kGravityChoices, 0),
        cont(ParamId::stagger, "stagger", "Pedal Stagger", 0.0f, 100.0f, 38.0f, 1.0f, "%"),
        choice(ParamId::staggerOrder, "staggerOrder", "Stagger Order", kStaggerChoices, 0),
        toggle(ParamId::anchorSub, "anchorSub", "Anchor V1 Sub", false),

        choice(ParamId::timbre, "timbre", "Timbre", kTimbreChoices, 0),
        cont(ParamId::detune, "detune", "Detune", 0.0f, 45.0f, 14.0f, 1.0f, "ct"),
        cont(ParamId::width, "width", "Stereo Width", 0.0f, 100.0f, 72.0f, 1.0f, "%"),
        cont(ParamId::space, "space", "Stereo Space", 0.0f, 100.0f, 28.0f, 1.0f, "%"),
        cont(ParamId::subAnchor, "subAnchor", "Sub-Anchor", 0.0f, 100.0f, 78.0f, 1.0f, "%"),
        cont(ParamId::subWarmth, "subWarmth", "Sub Warmth", 0.0f, 100.0f, 35.0f, 1.0f, "%"),
        cont(ParamId::subCutoff, "subCutoff", "Sub X-Over", 45.0f, 120.0f, 84.0f, 1.0f, "Hz"),
        cont(ParamId::drive, "drive", "ADAA Drive", 0.0f, 100.0f, 42.0f, 1.0f, "%"),
        choice(ParamId::driveModel, "driveModel", "Shaper", kDriveModelChoices, 0),
        choice(ParamId::filterMode, "filterMode", "Filter Mode", kFilterModeChoices, 0),
        cont(ParamId::filterHz, "filterHz", "Filter", 120.0f, 16000.0f, 2800.0f, 0.30f, "Hz"),
        cont(ParamId::filterRes, "filterRes", "Resonance", 0.0f, 92.0f, 22.0f, 1.0f, "%"),
        cont(ParamId::rissetGate, "rissetGate", "Risset Gate", 0.0f, 100.0f, 0.0f, 1.0f, "%"),
        cont(ParamId::glideMs, "glideMs", "Key Glide", 0.0f, 400.0f, 24.0f, 0.50f, "ms"),

        cont(ParamId::attackMs, "attackMs", "Attack", 1.0f, 1500.0f, 12.0f, 0.35f, "ms"),
        cont(ParamId::releaseMs, "releaseMs", "Release", 15.0f, 3000.0f, 180.0f, 0.35f, "ms"),
        choice(ParamId::lane1Rate, "lane1Rate", "Lane 1 Rate", kLoopBarChoices, 3),
        cont(ParamId::lane1Depth, "lane1Depth", "Lane 1 Depth", -100.0f, 100.0f, 100.0f, 1.0f, "%"),
        choice(ParamId::lane1Target, "lane1Target", "Lane 1 Target", kLaneTargetChoices, 0),
        choice(ParamId::lane2Rate, "lane2Rate", "Lane 2 Rate", kLane2BarChoices, 3),
        cont(ParamId::lane2Depth, "lane2Depth", "Lane 2 Depth", -100.0f, 100.0f, 30.0f, 1.0f, "%"),
        choice(ParamId::lane2Target, "lane2Target", "Lane 2 Target", kLaneTargetChoices, 2),
        toggle(ParamId::mpeOut, "mpeOut", "MPE Out", true),
        choice(ParamId::mpeRange, "mpeRange", "MPE Range", kMpeRangeChoices, 2), // 48 st
        toggle(ParamId::keySwitch, "keySwitch", "KeySwitch C0", true),
        toggle(ParamId::retrig, "retrig", "Note Retrig", false),
        toggle(ParamId::limiter, "limiter", "True-Peak Lim", true),
        cont(ParamId::outputDb, "outputDb", "Output", -24.0f, 6.0f, -3.0f, 1.0f, "dB"),
        toggle(ParamId::bypass, "bypass", "Bypass", false),

        choice(ParamId::padProgram, "padProgram", "Manifold Pad", kPadProgramChoices, 0),
        cont(ParamId::padX, "padX", "Pad X", 0.0f, 100.0f, 50.0f, 1.0f, "%"),
        cont(ParamId::padY, "padY", "Pad Y", 0.0f, 100.0f, 50.0f, 1.0f, "%"),
        toggle(ParamId::padTouch, "padTouch", "Pad Touch", false),
        toggle(ParamId::padHold, "padHold", "Drone Hold", false),
    };
    return kSpecs;
}

PadCaptions padCaptions(PadProgram program) noexcept HBA_NONBLOCKING {
    switch (program) {
    case PadProgram::spiral:
        return {"Spiral", "Velocity", "Bell & Air"};
    case PadProgram::mobius:
        return {"Mobius", "Torus Flow", "Gravity & Halo"};
    case PadProgram::copedent:
        return {"Copedent", "Chord Morph", "Stagger & Air"};
    case PadProgram::abyss:
        return {"Abyss", "Sub Dive", "Drive & Bite"};
    case PadProgram::risset:
        return {"Risset", "Pulse Gate", "Filter Focus"};
    case PadProgram::voidSpace:
        return {"Void", "Manifold Halo", "Detune & Width"};
    }
    return {"Spiral", "Velocity", "Bell & Air"};
}

const ParamSpec& specOf(ParamId id) { return paramSpecs()[static_cast<std::size_t>(id)]; }

const ParamSpec* findParam(std::string_view key) {
    for (const auto& s : paramSpecs()) {
        if (s.key == key)
            return &s;
    }
    return nullptr;
}

float clampValue(const ParamSpec& spec, float real) noexcept HBA_NONBLOCKING {
    return hba::core::clampParam(real, spec.low, spec.high, spec.def, spec.kind == ParamKind::continuous);
}

float toNormalised(const ParamSpec& spec, float real) noexcept HBA_NONBLOCKING {
    return hba::core::toNormalisedJuceSkew(real, spec.low, spec.high, spec.def, spec.skew,
                                           spec.kind == ParamKind::continuous);
}

float fromNormalised(const ParamSpec& spec, float norm) noexcept HBA_NONBLOCKING {
    return hba::core::fromNormalisedJuceSkew(norm, spec.low, spec.high, spec.def, spec.skew,
                                             spec.kind == ParamKind::continuous);
}

std::string formatValue(const ParamSpec& spec, float real) {
    const float v = clampValue(spec, real);
    if (spec.kind == ParamKind::toggle)
        return v >= 0.5f ? "On" : "Off";
    if (spec.kind == ParamKind::choice) {
        const auto idx =
            static_cast<std::size_t>(std::clamp(static_cast<int>(v), 0, static_cast<int>(spec.choices.size()) - 1));
        return std::string(spec.choices[idx]);
    }
    char buf[48]{};
    if (spec.unit == "Hz") {
        if (v >= 1000.0f)
            std::snprintf(buf, sizeof(buf), "%.2f kHz", static_cast<double>(v) / 1000.0);
        else
            std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(v));
        return buf;
    }
    if (spec.unit == "dB") {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(v));
        return buf;
    }
    if (spec.unit == "ms") {
        if (v >= 100.0f)
            std::snprintf(buf, sizeof(buf), "%.0f ms", static_cast<double>(v));
        else
            std::snprintf(buf, sizeof(buf), "%.1f ms", static_cast<double>(v));
        return buf;
    }
    if (spec.unit == "ct") {
        std::snprintf(buf, sizeof(buf), "%.1f ct", static_cast<double>(v));
        return buf;
    }
    if (spec.unit == "%") {
        std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(v));
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    return buf;
}

Params Params::defaults() {
    Params p;
    for (const auto& s : paramSpecs())
        p.values[static_cast<std::size_t>(s.id)] = s.def;
    return p;
}

hba::dsp::Curve presetRampUpCurve() noexcept HBA_NONBLOCKING {
    return hba::dsp::curves::make({
        {0.0f, -1.0f, 0.0f, hba::dsp::Segment::line},
        {0.995f, 1.0f, 0.0f, hba::dsp::Segment::hold},
    });
}

hba::dsp::Curve presetRampDownCurve() noexcept HBA_NONBLOCKING {
    return hba::dsp::curves::make({
        {0.0f, 1.0f, 0.0f, hba::dsp::Segment::line},
        {0.995f, -1.0f, 0.0f, hba::dsp::Segment::hold},
    });
}

hba::dsp::Curve presetTriangleCurve() noexcept HBA_NONBLOCKING {
    return hba::dsp::curves::make({
        {0.0f, -1.0f, 0.0f, hba::dsp::Segment::smooth},
        {0.5f, 1.0f, 0.0f, hba::dsp::Segment::smooth},
    });
}

hba::dsp::Curve presetSBloomCurve() noexcept HBA_NONBLOCKING {
    return hba::dsp::curves::make({
        {0.0f, -1.0f, -0.35f, hba::dsp::Segment::smooth},
        {0.33f, -0.30f, 0.0f, hba::dsp::Segment::smooth},
        {0.67f, 0.35f, 0.0f, hba::dsp::Segment::smooth},
        {0.96f, 1.0f, 0.0f, hba::dsp::Segment::smooth},
    });
}

hba::dsp::Curve presetStep4Curve() noexcept HBA_NONBLOCKING {
    return hba::dsp::curves::make({
        {0.0f, -1.0f, 0.0f, hba::dsp::Segment::smooth},
        {0.20f, -1.0f, 0.0f, hba::dsp::Segment::smooth},
        {0.25f, -0.333f, 0.0f, hba::dsp::Segment::smooth},
        {0.45f, -0.333f, 0.0f, hba::dsp::Segment::smooth},
        {0.50f, 0.333f, 0.0f, hba::dsp::Segment::smooth},
        {0.70f, 0.333f, 0.0f, hba::dsp::Segment::smooth},
        {0.75f, 1.0f, 0.0f, hba::dsp::Segment::smooth},
        {0.95f, 1.0f, 0.0f, hba::dsp::Segment::smooth},
    });
}

LaneCurves defaultLaneCurves() { return {presetRampUpCurve(), presetTriangleCurve()}; }

} // namespace tg
