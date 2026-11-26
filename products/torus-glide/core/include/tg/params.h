// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <hba/dsp/curve.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tg {

inline constexpr int kLaneCount = 2;
inline constexpr int kVoiceCount = 5;
inline constexpr int kSceneCount = 4;
inline constexpr int kOctaveCount = 7;
inline constexpr int kPadProgramCount = 6;

enum class ParamKind : std::uint8_t { continuous, choice, toggle };

enum class Mode : std::uint8_t { spiral = 0, copedent = 1, mobius = 2 };
enum class SpiralDir : std::uint8_t { rise = 0, fall = 1, pendulum = 2 };
enum class GravityMode : std::uint8_t { scaleWells = 0, justIntonation = 1, hybrid = 2 };
enum class StaggerOrder : std::uint8_t { innerFirst = 0, lowToHigh = 1, highToLow = 2, outsideIn = 3 };
enum class TimbreKind : std::uint8_t {
    reese = 0,
    sine = 1,
    formant = 2,
    wavefold = 3,
    pulse = 4,
    glassBell = 5,
    superOrgan = 6
};
enum class FilterMode : std::uint8_t { lp12 = 0, lp24 = 1, bandpass = 2, neuroNotch = 3 };
enum class PadProgram : std::uint8_t { spiral = 0, mobius = 1, copedent = 2, abyss = 3, risset = 4, voidSpace = 5 };
enum class LaneTarget : std::uint8_t {
    morph = 0,
    bellCenter = 1,
    filter = 2,
    drive = 3,
    gravity = 4,
    rissetGate = 5,
    space = 6
};

enum class ParamId : std::size_t {
    // Mode & Global
    mode,
    rootKey,
    scale,
    quality,

    // Spiral / Shepard Geometry
    spiralDir,
    loopBars,
    spiralRatio,
    spiralStack,
    bellCenter,
    bellWidth,
    phaseOffset,

    // Copedent / Poly-Bend Geometry
    voicingPack,
    morph,
    morphAuto,
    gravity,
    gravityMode,
    stagger,
    staggerOrder,
    anchorSub,

    // Sound & Sub-Anchor Core
    timbre,
    detune,
    width,
    space,
    subAnchor,
    subWarmth,
    subCutoff,
    drive,
    driveModel,
    filterMode,
    filterHz,
    filterRes,
    rissetGate,
    glideMs,

    // Envelope, Movement & Output/MPE
    attackMs,
    releaseMs,
    lane1Rate,
    lane1Depth,
    lane1Target,
    lane2Rate,
    lane2Depth,
    lane2Target,
    mpeOut,
    mpeRange,
    keySwitch,
    retrig,
    limiter,
    outputDb,
    bypass,

    // Playable 2D Pitch-Manifold Performance Surface
    padProgram,
    padX,
    padY,
    padTouch,
    padHold,

    count
};

struct PadCaptions {
    std::string_view name;
    std::string_view xLabel;
    std::string_view yLabel;
};
PadCaptions padCaptions(PadProgram program) noexcept HBA_NONBLOCKING;

inline constexpr std::size_t kParamCount = static_cast<std::size_t>(ParamId::count);

struct ParamSpec {
    ParamId id = ParamId::mode;
    std::string_view key;
    std::string_view name;
    ParamKind kind = ParamKind::continuous;
    float low = 0.0f;
    float high = 1.0f;
    float def = 0.0f;
    float skew = 1.0f;
    std::string_view unit;
    std::span<const std::string_view> choices;
    bool automatable = true;
};

const std::array<ParamSpec, kParamCount>& paramSpecs();
const ParamSpec& specOf(ParamId id);
const ParamSpec* findParam(std::string_view key);

float clampValue(const ParamSpec& spec, float real) noexcept HBA_NONBLOCKING;
float toNormalised(const ParamSpec& spec, float real) noexcept HBA_NONBLOCKING;
float fromNormalised(const ParamSpec& spec, float norm) noexcept HBA_NONBLOCKING;
std::string formatValue(const ParamSpec& spec, float real);

// Beats per full loop cycle for `loopBars` and `lane1Rate` choices:
// 0: 1/2 Bar (2 beats), 1: 1 Bar (4), 2: 2 Bars (8), 3: 4 Bars (16), 4: 8 Bars (32), 5: 16 Bars (64)
inline constexpr std::array<double, 6> kLoopBarBeats{2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
// Lane 2 rate choices: 1/4, 1/2, 1, 2, 4, 8 bars
inline constexpr std::array<double, 6> kLane2BarBeats{1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
// Oversampling factors for quality choice: Live 2x, Studio 4x, Ultra 8x
inline constexpr std::array<int, 3> kQualityFactors{2, 4, 8};
// MPE pitch-bend range choices (semitones)
inline constexpr std::array<float, 3> kMpeRanges{12.0f, 24.0f, 48.0f};
// Pitch ratio per Shepard cycle in semitones:
// Octave (12), Fifth (7.01955), Tritone (6), Minor 3rd (3), Bohlen-Pierce tritave (19.01955 / 1.5 -> 14.63)
inline constexpr std::array<double, 5> kSpiralRatioSemitones{12.0, 7.01955, 6.0, 3.0, 14.63042};

struct Params {
    std::array<float, kParamCount> values{};

    static Params defaults();

    float real(ParamId id) const noexcept HBA_NONBLOCKING { return values[static_cast<std::size_t>(id)]; }
    int choice(ParamId id) const noexcept HBA_NONBLOCKING {
        return static_cast<int>(std::lround(values[static_cast<std::size_t>(id)]));
    }
    bool flag(ParamId id) const noexcept HBA_NONBLOCKING { return values[static_cast<std::size_t>(id)] >= 0.5f; }
    void set(ParamId id, float realVal) noexcept HBA_NONBLOCKING { values[static_cast<std::size_t>(id)] = realVal; }
};

using LaneCurves = std::array<hba::dsp::Curve, kLaneCount>;
LaneCurves defaultLaneCurves();

// Quick preset curves for the movement lane editor.
hba::dsp::Curve presetRampUpCurve() noexcept HBA_NONBLOCKING;
hba::dsp::Curve presetRampDownCurve() noexcept HBA_NONBLOCKING;
hba::dsp::Curve presetTriangleCurve() noexcept HBA_NONBLOCKING;
hba::dsp::Curve presetSBloomCurve() noexcept HBA_NONBLOCKING;
hba::dsp::Curve presetStep4Curve() noexcept HBA_NONBLOCKING;

} // namespace tg
