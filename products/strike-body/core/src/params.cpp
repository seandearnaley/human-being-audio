// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sb/params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace sb {

namespace {
constexpr std::array<std::string_view, 4> kBodyWaveChoices{"Pure Sine", "Warm 909", "Tri-Punch", "Sub-Square"};
constexpr std::array<std::string_view, 6> kShellModelChoices{"Snare Shell",  "Kirchhoff Plate", "Drumhead",
                                                             "Metallic Bar", "Hollow Pipe",     "Glass Bell"};
constexpr std::array<std::string_view, 6> kMalletTypeChoices{"Wood Stick", "Hard Felt",  "Metal Hammer",
                                                             "Rim Flam",   "Skin Thump", "Laser Snap"};
constexpr std::array<std::string_view, 3> kWireColorChoices{"Snare Wire", "Crushed Metal", "Air Sizzle"};
constexpr std::array<std::string_view, 5> kDriveModelChoices{"Clip", "Tube", "Buchla", "Fold", "Sine"};
constexpr std::array<std::string_view, 3> kChokeModeChoices{"Split Sub", "Poly 4x", "Mono Cut"};
constexpr std::array<std::string_view, 2> kKeyTrackChoices{"Kit Map", "Chromatic"};
constexpr std::array<std::string_view, 3> kQualityChoices{"Live 2x", "Studio 4x", "Ultra 8x"};
constexpr std::array<std::string_view, 4> kLayerSoloChoices{"Off", "Body", "Shell", "Wire"};
constexpr std::array<std::string_view, 2> kToggleChoices{"Off", "On"};

constexpr ParamSpec cont(ParamId id, std::string_view key, std::string_view name, float low, float high, float def,
                         std::string_view unit = {}, float skew = 1.0f, bool automatable = true) {
    return {id, key, name, ParamKind::continuous, low, high, def, skew, unit, {}, automatable};
}

constexpr ParamSpec pick(ParamId id, std::string_view key, std::string_view name,
                         std::span<const std::string_view> choices, int def, bool automatable = true) {
    return {
        id,   key, name,    ParamKind::choice, 0.0f, static_cast<float>(choices.size() - 1), static_cast<float>(def),
        1.0f, {},  choices, automatable};
}

constexpr ParamSpec flagSpec(ParamId id, std::string_view key, std::string_view name, bool def,
                             bool automatable = true) {
    return {id, key, name, ParamKind::toggle, 0.0f, 1.0f, def ? 1.0f : 0.0f, 1.0f, {}, kToggleChoices, automatable};
}

constexpr std::array<ParamSpec, kParamCount> kSpecs{{
    // 1. Fundamental Body
    cont(ParamId::bodyHz, "bodyHz", "Body Hz", 32.0f, 360.0f, 185.0f, "Hz", 0.45f),
    pick(ParamId::bodyWave, "bodyWave", "Body Wave", kBodyWaveChoices, 1),
    cont(ParamId::bodyPhase, "bodyPhase", "Start Phase", 0.0f, 360.0f, 90.0f, "deg"),
    cont(ParamId::pitchDrop, "pitchDrop", "Pitch Drop", 0.0f, 36.0f, 19.0f, "st", 0.75f),
    cont(ParamId::pitchCurve, "pitchCurve", "Drop Curve", 0.0f, 100.0f, 55.0f, "%"),
    cont(ParamId::pitchTime, "pitchTime", "Drop Time", 2.0f, 120.0f, 12.0f, "ms", 0.45f),
    cont(ParamId::bodyDecay, "bodyDecay", "Body Decay", 25.0f, 1200.0f, 155.0f, "ms", 0.42f),
    cont(ParamId::bodyFm, "bodyFm", "Body FM", 0.0f, 100.0f, 18.0f, "%"),
    cont(ParamId::bodyFmRatio, "bodyFmRatio", "FM Ratio", 1.0f, 7.0f, 2.35f, "x", 0.65f),
    cont(ParamId::bodyLevel, "bodyLevel", "Body Level", 0.0f, 100.0f, 90.0f, "%"),

    // 2. Nonlinear Tensioned Modal Shell
    pick(ParamId::shellModel, "shellModel", "Shell Model", kShellModelChoices, 0),
    pick(ParamId::malletType, "malletType", "Mallet", kMalletTypeChoices, 0),
    cont(ParamId::shellTune, "shellTune", "Shell Tune", -24.0f, 24.0f, 4.0f, "st"),
    cont(ParamId::shellTension, "shellTension", "Tension", 0.0f, 100.0f, 48.0f, "%"),
    cont(ParamId::strikePos, "strikePos", "Strike Pos", 0.0f, 100.0f, 34.0f, "%"),
    cont(ParamId::shellStretch, "shellStretch", "Stretch", -50.0f, 100.0f, 0.0f, "%"),
    cont(ParamId::shellDecay, "shellDecay", "Shell Decay", 30.0f, 1500.0f, 175.0f, "ms", 0.40f),
    cont(ParamId::shellDamp, "shellDamp", "Hi Damp", 0.0f, 100.0f, 44.0f, "%"),
    cont(ParamId::shellSpread, "shellSpread", "Spread", 0.0f, 100.0f, 38.0f, "%"),
    cont(ParamId::shellOffsetMs, "shellOffsetMs", "Shell Align", -5.0f, 5.0f, 0.0f, "ms"),
    flagSpec(ParamId::shellInvert, "shellInvert", "Shell Pol", false),
    cont(ParamId::shellLevel, "shellLevel", "Shell Level", 0.0f, 100.0f, 74.0f, "%"),

    // 3. Transient-Ducked Wire & Click Layer
    pick(ParamId::wireColor, "wireColor", "Wire Color", kWireColorChoices, 0),
    cont(ParamId::wireBuzz, "wireBuzz", "Wire Buzz", 0.0f, 100.0f, 36.0f, "%"),
    cont(ParamId::clickAmt, "clickAmt", "Crack Click", 0.0f, 100.0f, 68.0f, "%"),
    cont(ParamId::clickTone, "clickTone", "Click Hz", 800.0f, 12000.0f, 4800.0f, "Hz", 0.42f),
    cont(ParamId::wireDuck, "wireDuck", "Wire Duck", 0.0f, 100.0f, 75.0f, "%"),
    cont(ParamId::wireDuckMs, "wireDuckMs", "Duck Window", 3.0f, 35.0f, 10.5f, "ms", 0.60f),
    cont(ParamId::wireTone, "wireTone", "Wire Tone", 600.0f, 14000.0f, 6000.0f, "Hz", 0.42f),
    cont(ParamId::wireDecay, "wireDecay", "Wire Decay", 20.0f, 900.0f, 165.0f, "ms", 0.42f),
    cont(ParamId::wireWidth, "wireWidth", "Wire Width", 0.0f, 100.0f, 48.0f, "%"),
    cont(ParamId::wireOffsetMs, "wireOffsetMs", "Wire Align", -5.0f, 5.0f, 0.0f, "ms"),
    flagSpec(ParamId::wireInvert, "wireInvert", "Wire Pol", false),
    cont(ParamId::wireLevel, "wireLevel", "Wire Level", 0.0f, 100.0f, 72.0f, "%"),
    cont(ParamId::humanize, "humanize", "Humanize", 0.0f, 100.0f, 0.0f, "%"),

    // 4. Punch, Disperser, Tone Sculptor, Room, ADAA2 & Output
    cont(ParamId::disperserAmt, "disperserAmt", "Disperser", 0.0f, 100.0f, 28.0f, "%"),
    cont(ParamId::disperserHz, "disperserHz", "Disp Hz", 70.0f, 2400.0f, 380.0f, "Hz", 0.42f),
    cont(ParamId::drive, "drive", "ADAA Drive", 0.0f, 100.0f, 42.0f, "%"),
    pick(ParamId::driveModel, "driveModel", "Shaper", kDriveModelChoices, 0),
    cont(ParamId::scoopAmt, "scoopAmt", "Tone Scoop", -100.0f, 100.0f, 20.0f, "%"),
    cont(ParamId::roomAmt, "roomAmt", "Studio Room", 0.0f, 100.0f, 14.0f, "%"),
    pick(ParamId::chokeMode, "chokeMode", "Voice Choke", kChokeModeChoices, 0),
    pick(ParamId::keyTrack, "keyTrack", "MIDI Mode", kKeyTrackChoices, 0),
    flagSpec(ParamId::subMono, "subMono", "Sub Mono", true),
    flagSpec(ParamId::limiter, "limiter", "Limiter", true),
    pick(ParamId::quality, "quality", "Quality", kQualityChoices, 1, false),
    cont(ParamId::outputDb, "outputDb", "Output", -18.0f, 6.0f, -1.5f, "dB"),
    flagSpec(ParamId::bypass, "bypass", "Bypass", false),

    // 5. Performance Macros
    cont(ParamId::macroPunch, "macroPunch", "Punch", 0.0f, 100.0f, 0.0f, "%"),
    cont(ParamId::macroTight, "macroTight", "Tight", -100.0f, 100.0f, 0.0f, "%"),
    cont(ParamId::macroMetal, "macroMetal", "Metal", 0.0f, 100.0f, 0.0f, "%"),
    cont(ParamId::macroSmack, "macroSmack", "Smack", 0.0f, 100.0f, 0.0f, "%"),

    // 6. Layer Audition Mute / Solo
    flagSpec(ParamId::bodyMute, "bodyMute", "Body Mute", false, false),
    flagSpec(ParamId::shellMute, "shellMute", "Shell Mute", false, false),
    flagSpec(ParamId::wireMute, "wireMute", "Wire Mute", false, false),
    pick(ParamId::layerSolo, "layerSolo", "Layer Solo", kLayerSoloChoices, 0, false),
}};
} // namespace

const std::array<ParamSpec, kParamCount>& paramSpecs() { return kSpecs; }

const ParamSpec& specOf(ParamId id) { return kSpecs[static_cast<std::size_t>(id)]; }

const ParamSpec* findParam(std::string_view key) {
    for (const auto& s : kSpecs) {
        if (s.key == key)
            return &s;
    }
    return nullptr;
}

float clampValue(const ParamSpec& spec, float real) noexcept HBA_NONBLOCKING {
    if (!std::isfinite(real))
        return spec.def;
    if (spec.kind == ParamKind::toggle)
        return real >= 0.5f ? 1.0f : 0.0f;
    if (spec.kind == ParamKind::choice)
        return std::clamp(std::round(real), spec.low, spec.high);
    return std::clamp(real, spec.low, spec.high);
}

float toNormalised(const ParamSpec& spec, float real) noexcept HBA_NONBLOCKING {
    const float c = clampValue(spec, real);
    const float span = spec.high - spec.low;
    if (span <= 0.0f)
        return 0.0f;
    const float lin = std::clamp((c - spec.low) / span, 0.0f, 1.0f);
    if (spec.kind == ParamKind::continuous && spec.skew != 1.0f)
        return std::pow(lin, spec.skew);
    return lin;
}

float fromNormalised(const ParamSpec& spec, float norm) noexcept HBA_NONBLOCKING {
    const float n = std::clamp(std::isfinite(norm) ? norm : 0.0f, 0.0f, 1.0f);
    const float lin = (spec.kind == ParamKind::continuous && spec.skew != 1.0f) ? std::pow(n, 1.0f / spec.skew) : n;
    return clampValue(spec, spec.low + lin * (spec.high - spec.low));
}

std::string hzToNoteTag(float hz) {
    static constexpr std::array<const char*, 12> kNotes{"C",  "C#", "D",  "D#", "E",  "F",
                                                        "F#", "G",  "G#", "A",  "A#", "B"};
    const double safeHz = std::max(16.0, static_cast<double>(hz));
    const int midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(safeHz / 440.0)));
    const int pc = ((midi % 12) + 12) % 12;
    const int oct = (midi / 12) - 1;
    return std::string(kNotes[static_cast<std::size_t>(pc)]) + std::to_string(oct);
}

std::string formatValue(const ParamSpec& spec, float real) {
    const float c = clampValue(spec, real);
    if (spec.kind == ParamKind::choice || spec.kind == ParamKind::toggle) {
        const auto idx = static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::lround(c)), 0, static_cast<int>(spec.choices.size()) - 1));
        return std::string(spec.choices[idx]);
    }
    char buf[48]{};
    if (spec.id == ParamId::bodyHz) {
        const std::string tag = hzToNoteTag(c);
        std::snprintf(buf, sizeof(buf), "%.0f Hz %s", static_cast<double>(c), tag.c_str());
    } else if (spec.unit == "Hz") {
        if (c >= 1000.0f)
            std::snprintf(buf, sizeof(buf), "%.2f kHz", static_cast<double>(c) * 0.001);
        else
            std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(c));
    } else if (spec.unit == "ms") {
        if (spec.low < 0.0f)
            std::snprintf(buf, sizeof(buf), "%+.2f ms", static_cast<double>(c));
        else if (c < 20.0f)
            std::snprintf(buf, sizeof(buf), "%.1f ms", static_cast<double>(c));
        else
            std::snprintf(buf, sizeof(buf), "%.0f ms", static_cast<double>(c));
    } else if (spec.unit == "dB") {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(c));
    } else if (spec.unit == "st") {
        if (spec.low < 0.0f)
            std::snprintf(buf, sizeof(buf), "%+.1f st", static_cast<double>(c));
        else
            std::snprintf(buf, sizeof(buf), "+%.1f st", static_cast<double>(c));
    } else if (spec.unit == "deg") {
        std::snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", static_cast<double>(c));
    } else if (spec.unit == "x") {
        std::snprintf(buf, sizeof(buf), "%.2fx", static_cast<double>(c));
    } else if (spec.unit == "%") {
        if (spec.low < 0.0f)
            std::snprintf(buf, sizeof(buf), "%+.0f%%", static_cast<double>(c));
        else
            std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(c));
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(c));
    }
    return buf;
}

Params Params::defaults() {
    Params p;
    for (std::size_t i = 0; i < kParamCount; ++i)
        p.values[i] = kSpecs[i].def;
    return p;
}

} // namespace sb
