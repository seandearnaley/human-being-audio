// SPDX-License-Identifier: GPL-3.0-or-later
// C^2-smooth soft clippers and a 4x true-peak look-ahead limiter after
// Geraint Luff's design (https://signalsmith-audio.co.uk/writing/2022/limiter/).
#pragma once

#include <hba/dsp/control.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace hba::dsp {

// Identity below `knee`, then a C^2-smooth tanh shoulder asymptoting to `bound`.
inline constexpr double kSoftClipKnee = 0.8;

inline double softClip(double x, double knee = kSoftClipKnee, double bound = 1.0) noexcept HBA_NONBLOCKING {
    if (!std::isfinite(x))
        return 0.0;
    const double a = std::abs(x);
    if (a <= knee) return x;
    const double range = bound - knee;
    if (range <= 1.0e-12)
        return x < 0.0 ? -bound : bound;
    const double y = knee + range * std::tanh((a - knee) / range);
    return x < 0.0 ? -y : y;
}

// Linked stereo soft clip using a single gain derived from max(|L|, |R|).
inline double softClipLinked(double& left, double& right, double knee = kSoftClipKnee,
                             double bound = 1.0) noexcept HBA_NONBLOCKING {
    const double peak = std::max(std::abs(left), std::abs(right));
    if (peak <= knee) return 1.0;
    const double gain = softClip(peak, knee, bound) / peak;
    left *= gain;
    right *= gain;
    return gain;
}

// First antiderivative F1(u) = integral_0^u softClip(s, knee, 1.0) ds for ADAA1 clipping.
// Even, C^3-smooth, overflow-free for arbitrarily large |u|.
inline double softClipUnitF1(double u, double knee = kSoftClipKnee) noexcept HBA_NONBLOCKING {
    const double a = std::abs(u);
    if (a <= knee)
        return 0.5 * a * a;
    const double r = 1.0 - knee;
    if (r <= 1.0e-12)
        return 0.5 * knee * knee + knee * (a - knee);
    const double t = a - knee;
    const double z = t / r;
    constexpr double kLn2 = 0.693147180559945309417232121458176568;
    const double logCosh = z - kLn2 + std::log1p(std::exp(-2.0 * z));
    return 0.5 * knee * knee + knee * t + r * r * logCosh;
}

// First-order anti-aliased (ADAA1) C^2 soft clipper with exact 0.5-sample delay.
// Below `kneeRatio * bound` on both consecutive samples, reduces bit-identically to
// `0.5 * (x + prevX)` independent of `bound`, so linear bypass / path alignment stays exact.
class SoftClipAdaa1 {
  public:
    void reset() noexcept HBA_NONBLOCKING { prevX_ = 0.0; }

    double process(double x, double kneeRatio = kSoftClipKnee, double bound = 1.0) noexcept HBA_NONBLOCKING {
        if (!std::isfinite(x))
            x = 0.0;
        const double x0 = prevX_;
        prevX_ = x;
        const double safeBound = std::isfinite(bound) ? std::max(bound, 1.0e-9) : 1.0;
        const double k = std::clamp(kneeRatio, 0.0, 1.0);
        const double knee = k * safeBound;
        if (std::abs(x) <= knee && std::abs(x0) <= knee)
            return 0.5 * (x + x0);

        const double invBound = 1.0 / safeBound;
        const double u0 = x0 * invBound;
        const double u1 = x * invBound;
        const double du = u1 - u0;
        if (std::abs(du) < 1.0e-6)
            return safeBound * softClip(0.5 * (u0 + u1), k, 1.0);
        return safeBound * ((softClipUnitF1(u1, k) - softClipUnitF1(u0, k)) / du);
    }

  private:
    double prevX_ = 0.0;
};

// 4x Kaiser-windowed-sinc true-peak detector.
class TruePeakDetector {
public:
    static constexpr int kPhases = 4;
    static constexpr int kDelay = 28;
    static constexpr int kWidth = 2 * kDelay;

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
        for (int r = 1; r < kPhases; ++r)
            for (int j = 0; j < kWidth; ++j) {
                const int m = kWidth - 1 - j;
                const int t = r + kPhases * m;
                taps_[static_cast<std::size_t>(r - 1)][static_cast<std::size_t>(j)] = t < length ? static_cast<float>(h(t)) : 0.0f;
            }
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
        for (auto& h : history_) h.fill(0.0f);
        pos_ = 0;
    }

    double process(double left, double right) noexcept HBA_NONBLOCKING {
        pos_ = pos_ + 1 == kWidth ? 0 : pos_ + 1;
        const std::size_t w = static_cast<std::size_t>(pos_);
        float peak = 0.0f;
        const double in[2] = {std::isfinite(left) ? left : 0.0, std::isfinite(right) ? right : 0.0};
        for (std::size_t c = 0; c < 2; ++c) {
            auto& h = history_[c];
            h[w] = h[w + kWidth] = static_cast<float>(in[c]);
            const float* window = h.data() + w + 1;
            peak = std::max(peak, std::abs(window[kWidth - 1 - kDelay]));
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

// Monotonic-queue sliding minimum over the last `window` values.
class MovingMinimum {
public:
    void prepare(int window) {
        window_ = std::max(1, window);
        values_.assign(static_cast<std::size_t>(window_ + 1), 0.0);
        indices_.assign(static_cast<std::size_t>(window_ + 1), 0);
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
        head_ = tail_ = 0;
        count_ = 0;
        index_ = 0;
    }
    double process(double x) noexcept HBA_NONBLOCKING {
        if (values_.empty() || indices_.empty())
            return std::isfinite(x) ? x : 1.0;
        if (!std::isfinite(x))
            x = 1.0;
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
    static constexpr double kMarginDb = 0.05;
    static constexpr std::size_t kMaxDelayed = 4;

    void prepare(double sampleRate, double releaseSeconds = kReleaseSeconds) {
        const double sr = std::max(sampleRate, 1.0);
        lookahead_ = std::max(1, static_cast<int>(std::lround(kLookaheadSeconds * sr)));
        delay_ = lookahead_ - 1 + TruePeakDetector::kDelay;
        for (auto& d : delays_) d.prepare(delay_);
        hold_.prepare(lookahead_);
        box_.assign(static_cast<std::size_t>(lookahead_), 1.0);
        releaseCoef_ = onePoleCoef(std::max(releaseSeconds, 1.0e-6), 1.0 / sr);
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
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

    int latency() const noexcept HBA_NONBLOCKING { return delay_; }
    double gain() const noexcept HBA_NONBLOCKING { return gain_; }

    template <std::size_t N>
    void process(double& left, double& right, std::array<double, N>& delayed, double ceiling,
                 double amount) noexcept HBA_NONBLOCKING {
        static_assert(N <= kMaxDelayed);
        if (box_.empty())
            return;
        if (!std::isfinite(left))
            left = 0.0;
        if (!std::isfinite(right))
            right = 0.0;
        const double safeCeiling = std::isfinite(ceiling) ? std::max(1.0e-6, ceiling) : 1.0;
        const double target = safeCeiling * marginGain_;
        const double peak = detector_.process(left, right);
        const double required = peak > target ? target / peak : 1.0;
        const double covering = std::min(required, previousRequired_);
        previousRequired_ = required;

        const double held = hold_.process(covering);
        released_ = held < released_ ? held : released_ + releaseCoef_ * (held - released_);

        boxSum_ += released_ - box_[static_cast<std::size_t>(boxPos_)];
        box_[static_cast<std::size_t>(boxPos_)] = released_;
        boxPos_ = boxPos_ + 1 == lookahead_ ? 0 : boxPos_ + 1;
        if (boxPos_ == 0) {
            // Periodically re-sum to eliminate accumulated floating-point drift.
            double exact = 0.0;
            for (double v : box_)
                exact += v;
            boxSum_ = exact;
        }
        const double smooth = std::clamp(boxSum_ / lookahead_, 0.0, 1.0);

        gain_ = 1.0 - amount * (1.0 - smooth);
        left = delays_[0].process(left) * gain_;
        right = delays_[1].process(right) * gain_;
        for (std::size_t i = 0; i < N; ++i) delayed[i] = delays_[2 + i].process(delayed[i]);
    }

    void process(double& left, double& right, double ceiling, double amount) noexcept HBA_NONBLOCKING {
        std::array<double, 0> none {};
        process(left, right, none, ceiling, amount);
    }

private:
  double marginGain_ = dbToGain(-kMarginDb);
  TruePeakDetector detector_;
  MovingMinimum hold_;
  std::array<DelayLine<double>, 2 + kMaxDelayed> delays_{};
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

}  // namespace hba::dsp
