// Decimation by 1, 2, 4, 8 or 16 with a cascade of HIIR polyphase IIR
// half-band stages. Each stage is designed for its place in the cascade: the
// last one (2x -> 1x) has the steep transition band, the earlier ones only have
// to keep their images out of the final audio band, so they are much cheaper.
//
// Coefficients from hiir::PolyphaseIir2Designer::compute_coefs (HIIR 1.50):
//   stage      transition (rel. to its input rate)   coefs   stop-band
//   2x -> 1x   0.0465  (pass 20 kHz at 44.1 kHz)      10      130.0 dB
//   4x -> 2x   0.25                                     5      144.1 dB
//   8x -> 4x   0.375                                    3      134.3 dB
//  16x -> 8x   0.4375                                   2      124.7 dB
#pragma once

#include "tf/nonblocking.h"

#include <array>

#include "hiir/Downsampler2xFpu.h"
#include "hiir/Upsampler2xFpu.h"

namespace tf::dsp {

namespace decimator_coefs {
inline constexpr std::array<double, 10> k2 { 0.024685829054447571, 0.094249802591679088, 0.19694469585846766,
                                             0.31791855629187493,  0.44353126463368947,  0.56409762124863516,
                                             0.67467859072325931,  0.77455265141382068,  0.86624354158326644,
                                             0.95475553917103384 };
inline constexpr std::array<double, 5> k4 { 0.028414445279188464, 0.11393344727958085, 0.25906131103886926,
                                            0.4738722544857808, 0.78701940115286972 };
inline constexpr std::array<double, 3> k8 { 0.055907353696531173, 0.24352963280437187, 0.64715311429020916 };
inline constexpr std::array<double, 2> k16 { 0.10723890054318994, 0.53103059594468105 };
}

// HIIR's process_sample() contains assert(), which is a no-op in release builds
// and a call to __assert_rtn() in debug builds. That call is the only thing the
// effect analysis cannot see through, so it is suppressed here, for this
// single audited call and nothing else.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
template <class Stage>
inline float halfBand(Stage& stage, const float* in) noexcept TF_NONBLOCKING {
    return stage.process_sample(in);
}
template <class Stage>
inline void halfBandUp(Stage& stage, float in, float* out) noexcept TF_NONBLOCKING {
    stage.process_sample(out[0], out[1], in);
}
#pragma clang diagnostic pop

inline constexpr int kMaxOversampling = 16;

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
    void reset() noexcept TF_NONBLOCKING {
        s2_.clear_buffers();
        s4_.clear_buffers();
        s8_.clear_buffers();
        s16_.clear_buffers();
    }
    int factor() const noexcept TF_NONBLOCKING { return factor_; }

    // Takes `factor` oversampled samples, returns one base-rate sample.
    // `in` is used as scratch space.
    float process(float* in) noexcept TF_NONBLOCKING {
        int n = factor_;
        if (n >= 16) n = halve(s16_, in, n);
        if (n >= 8) n = halve(s8_, in, n);
        if (n >= 4) n = halve(s4_, in, n);
        if (n >= 2) n = halve(s2_, in, n);
        return in[0];
    }

private:
    template <class Stage>
    static int halve(Stage& stage, float* buf, int n) noexcept TF_NONBLOCKING {
        for (int i = 0; i < n / 2; ++i) buf[i] = halfBand(stage, buf + 2 * i);
        return n / 2;
    }

    int factor_ = 1;
    hiir::Downsampler2xFpu<10> s2_;
    hiir::Downsampler2xFpu<5> s4_;
    hiir::Downsampler2xFpu<3> s8_;
    hiir::Downsampler2xFpu<2> s16_;
};

// 2x around a base-rate nonlinearity (the growl clipper), with the decimator's
// steep last-stage design both ways, so images and aliases stay out of the
// audio band and the clipped result is band-limited before it leaves.
class BaseRate2x {
public:
    static constexpr int kFactor = 2;
    void prepare() {
        up_.set_coefs(decimator_coefs::k2.data());
        down_.set_coefs(decimator_coefs::k2.data());
        reset();
    }
    void reset() noexcept TF_NONBLOCKING {
        up_.clear_buffers();
        down_.clear_buffers();
    }
    void up(float x, float* out) noexcept TF_NONBLOCKING { halfBandUp(up_, x, out); }
    float down(const float* in) noexcept TF_NONBLOCKING { return halfBand(down_, in); }

private:
    hiir::Upsampler2xFpu<10> up_;
    hiir::Downsampler2xFpu<10> down_;
};

// The fold needs about 16x to keep its aliasing 70 dB down at full drive, more
// than the rest of the oversampled domain. LocalOversampler lifts one signal
// from the domain's rate (factor M) to 16x and back around the fold, using the
// same per-rate half-band designs as the decimator (they only have to protect
// the final audio band). Every signal that must stay sample-aligned with the
// folded one (the master sine for the sub) goes through an identical instance.
class LocalOversampler {
public:
    static int localFactor(int domainFactor) noexcept TF_NONBLOCKING {
        return domainFactor >= 2 && domainFactor < kMaxOversampling ? kMaxOversampling / domainFactor : 1;
    }

    void prepare(int domainFactor) {
        domain_ = domainFactor;
        factor_ = localFactor(domainFactor);
        up4_.set_coefs(decimator_coefs::k4.data());
        up8_.set_coefs(decimator_coefs::k8.data());
        up16_.set_coefs(decimator_coefs::k16.data());
        down4_.set_coefs(decimator_coefs::k4.data());
        down8_.set_coefs(decimator_coefs::k8.data());
        down16_.set_coefs(decimator_coefs::k16.data());
        reset();
    }
    void reset() noexcept TF_NONBLOCKING {
        up4_.clear_buffers();
        up8_.clear_buffers();
        up16_.clear_buffers();
        down4_.clear_buffers();
        down8_.clear_buffers();
        down16_.clear_buffers();
    }
    int factor() const noexcept TF_NONBLOCKING { return factor_; }

    // One domain-rate sample in, factor() local-rate samples out.
    void up(float x, float* out) noexcept TF_NONBLOCKING {
        out[0] = x;
        int n = 1;
        int rate = domain_;  // relative to the base rate
        while (n < factor_) {
            rate *= 2;
            std::array<float, kMaxOversampling> in {};
            for (int i = 0; i < n; ++i) in[static_cast<std::size_t>(i)] = out[i];
            for (int i = 0; i < n; ++i) {  // in time order: the stages are recursive
                const float v = in[static_cast<std::size_t>(i)];
                if (rate == 4) halfBandUp(up4_, v, out + 2 * i);
                else if (rate == 8) halfBandUp(up8_, v, out + 2 * i);
                else halfBandUp(up16_, v, out + 2 * i);
            }
            n *= 2;
        }
    }

    // factor() local-rate samples in (used as scratch), one domain-rate sample out.
    float down(float* in) noexcept TF_NONBLOCKING {
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

}
