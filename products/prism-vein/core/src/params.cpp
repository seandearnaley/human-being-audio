// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pv/params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace pv {

std::optional<ParamId> findParam(std::string_view key) {
    for (const auto& s : kParamSpecs) {
        if (s.key == key)
            return s.id;
    }
    return std::nullopt;
}

std::string formatValue(const ParamSpec& s, float real) {
    const float v = clampParam(s, real);
    if (s.kind == ParamKind::toggle)
        return v >= 0.5f ? "On" : "Off";
    if (s.kind == ParamKind::choice) {
        const auto idx =
            static_cast<std::size_t>(std::clamp(static_cast<int>(v + 0.5f), 0, static_cast<int>(s.choices.size()) - 1));
        return std::string(s.choices[idx]);
    }

    char buf[48]{};
    if (s.id == ParamId::harmonics) {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
    } else if (s.unit == "%") {
        if (s.low < 0.0f)
            std::snprintf(buf, sizeof(buf), "%+d%%", static_cast<int>(std::lround(v * 100.0f)));
        else
            std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(v * 100.0f)));
    } else if (s.unit == "Hz") {
        if (v >= 1000.0f)
            std::snprintf(buf, sizeof(buf), "%.2f kHz", v * 0.001f);
        else if (v >= 100.0f)
            std::snprintf(buf, sizeof(buf), "%.0f Hz", v);
        else
            std::snprintf(buf, sizeof(buf), "%.1f Hz", v);
    } else if (s.unit == "ms") {
        std::snprintf(buf, sizeof(buf), "%.0f ms", v);
    } else if (s.unit == "st") {
        std::snprintf(buf, sizeof(buf), "%+.1f st", v);
    } else if (s.unit == "ct") {
        std::snprintf(buf, sizeof(buf), "%.1f ct", v);
    } else if (s.unit == "dB") {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
    } else if (s.unit == "\xc2\xb0") {
        std::snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f", v);
    }
    return std::string(buf);
}

std::string describeChordMask(std::uint16_t mask) {
    const std::uint16_t clean = static_cast<std::uint16_t>(mask & 0x0FFFu);
    for (const auto& cp : kChordPresets) {
        if (cp.mask == clean)
            return std::string(cp.label);
    }
    static constexpr std::array<const char*, 12> kNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                        "F#", "G",  "G#", "A",  "A#", "B"};
    std::string out;
    int count = 0;
    for (int pc = 0; pc < 12; ++pc) {
        if ((clean & (1u << pc)) != 0u) {
            if (!out.empty())
                out += ' ';
            out += kNames[static_cast<std::size_t>(pc)];
            if (++count >= 5)
                break;
        }
    }
    return out.empty() ? "C" : out;
}

} // namespace pv
