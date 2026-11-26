// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic, zero-allocation audio-thread PRNGs.
#pragma once

#include <hba/dsp/nonblocking.h>

#include <cstdint>

namespace hba::dsp {

// 32-bit Numerical Recipes LCG (fast white noise for audio-thread exciters & audition kits).
class Lcg32 {
  public:
    explicit constexpr Lcg32(std::uint32_t initial = 0x12345678u) noexcept : state_(initial) {}

    void seed(std::uint32_t value) noexcept HBA_NONBLOCKING { state_ = value; }

    std::uint32_t nextU32() noexcept HBA_NONBLOCKING {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

    // Uniform in [-1, 1).
    double nextBipolar() noexcept HBA_NONBLOCKING { return (static_cast<double>(nextU32()) / 2147483648.0) - 1.0; }

    // Uniform in [0, 1).
    double nextUnit() noexcept HBA_NONBLOCKING { return static_cast<double>(nextU32()) / 4294967296.0; }

  private:
    std::uint32_t state_;
};

// Marsaglia XorShift32 (non-zero state enforced automatically).
class XorShift32 {
  public:
    explicit constexpr XorShift32(std::uint32_t initial = 0x9E3779B9u) noexcept
        : state_(initial == 0u ? 0x9E3779B9u : initial) {}

    void seed(std::uint32_t value) noexcept HBA_NONBLOCKING { state_ = value == 0u ? 0x9E3779B9u : value; }

    std::uint32_t nextU32() noexcept HBA_NONBLOCKING {
        std::uint32_t x = state_;
        x ^= x << 13u;
        x ^= x >> 17u;
        x ^= x << 5u;
        state_ = x;
        return x;
    }

    double nextBipolar() noexcept HBA_NONBLOCKING { return (static_cast<double>(nextU32()) / 2147483648.0) - 1.0; }

    double nextUnit() noexcept HBA_NONBLOCKING { return static_cast<double>(nextU32()) / 4294967296.0; }

  private:
    std::uint32_t state_;
};

} // namespace hba::dsp
