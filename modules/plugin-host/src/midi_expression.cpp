// SPDX-License-Identifier: GPL-3.0-or-later
#include <hba/host/midi_expression.h>

namespace hba::host {

float MidiExpressionParser::normalisePitchWheel(int raw14Bit) noexcept {
    const int delta = raw14Bit - 8192;
    return delta >= 0 ? static_cast<float>(delta) / 8191.0f : static_cast<float>(delta) / 8192.0f;
}

} // namespace hba::host
