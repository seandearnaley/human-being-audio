// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace sb {

inline constexpr int kModeCount = 24;
inline constexpr int kArticulationCount = 4;
inline constexpr int kScopeSamples = 256;

enum class ParamKind : std::uint8_t { continuous, choice, toggle };
enum class Articulation : std::uint8_t { main = 0, ghost = 1, rim = 2, roll = 3 };
enum class BodyWave : std::uint8_t { pureSine = 0, warm909 = 1, triPunch = 2, subSquare = 3 };
enum class ShellModel : std::uint8_t {
    snareShell = 0,
    kirchhoffPlate = 1,
    drumhead = 2,
    metallicBar = 3,
    hollowPipe = 4,
    glassBell = 5
};
enum class MalletType : std::uint8_t {
    woodStick = 0,
    hardFelt = 1,
    metalHammer = 2,
    rimFlam = 3,
    skinThump = 4,
    laserSnap = 5
};
enum class WireColor : std::uint8_t { snareWire = 0, crushedMetal = 1, airSizzle = 2 };
enum class KeyTrackMode : std::uint8_t { kitMap = 0, chromatic = 1 };
enum class ChokeMode : std::uint8_t { splitSub = 0, poly4x = 1, monoCut = 2 };
enum class LayerSolo : std::uint8_t { none = 0, body = 1, shell = 2, wire = 3 };

enum class ParamId : std::size_t {
    // 1. Fundamental Body
    bodyHz,
    bodyWave,
    bodyPhase,
    pitchDrop,
    pitchCurve,
    pitchTime,
    bodyDecay,
    bodyFm,
    bodyFmRatio,
    bodyLevel,

    // 2. Nonlinear Tensioned Modal Shell (24 Modes)
    shellModel,
    malletType,
    shellTune,
    shellTension,
    strikePos,
    shellStretch,
    shellDecay,
    shellDamp,
    shellSpread,
    shellOffsetMs,
    shellInvert,
    shellLevel,

    // 3. Transient-Ducked Wire & Click Layer
    wireColor,
    wireBuzz,
    clickAmt,
    clickTone,
    wireDuck,
    wireDuckMs,
    wireTone,
    wireDecay,
    wireWidth,
    wireOffsetMs,
    wireInvert,
    wireLevel,
    humanize,

    // 4. Punch, Disperser, Tone Sculptor, Room, ADAA2 & Output
    disperserAmt,
    disperserHz,
    drive,
    driveModel,
    scoopAmt,
    roomAmt,
    chokeMode,
    keyTrack,
    subMono,
    limiter,
    quality,
    outputDb,
    bypass,

    // 5. Performance Macros (automatable!)
    macroPunch,
    macroTight,
    macroMetal,
    macroSmack,

    // 6. Layer Audition Mute / Solo
    bodyMute,
    shellMute,
    wireMute,
    layerSolo,

    count
};

inline constexpr std::size_t kParamCount = static_cast<std::size_t>(ParamId::count);
static_assert(kParamCount <= 64, "Strike Body stays within Ableton Live's 64-parameter direct configure limit");

struct ParamSpec {
    ParamId id = ParamId::bodyHz;
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
std::string hzToNoteTag(float hz);

inline constexpr std::array<int, 3> kQualityFactors{2, 4, 8};

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

} // namespace sb
