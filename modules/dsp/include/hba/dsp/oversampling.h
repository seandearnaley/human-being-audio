// SPDX-License-Identifier: GPL-3.0-or-later
// HIIR polyphase IIR half-band upsampling and decimation building blocks.
//
// Coefficients from hiir::PolyphaseIir2Designer::compute_coefs (HIIR 1.50):
//   stage      transition (rel. to its input rate)   coefs   stop-band
//   2x -> 1x   0.0465  (pass 20 kHz at 44.1 kHz)      10      130.0 dB
//   4x -> 2x   0.25                                     5      144.1 dB
//   8x -> 4x   0.375                                    3      134.3 dB
//  16x -> 8x   0.4375                                   2      124.7 dB
#pragma once

#include <hba/dsp/nonblocking.h>

#include <array>
#include <cstddef>

#include "hiir/Downsampler2xFpu.h"
#include "hiir/Upsampler2xFpu.h"

namespace hba::dsp {

namespace decimator_coefs {
inline constexpr std::array<double, 10> k2 { 0.024685829054447571, 0.094249802591679088, 0.19694469585846766,
                                             0.31791855629187493,  0.44353126463368947,  0.56409762124863516,
                                             0.67467859072325931,  0.77455265141382068,  0.86624354158326644,
                                             0.95475553917103384 };
inline constexpr std::array<double, 5> k4 { 0.028414445279188464, 0.11393344727958085, 0.25906131103886926,
                                            0.4738722544857808, 0.78701940115286972 };
inline constexpr std::array<double, 3> k8 { 0.055907353696531173, 0.24352963280437187, 0.64715311429020916 };
inline constexpr std::array<double, 2> k16 { 0.10723890054318994, 0.53103059594468105 };
}  // namespace decimator_coefs

// HIIR's process_sample() contains assert(), which is a no-op in release builds
// and a call to __assert_rtn() in debug builds. Suppress only for these audited calls.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
#endif
template <class Stage>
inline float halfBand(Stage& stage, const float* in) noexcept HBA_NONBLOCKING {
    return stage.process_sample(in);
}
template <class Stage>
inline void halfBandUp(Stage& stage, float in, float* out) noexcept HBA_NONBLOCKING {
    stage.process_sample(out[0], out[1], in);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

inline constexpr int kMaxOversampling = 16;

// Decimation by 1, 2, 4, 8, or 16 from an oversampled synthesis domain.
class Decimator {
public:
    void prepare(int factor) {
        factor_ = factor;
        s2_.set_coefs(decimator_coefs::k2.data());
        s4_.set_coefs(decimator_coefs::k4.data());
        s8_.set_coefs(decimator_coefs::k8.data());
        s16_.set_coefs(decimator_coefs::k16.data());
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
        s2_.clear_buffers();
        s4_.clear_buffers();
        s8_.clear_buffers();
        s16_.clear_buffers();
    }
    int factor() const noexcept HBA_NONBLOCKING { return factor_; }

    float process(float* in) noexcept HBA_NONBLOCKING {
        int n = factor_;
        if (n >= 16) n = halve(s16_, in, n);
        if (n >= 8) n = halve(s8_, in, n);
        if (n >= 4) n = halve(s4_, in, n);
        if (n >= 2) n = halve(s2_, in, n);
        return in[0];
    }

private:
    template <class Stage>
    static int halve(Stage& stage, float* buf, int n) noexcept HBA_NONBLOCKING {
        for (int i = 0; i < n / 2; ++i) buf[i] = halfBand(stage, buf + 2 * i);
        return n / 2;
    }

    int factor_ = 1;
    hiir::Downsampler2xFpu<10> s2_;
    hiir::Downsampler2xFpu<5> s4_;
    hiir::Downsampler2xFpu<3> s8_;
    hiir::Downsampler2xFpu<2> s16_;
};

// Steep 2x up/down trip around a base-rate nonlinearity.
class BaseRate2x {
public:
    static constexpr int kFactor = 2;
    void prepare() {
        up_.set_coefs(decimator_coefs::k2.data());
        down_.set_coefs(decimator_coefs::k2.data());
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
        up_.clear_buffers();
        down_.clear_buffers();
    }
    void up(float x, float* out) noexcept HBA_NONBLOCKING { halfBandUp(up_, x, out); }
    float down(const float* in) noexcept HBA_NONBLOCKING { return halfBand(down_, in); }

private:
    hiir::Upsampler2xFpu<10> up_;
    hiir::Downsampler2xFpu<10> down_;
};

// Steep 4x up/down trip (130 dB + 144 dB HIIR stopband) around a base-rate nonlinearity.
class BaseRate4x {
  public:
    static constexpr int kFactor = 4;
    void prepare() {
        u2_.set_coefs(decimator_coefs::k2.data());
        u4_.set_coefs(decimator_coefs::k4.data());
        d2_.set_coefs(decimator_coefs::k2.data());
        d4_.set_coefs(decimator_coefs::k4.data());
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
        u2_.clear_buffers();
        u4_.clear_buffers();
        d2_.clear_buffers();
        d4_.clear_buffers();
    }
    void up(float x, float* out) noexcept HBA_NONBLOCKING {
        float mid[2];
        halfBandUp(u2_, x, mid);
        halfBandUp(u4_, mid[0], out + 0);
        halfBandUp(u4_, mid[1], out + 2);
    }
    float down(const float* in) noexcept HBA_NONBLOCKING {
        float mid[2];
        mid[0] = halfBand(d4_, in + 0);
        mid[1] = halfBand(d4_, in + 2);
        return halfBand(d2_, mid);
    }

  private:
    hiir::Upsampler2xFpu<10> u2_;
    hiir::Upsampler2xFpu<5> u4_;
    hiir::Downsampler2xFpu<10> d2_;
    hiir::Downsampler2xFpu<5> d4_;
};

// Matched 2x/4x/8x up/down oversampler for audio effects.
class Oversampler {
public:
    void prepare(int factor) {
        factor_ = (factor == 2 || factor == 4 || factor == 8) ? factor : 4;
        u2_.set_coefs(decimator_coefs::k2.data());
        u4_.set_coefs(decimator_coefs::k4.data());
        u8_.set_coefs(decimator_coefs::k8.data());
        d2_.set_coefs(decimator_coefs::k2.data());
        d4_.set_coefs(decimator_coefs::k4.data());
        d8_.set_coefs(decimator_coefs::k8.data());
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
        u2_.clear_buffers();
        u4_.clear_buffers();
        u8_.clear_buffers();
        d2_.clear_buffers();
        d4_.clear_buffers();
        d8_.clear_buffers();
    }
    int factor() const noexcept HBA_NONBLOCKING { return factor_; }

    // Passband round-trip (up + down) group delay in base-rate samples for HIIR k2/k4/k8.
    double latencyExact() const noexcept HBA_NONBLOCKING {
        if (factor_ <= 2)
            return 4.5483;
        if (factor_ == 4)
            return 6.2007;
        return 6.7549;
    }
    int latencySamples() const noexcept HBA_NONBLOCKING {
        if (factor_ <= 2)
            return 5;
        if (factor_ == 4)
            return 6;
        return 7;
    }

    void up(float x, float* out) noexcept HBA_NONBLOCKING {
        std::array<float, 8> tmp {};
        halfBandUp(u2_, x, out);
        if (factor_ >= 4) {
            tmp[0] = out[0];
            tmp[1] = out[1];
            halfBandUp(u4_, tmp[0], out + 0);
            halfBandUp(u4_, tmp[1], out + 2);
        }
        if (factor_ >= 8) {
            for (int i = 0; i < 4; ++i) tmp[static_cast<std::size_t>(i)] = out[i];
            for (int i = 0; i < 4; ++i) halfBandUp(u8_, tmp[static_cast<std::size_t>(i)], out + 2 * i);
        }
    }

    float down(float* buf) noexcept HBA_NONBLOCKING {
        int n = factor_;
        if (n >= 8) {
            for (int i = 0; i < 4; ++i) buf[i] = halfBand(d8_, buf + 2 * i);
            n = 4;
        }
        if (n >= 4) {
            for (int i = 0; i < 2; ++i) buf[i] = halfBand(d4_, buf + 2 * i);
            n = 2;
        }
        return halfBand(d2_, buf);
    }

private:
    int factor_ = 4;
    hiir::Upsampler2xFpu<10> u2_;
    hiir::Upsampler2xFpu<5> u4_;
    hiir::Upsampler2xFpu<3> u8_;
    hiir::Downsampler2xFpu<10> d2_;
    hiir::Downsampler2xFpu<5> d4_;
    hiir::Downsampler2xFpu<3> d8_;
};

// Local rate lift inside an already-oversampled synthesis domain up to `targetRate`
// (default 16x; can be lowered to 8x when paired with ADAA2).
class LocalOversampler {
public:
    static int localFactor(int domainFactor, int targetRate = kMaxOversampling) noexcept HBA_NONBLOCKING {
        return domainFactor >= 2 && domainFactor < targetRate ? targetRate / domainFactor : 1;
    }

    void prepare(int domainFactor, int targetRate = kMaxOversampling) {
        domain_ = domainFactor;
        factor_ = localFactor(domainFactor, targetRate);
        up4_.set_coefs(decimator_coefs::k4.data());
        up8_.set_coefs(decimator_coefs::k8.data());
        up16_.set_coefs(decimator_coefs::k16.data());
        down4_.set_coefs(decimator_coefs::k4.data());
        down8_.set_coefs(decimator_coefs::k8.data());
        down16_.set_coefs(decimator_coefs::k16.data());
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
        up4_.clear_buffers();
        up8_.clear_buffers();
        up16_.clear_buffers();
        down4_.clear_buffers();
        down8_.clear_buffers();
        down16_.clear_buffers();
    }
    int factor() const noexcept HBA_NONBLOCKING { return factor_; }

    void up(float x, float* out) noexcept HBA_NONBLOCKING {
        out[0] = x;
        int n = 1;
        int rate = domain_;
        while (n < factor_) {
            rate *= 2;
            std::array<float, kMaxOversampling> in {};
            for (int i = 0; i < n; ++i) in[static_cast<std::size_t>(i)] = out[i];
            for (int i = 0; i < n; ++i) {
                const float v = in[static_cast<std::size_t>(i)];
                if (rate == 4) halfBandUp(up4_, v, out + 2 * i);
                else if (rate == 8) halfBandUp(up8_, v, out + 2 * i);
                else halfBandUp(up16_, v, out + 2 * i);
            }
            n *= 2;
        }
    }

    float down(float* in) noexcept HBA_NONBLOCKING {
        int n = factor_;
        int rate = domain_ * factor_;
        while (n > 1) {
            for (int i = 0; i < n / 2; ++i) {
                if (rate == 4) in[i] = halfBand(down4_, in + 2 * i);
                else if (rate == 8) in[i] = halfBand(down8_, in + 2 * i);
                else in[i] = halfBand(down16_, in + 2 * i);
            }
            n /= 2;
            rate /= 2;
        }
        return in[0];
    }

private:
    int domain_ = 1, factor_ = 1;
    hiir::Upsampler2xFpu<5> up4_;
    hiir::Upsampler2xFpu<3> up8_;
    hiir::Upsampler2xFpu<2> up16_;
    hiir::Downsampler2xFpu<5> down4_;
    hiir::Downsampler2xFpu<3> down8_;
    hiir::Downsampler2xFpu<2> down16_;
};

}  // namespace hba::dsp
