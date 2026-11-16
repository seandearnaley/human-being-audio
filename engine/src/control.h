// Small building blocks shared by the DSP pieces: constants, a one-pole
// parameter smoother, a linear ramp and a fixed-capacity delay line.
#pragma once

#include "tf/nonblocking.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace tf::dsp {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

inline double dbToGain(double db) noexcept TF_NONBLOCKING { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double gain) noexcept TF_NONBLOCKING { return 20.0 * std::log10(std::max(gain, 1e-30)); }

// x - floor(x): the fractional part, in [0, 1) also for negative x.
inline double frac(double x) noexcept TF_NONBLOCKING { return x - std::floor(x); }

// Coefficient of a one-pole smoother that is updated every `periodSeconds`
// and has time constant `tauSeconds`.
inline double onePoleCoef(double tauSeconds, double periodSeconds) noexcept TF_NONBLOCKING {
    if (tauSeconds <= 0.0) return 1.0;
    return 1.0 - std::exp(-periodSeconds / tauSeconds);
}

// One-pole smoother. The first value it sees is taken as is (no fade from zero).
class Smoother {
public:
    void reset() noexcept TF_NONBLOCKING { primed_ = false; }
    double value() const noexcept TF_NONBLOCKING { return value_; }
    double step(double target, double coef) noexcept TF_NONBLOCKING {
        if (!primed_) {
            value_ = target;
            primed_ = true;
        } else {
            value_ += coef * (target - value_);
        }
        return value_;
    }
    void snap(double target) noexcept TF_NONBLOCKING {
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
    void to(double end, int steps) noexcept TF_NONBLOCKING {
        increment = steps > 0 ? (end - value) / steps : 0.0;
        if (steps <= 0) value = end;
    }
    void snap(double v) noexcept TF_NONBLOCKING {
        value = v;
        increment = 0.0;
    }
    double next() noexcept TF_NONBLOCKING {
        value += increment;
        return value;
    }
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
    void clear() noexcept TF_NONBLOCKING { std::fill(buffer_.begin(), buffer_.end(), T {}); }
    int delay() const noexcept TF_NONBLOCKING { return delay_; }
    // Writes x and returns the sample written `delay` calls ago.
    T process(T x) noexcept TF_NONBLOCKING {
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

}
