// The feedback loop's delay line: written once per oversampled sample, read at
// a fractional delay with 3rd-order Lagrange interpolation, followed by a DC
// blocker. Clearing only touches what was written since the last clear, so
// going idle stays cheap after short notes.
#pragma once

#include "control.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tf::dsp {

class FeedbackDelay {
public:
    void prepare(int maxDelaySamples) {
        size_ = std::max(8, maxDelaySamples + 8);
        buffer_.assign(static_cast<std::size_t>(size_), 0.0f);
        pos_ = 0;
        written_ = 0;
    }

    // Longest delay read() accepts.
    double maxDelay() const noexcept TF_NONBLOCKING { return static_cast<double>(size_ - 4); }

    void clear() noexcept TF_NONBLOCKING {
        int p = pos_;
        for (int i = 0; i < written_; ++i) {
            p = p == 0 ? size_ - 1 : p - 1;
            buffer_[static_cast<std::size_t>(p)] = 0.0f;
        }
        written_ = 0;
    }

    // Value written `delay` samples ago (1 = the most recent write), delay >= 2.
    double read(double delay) const noexcept TF_NONBLOCKING {
        const double d = std::clamp(delay, 2.0, maxDelay());
        const int n = static_cast<int>(std::floor(d)) - 1;
        const double t = d - n;  // in [1, 2): between the middle taps
        const double h0 = -(t - 1.0) * (t - 2.0) * (t - 3.0) / 6.0;
        const double h1 = t * (t - 2.0) * (t - 3.0) / 2.0;
        const double h2 = -t * (t - 1.0) * (t - 3.0) / 2.0;
        const double h3 = t * (t - 1.0) * (t - 2.0) / 6.0;
        return h0 * at(n) + h1 * at(n + 1) + h2 * at(n + 2) + h3 * at(n + 3);
    }

    void write(float x) noexcept TF_NONBLOCKING {
        buffer_[static_cast<std::size_t>(pos_)] = x;
        pos_ = pos_ + 1 == size_ ? 0 : pos_ + 1;
        written_ = std::min(written_ + 1, size_);
    }

private:
    double at(int delay) const noexcept TF_NONBLOCKING {
        int i = pos_ - delay;
        if (i < 0) i += size_;
        return buffer_[static_cast<std::size_t>(i)];
    }

    std::vector<float> buffer_;
    int size_ = 8;
    int pos_ = 0;
    int written_ = 0;
};

// One-pole DC blocker: y = x - x1 + r y1, r = 1 - 2 pi fc / fs.
class DcBlocker {
public:
    void prepare(double cutoffHz, double sampleRate) noexcept TF_NONBLOCKING {
        r_ = 1.0 - kTwoPi * cutoffHz / sampleRate;
    }
    void reset() noexcept TF_NONBLOCKING { x1_ = y1_ = 0.0; }
    double process(double x) noexcept TF_NONBLOCKING {
        const double y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    double r_ = 0.999;
    double x1_ = 0.0, y1_ = 0.0;
};

}
