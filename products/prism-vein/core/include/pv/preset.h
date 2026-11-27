// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "pv/params.h"

#include <hba/dsp/curve.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pv {

using LaneCurves = std::array<hba::dsp::Curve, static_cast<std::size_t>(kLaneCount)>;

inline LaneCurves defaultLaneCurves() noexcept PV_NONBLOCKING {
    return {
        hba::dsp::curves::sine(),
        hba::dsp::curves::triangle(),
        hba::dsp::curves::rampUp(),
        hba::dsp::curves::rampDown(),
    };
}

struct Preset {
    std::string name = "Iridescent F Min9";
    std::string category = "Color Bass";
    Params params = Params::defaults();
    std::uint16_t chordMask = kDefaultChordMask;
    LaneCurves curves = defaultLaneCurves();
};

std::string serializePreset(const Preset& preset);
std::optional<Preset> parsePreset(std::string_view jsonText, std::string* errorOut = nullptr);

} // namespace pv
