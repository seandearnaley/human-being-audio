// SPDX-License-Identifier: GPL-3.0-or-later
// Shared DSP primitives: constants, amplitude/dB helpers, smoothers, ramps,
// one-pole DC blocker, and fixed-capacity integer delay line.
#pragma once

#include <hba/dsp/gain.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace hba::dsp {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

inline double gainToDb(double gain) noexcept HBA_NONBLOCKING { return 20.0 * std::log10(std::max(gain, 1e-30)); }

// Fractional part in [0, 1), including for negative inputs.
inline double frac(double x) noexcept HBA_NONBLOCKING { return x - std::floor(x); }

// Coefficient of a one-pole smoother updated every `periodSeconds` with time constant `tauSeconds`.
inline double onePoleCoef(double tauSeconds, double periodSeconds) noexcept HBA_NONBLOCKING {
    if (tauSeconds <= 0.0) return 1.0;
    return 1.0 - std::exp(-periodSeconds / tauSeconds);
}

// One-pole smoother. The first value it sees is taken as is (no fade from zero).
class Smoother {
public:
    void reset() noexcept HBA_NONBLOCKING { primed_ = false; }
    double value() const noexcept HBA_NONBLOCKING { return value_; }
    double step(double target, double coef) noexcept HBA_NONBLOCKING {
        if (!primed_) {
            value_ = target;
            primed_ = true;
        } else {
            value_ += coef * (target - value_);
        }
        return value_;
    }
    void snap(double target) noexcept HBA_NONBLOCKING {
        value_ = target;
        primed_ = true;
    }

private:
    double value_ = 0.0;
    bool primed_ = false;
};

// A value that moves linearly to a new end point over a fixed number of steps.
struct Ramp {
    double value = 0.0;
    double increment = 0.0;
    void to(double end, int steps) noexcept HBA_NONBLOCKING {
        increment = steps > 0 ? (end - value) / steps : 0.0;
        if (steps <= 0) value = end;
    }
    void snap(double v) noexcept HBA_NONBLOCKING {
        value = v;
        increment = 0.0;
    }
    double next() noexcept HBA_NONBLOCKING {
        value += increment;
        return value;
    }
};

// One-pole DC blocker: y = x - x1 + r * y1, r = exp(-2 pi fc / fs).
class DcBlocker {
public:
    void prepare(double cutoffHz, double sampleRate) noexcept HBA_NONBLOCKING {
        r_ = std::exp(-kTwoPi * cutoffHz / std::max(sampleRate, 1.0));
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING { x1_ = y1_ = 0.0; }
    double process(double x) noexcept HBA_NONBLOCKING {
        const double y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    double r_ = 0.999;
    double x1_ = 0.0;
    double y1_ = 0.0;
};

// Integer delay line with a fixed capacity chosen in prepare().
template <typename T>
class DelayLine {
public:
    void prepare(int delaySamples) {
        delay_ = std::max(0, delaySamples);
        buffer_.assign(static_cast<std::size_t>(delay_ + 1), T {});
        pos_ = 0;
    }
    void clear() noexcept HBA_NONBLOCKING { std::fill(buffer_.begin(), buffer_.end(), T {}); }
    int delay() const noexcept HBA_NONBLOCKING { return delay_; }
    // Writes x and returns the sample written `delay` calls ago.
    T process(T x) noexcept HBA_NONBLOCKING {
        if (delay_ == 0) return x;
        buffer_[static_cast<std::size_t>(pos_)] = x;
        int read = pos_ + 1;
        if (read > delay_) read = 0;
        const T out = buffer_[static_cast<std::size_t>(read)];
        pos_ = read;
        return out;
    }

private:
    std::vector<T> buffer_;
    int delay_ = 0;
    int pos_ = 0;
};

}  // namespace hba::dsp
