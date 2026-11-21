// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>

namespace hba::dsp {
// Amplitude gain. No noise floor or clipping: callers own their level policy.
inline double dbToGain(double db) noexcept
#if __has_cpp_attribute(clang::nonblocking)
    [[clang::nonblocking]]
#endif
{
    return std::pow(10.0, db / 20.0);
}
}
