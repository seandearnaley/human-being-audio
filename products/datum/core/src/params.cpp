// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dt/params.h"

#include <hba/core/note_name.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dt {

std::string noteNameForHz(float hz) {
    if (!(hz > 10.0f) || !std::isfinite(hz))
        return "--";
    const int midi =
        std::clamp(static_cast<int>(std::lround(69.0 + 12.0 * std::log2(static_cast<double>(hz) / 440.0))), 0, 127);
    return hba::core::midiNoteName(midi, false);
}

std::string formatValue(const ParamSpec& s, float real) {
    const float v = clampParam(s, real);
    if (s.kind == ParamKind::toggle)
        return v >= 0.5f ? "On" : "Off";
    if (s.kind == ParamKind::choice && !s.choices.empty()) {
        const std::size_t idx = static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::lround(v)), 0, static_cast<int>(s.choices.size()) - 1));
        return std::string(s.choices[idx]);
    }

    char buf[40]{};
    if (s.id == ParamId::tuneHz) {
        const std::string note = noteNameForHz(v);
        std::snprintf(buf, sizeof(buf), "%.1f Hz \xc2\xb7 %s", static_cast<double>(v), note.c_str());
        return buf;
    }
    if (s.unit == "Hz") {
        if (v >= 1000.0f)
            std::snprintf(buf, sizeof(buf), "%.2f kHz", static_cast<double>(v) * 0.001);
        else
            std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(v));
        return buf;
    }
    if (s.unit == "ms") {
        if (v >= 100.0f)
            std::snprintf(buf, sizeof(buf), "%.0f ms", static_cast<double>(v));
        else
            std::snprintf(buf, sizeof(buf), "%.1f ms", static_cast<double>(v));
        return buf;
    }
    if (s.unit == "dB") {
        if (s.id == ParamId::boxCutDb)
            std::snprintf(buf, sizeof(buf), "-%.1f dB", static_cast<double>(v));
        else
            std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(v));
        return buf;
    }
    if (s.unit == "\xc2\xb0") {
        std::snprintf(buf, sizeof(buf), "%.0f\xc2\xb0", static_cast<double>(v));
        return buf;
    }
    if (s.unit == "x") {
        std::snprintf(buf, sizeof(buf), "%.2f\xc3\x97", static_cast<double>(v));
        return buf;
    }
    if (s.unit == "%") {
        if (s.low < 0.0f)
            std::snprintf(buf, sizeof(buf), "%+.0f%%", static_cast<double>(v * 100.0f));
        else
            std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(v * 100.0f));
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    return buf;
}

} // namespace dt
