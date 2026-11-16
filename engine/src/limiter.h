// Output stage: a soft clip, then a look-ahead limiter after Geraint Luff's
// design (https://signalsmith-audio.co.uk/writing/2022/limiter/): the gain each
// sample needs is held at its minimum for the look-ahead length, released with
// a one-pole, and smoothed with a box filter of the same length, so the gain
// has fully arrived when the peak does. Peaks are detected on a 4x oversampled
// (linear-phase windowed-sinc) sidechain, so inter-sample peaks count.
#pragma once

#include "control.h"

#include <array>
#include <cmath>
#include <vector>

namespace tf::dsp {

// Identity below the knee, then a tanh shoulder that never exceeds 1.
inline constexpr double kSoftClipKnee = 0.8;

inline double softClip(double x) noexcept TF_NONBLOCKING {
    const double a = std::abs(x);
    if (a <= kSoftClipKnee) return x;
    const double range = 1.0 - kSoftClipKnee;
    const double y = kSoftClipKnee + range * std::tanh((a - kSoftClipKnee) / range);
    return x < 0.0 ? -y : y;
}

// The soft clip applied to a stereo pair with one gain, taken from the larger
// channel, so it never turns the (mono) low end into side signal.
inline void softClipLinked(double& left, double& right) noexcept TF_NONBLOCKING {
    const double peak = std::max(std::abs(left), std::abs(right));
    if (peak <= kSoftClipKnee) return;
    const double gain = softClip(peak) / peak;
    left *= gain;
    right *= gain;
}

// 4x interpolator for peak detection: a Kaiser-windowed sinc reaching kDelay
// base-rate samples either side. The decimators leave real content up to the
// base Nyquist, so the interpolator must stay accurate close to it; a short
// one under-reads true peaks there by most of a dB. Phase 0 is the input
// sample itself (delayed by kDelay), phases 1-3 are interpolated.
class TruePeakDetector {
public:
    static constexpr int kPhases = 4;
    static constexpr int kDelay = 32;           // base-rate samples
    static constexpr int kWidth = 2 * kDelay;   // taps per interpolated phase

    TruePeakDetector() {
        constexpr int length = kPhases * kWidth + 1;
        constexpr double centre = kPhases * kDelay;
        constexpr double beta = 7.0;
        const auto bessel = [](double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        const auto h = [&](int t) {
            const double x = (t - centre) / kPhases;
            const double sinc = x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double r = (t - centre) / centre;
            return sinc * bessel(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel(beta);
        };
        // taps_[r-1][j] multiplies the j-th oldest of the last kWidth inputs.
        for (int r = 1; r < kPhases; ++r)
            for (int j = 0; j < kWidth; ++j) {
                const int m = kWidth - 1 - j;  // input age
                const int t = r + kPhases * m;
                taps_[static_cast<std::size_t>(r - 1)][static_cast<std::size_t>(j)] = t < length ? static_cast<float>(h(t)) : 0.0f;
            }
        reset();
    }
    void reset() noexcept TF_NONBLOCKING {
        for (auto& h : history_) h.fill(0.0f);
        pos_ = 0;
    }

    // Largest magnitude of the interpolated signal between input samples
    // n - kDelay and n - kDelay + 1, over both channels.
    double process(double left, double right) noexcept TF_NONBLOCKING {
        // Each history is written twice, kWidth apart, so the last kWidth
        // inputs are always contiguous (oldest first) at pos_ + 1.
        pos_ = pos_ + 1 == kWidth ? 0 : pos_ + 1;
        const std::size_t w = static_cast<std::size_t>(pos_);
        float peak = 0.0f;
        const double in[2] = { left, right };
        for (std::size_t c = 0; c < 2; ++c) {
            auto& h = history_[c];
            h[w] = h[w + kWidth] = static_cast<float>(in[c]);
            const float* window = h.data() + w + 1;
            peak = std::max(peak, std::abs(window[kWidth - 1 - kDelay]));  // phase 0: exact sample
            for (const auto& phase : taps_) {
                float v = 0.0f;
                for (std::size_t j = 0; j < kWidth; ++j) v += phase[j] * window[j];
                peak = std::max(peak, std::abs(v));
            }
        }
        return peak;
    }

private:
    std::array<std::array<float, kWidth>, kPhases - 1> taps_ {};
    std::array<std::array<float, 2 * kWidth + 1>, 2> history_ {};
    int pos_ = 0;
};

// Running minimum over the last `window` values (monotonic queue).
class MovingMinimum {
public:
    void prepare(int window) {
        window_ = std::max(1, window);
        values_.assign(static_cast<std::size_t>(window_ + 1), 0.0);
        indices_.assign(static_cast<std::size_t>(window_ + 1), 0);
        reset();
    }
    void reset() noexcept TF_NONBLOCKING {
        head_ = tail_ = 0;
        count_ = 0;
        index_ = 0;
    }
    double process(double x) noexcept TF_NONBLOCKING {
        const int capacity = window_ + 1;
        while (count_ > 0) {
            const int back = tail_ == 0 ? capacity - 1 : tail_ - 1;
            if (values_[static_cast<std::size_t>(back)] < x) break;
            tail_ = back;
            --count_;
        }
        values_[static_cast<std::size_t>(tail_)] = x;
        indices_[static_cast<std::size_t>(tail_)] = index_;
        tail_ = tail_ + 1 == capacity ? 0 : tail_ + 1;
        ++count_;
        while (indices_[static_cast<std::size_t>(head_)] <= index_ - window_) {
            head_ = head_ + 1 == capacity ? 0 : head_ + 1;
            --count_;
        }
        ++index_;
        return values_[static_cast<std::size_t>(head_)];
    }

private:
    std::vector<double> values_;
    std::vector<long long> indices_;
    int window_ = 1, head_ = 0, tail_ = 0, count_ = 0;
    long long index_ = 0;
};

class Limiter {
public:
    static constexpr double kLookaheadSeconds = 0.0015;
    static constexpr double kReleaseSeconds = 0.08;
    static constexpr double kMarginDb = 0.05;  // aim this far below the ceiling

    void prepare(double sampleRate) {
        lookahead_ = std::max(1, static_cast<int>(std::lround(kLookaheadSeconds * sampleRate)));
        delay_ = lookahead_ - 1 + TruePeakDetector::kDelay;
        for (auto& d : delays_) d.prepare(delay_);
        hold_.prepare(lookahead_);
        box_.assign(static_cast<std::size_t>(lookahead_), 1.0);
        releaseCoef_ = onePoleCoef(kReleaseSeconds, 1.0 / sampleRate);
        reset();
    }
    void reset() noexcept TF_NONBLOCKING {
        for (auto& d : delays_) d.clear();
        detector_.reset();
        hold_.reset();
        std::fill(box_.begin(), box_.end(), 1.0);
        boxSum_ = static_cast<double>(lookahead_);
        boxPos_ = 0;
        released_ = 1.0;
        previousRequired_ = 1.0;
        gain_ = 1.0;
    }

    // Samples between the input and the output of process().
    int latency() const noexcept TF_NONBLOCKING { return delay_; }
    double gain() const noexcept TF_NONBLOCKING { return gain_; }

    // Limits the stereo pair to `ceiling` (linear). `amount` 0 bypasses the
    // gain (the delay stays). `taps` are delayed alongside the audio.
    void process(double& left, double& right, std::array<double, 2>& taps, double ceiling,
                 double amount) noexcept TF_NONBLOCKING {
        const double target = ceiling * marginGain_;
        const double peak = detector_.process(left, right);
        const double required = peak > target ? target / peak : 1.0;
        const double covering = std::min(required, previousRequired_);
        previousRequired_ = required;

        const double held = hold_.process(covering);
        released_ = held < released_ ? held : released_ + releaseCoef_ * (held - released_);

        boxSum_ += released_ - box_[static_cast<std::size_t>(boxPos_)];
        box_[static_cast<std::size_t>(boxPos_)] = released_;
        boxPos_ = boxPos_ + 1 == lookahead_ ? 0 : boxPos_ + 1;
        const double smooth = std::min(1.0, boxSum_ / lookahead_);

        gain_ = 1.0 - amount * (1.0 - smooth);
        left = delays_[0].process(left) * gain_;
        right = delays_[1].process(right) * gain_;
        taps[0] = delays_[2].process(taps[0]);
        taps[1] = delays_[3].process(taps[1]);
    }

private:
    double marginGain_ = std::pow(10.0, -kMarginDb / 20.0);
    TruePeakDetector detector_;
    MovingMinimum hold_;
    std::array<DelayLine<double>, 4> delays_ {};
    std::vector<double> box_;
    double boxSum_ = 1.0;
    int boxPos_ = 0;
    int lookahead_ = 1;
    int delay_ = 0;
    double releaseCoef_ = 1.0;
    double released_ = 1.0;
    double previousRequired_ = 1.0;
    double gain_ = 1.0;
};

}
