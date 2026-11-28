// SPDX-License-Identifier: GPL-3.0-or-later
// Zero-allocation, [[clang::nonblocking]]-safe radix-2 Real/Stereo FFT and
// 4x overlap-add (Hann^2 COLA) Stereo STFT processor for Human Being Audio.
#pragma once

#include <hba/dsp/control.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hba::dsp {

// Fixed-capacity radix-2 Cooley-Tukey FFT supporting power-of-2 sizes in [64, MaxN].
// All twiddle and bit-reversal tables are computed once in prepare(); forward/inverse
// transforms are strictly zero-allocation and [[clang::nonblocking]].
template <int MaxN = 2048> class RealFft {
    static_assert((MaxN & (MaxN - 1)) == 0 && MaxN >= 64 && MaxN <= 65536, "MaxN must be a power of 2 in [64, 65536]");

  public:
    static constexpr int kMaxSize = MaxN;
    static constexpr int kMaxBins = MaxN / 2 + 1;
    static constexpr int kDefaultSize = MaxN < 1024 ? MaxN : 1024;

    RealFft() noexcept HBA_NONBLOCKING { prepare(kDefaultSize); }

    void prepare(int fftSize) noexcept HBA_NONBLOCKING {
        int n = 64;
        while (n < fftSize && n < MaxN)
            n <<= 1;
        size_ = n;
        half_ = n / 2;
        invSize_ = 1.0f / static_cast<float>(n);

        int bits = 0;
        for (int tmp = n; tmp > 1; tmp >>= 1)
            ++bits;

        for (int i = 0; i < n; ++i) {
            int rev = 0;
            int x = i;
            for (int b = 0; b < bits; ++b) {
                rev = (rev << 1) | (x & 1);
                x >>= 1;
            }
            bitRev_[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(rev);
        }

        // Precompute twiddle factors exp(-j * 2*pi*k / N) for k = 0..N/2-1.
        for (int k = 0; k < half_; ++k) {
            const double angle = -kTwoPi * static_cast<double>(k) / static_cast<double>(n);
            twiddleRe_[static_cast<std::size_t>(k)] = static_cast<float>(std::cos(angle));
            twiddleIm_[static_cast<std::size_t>(k)] = static_cast<float>(std::sin(angle));
        }
    }

    int size() const noexcept HBA_NONBLOCKING { return size_; }
    int numBins() const noexcept HBA_NONBLOCKING { return half_ + 1; }

    // In-place complex FFT (inverse == false) or IFFT with 1/N scaling (inverse == true).
    void transformComplex(float* re, float* im, bool inverse) const noexcept HBA_NONBLOCKING {
        const int n = size_;
        for (int i = 0; i < n; ++i) {
            const int j = static_cast<int>(bitRev_[static_cast<std::size_t>(i)]);
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }

        const float imSign = inverse ? -1.0f : 1.0f;
        for (int len = 2; len <= n; len <<= 1) {
            const int halfLen = len >> 1;
            const int stride = n / len;
            for (int start = 0; start < n; start += len) {
                int twIdx = 0;
                for (int k = 0; k < halfLen; ++k) {
                    const float wr = twiddleRe_[static_cast<std::size_t>(twIdx)];
                    const float wi = imSign * twiddleIm_[static_cast<std::size_t>(twIdx)];
                    twIdx += stride;

                    const int even = start + k;
                    const int odd = even + halfLen;
                    const float orVal = re[odd];
                    const float oiVal = im[odd];
                    const float tr = wr * orVal - wi * oiVal;
                    const float ti = wr * oiVal + wi * orVal;

                    re[odd] = re[even] - tr;
                    im[odd] = im[even] - ti;
                    re[even] += tr;
                    im[even] += ti;
                }
            }
        }

        if (inverse) {
            const float scale = invSize_;
            for (int i = 0; i < n; ++i) {
                re[i] *= scale;
                im[i] *= scale;
            }
        }
    }

    // Mono real-to-complex forward FFT -> populates bins 0..N/2 (and full conjugate spectrum).
    void forwardReal(const float* timeIn, float* re, float* im) const noexcept HBA_NONBLOCKING {
        for (int i = 0; i < size_; ++i) {
            re[i] = timeIn[i];
            im[i] = 0.0f;
        }
        transformComplex(re, im, false);
    }

    // Mono complex-to-real inverse FFT reading bins 0..N/2 and enforcing Hermitian symmetry.
    void inverseReal(float* re, float* im, float* timeOut) const noexcept HBA_NONBLOCKING {
        im[0] = 0.0f;
        im[half_] = 0.0f;
        for (int k = 1; k < half_; ++k) {
            re[size_ - k] = re[k];
            im[size_ - k] = -im[k];
        }
        transformComplex(re, im, true);
        for (int i = 0; i < size_; ++i)
            timeOut[i] = re[i];
    }

    // 2-for-1 stereo real-to-complex forward FFT: packs L + jR into one complex FFT,
    // then unpacks exact half-spectra (0..N/2) for Left and Right.
    void forwardStereo(const float* timeL, const float* timeR, float* reL, float* imL, float* reR, float* imR,
                       float* scratchRe, float* scratchIm) const noexcept HBA_NONBLOCKING {
        for (int i = 0; i < size_; ++i) {
            scratchRe[i] = timeL[i];
            scratchIm[i] = timeR[i];
        }
        transformComplex(scratchRe, scratchIm, false);

        // DC and Nyquist bins are purely real for both real inputs.
        reL[0] = scratchRe[0];
        imL[0] = 0.0f;
        reR[0] = scratchIm[0];
        imR[0] = 0.0f;

        reL[half_] = scratchRe[half_];
        imL[half_] = 0.0f;
        reR[half_] = scratchIm[half_];
        imR[half_] = 0.0f;

        for (int k = 1; k < half_; ++k) {
            const int nk = size_ - k;
            const float zr1 = scratchRe[k];
            const float zi1 = scratchIm[k];
            const float zr2 = scratchRe[nk];
            const float zi2 = scratchIm[nk];

            reL[k] = 0.5f * (zr1 + zr2);
            imL[k] = 0.5f * (zi1 - zi2);
            reR[k] = 0.5f * (zi1 + zi2);
            imR[k] = 0.5f * (zr2 - zr1);
        }
    }

    // 2-for-1 stereo complex-to-real inverse FFT: packs modified half-spectra L[0..N/2]
    // and R[0..N/2] into Z = L + jR with exact Hermitian reconstruction and runs one IFFT.
    void inverseStereo(const float* reL, const float* imL, const float* reR, const float* imR, float* timeL,
                       float* timeR) const noexcept HBA_NONBLOCKING {
        timeL[0] = reL[0];
        timeR[0] = reR[0];
        timeL[half_] = reL[half_];
        timeR[half_] = reR[half_];

        for (int k = 1; k < half_; ++k) {
            const int nk = size_ - k;
            const float rl = reL[k];
            const float il = imL[k];
            const float rr = reR[k];
            const float ir = imR[k];

            timeL[k] = rl - ir;
            timeR[k] = il + rr;
            timeL[nk] = rl + ir;
            timeR[nk] = rr - il;
        }
        transformComplex(timeL, timeR, true);
    }

  private:
    int size_ = kDefaultSize;
    int half_ = kDefaultSize / 2;
    float invSize_ = 1.0f / static_cast<float>(kDefaultSize);
    std::array<std::uint16_t, MaxN> bitRev_{};
    std::array<float, MaxN / 2> twiddleRe_{};
    std::array<float, MaxN / 2> twiddleIm_{};
};

// 4x overlap-add (Hann analysis * Hann synthesis, COLA gain = 2/3) Stereo STFT
// with optional sample-aligned stereo Sidechain Carrier analysis.
// Exact end-to-end pass-through latency is strictly `fftSize` samples (unity < -110 dB).
template <int MaxN = 2048> class StereoStft {
  public:
    static constexpr int kMaxSize = MaxN;
    static constexpr int kMaxBins = MaxN / 2 + 1;
    static constexpr int kDefaultSize = MaxN < 1024 ? MaxN : 1024;

    StereoStft() noexcept HBA_NONBLOCKING { prepare(kDefaultSize); }

    struct FrameBuffers {
        float* reL = nullptr;
        float* imL = nullptr;
        float* reR = nullptr;
        float* imR = nullptr;
        const float* scReL = nullptr;
        const float* scImL = nullptr;
        const float* scReR = nullptr;
        const float* scImR = nullptr;
        int numBins = 0;
        int fftSize = 0;
        int hopSize = 0;
        bool hasSidechain = false;
    };

    void prepare(int fftSize) noexcept HBA_NONBLOCKING {
        fft_.prepare(fftSize);
        size_ = fft_.size();
        hop_ = size_ / 4;
        mask_ = size_ - 1;

        // Periodic Hann analysis & synthesis windows; with 4x overlap, sum(w^2) == 1.5 exactly.
        const double invN = 1.0 / static_cast<double>(size_);
        for (int i = 0; i < size_; ++i) {
            const float w = static_cast<float>(0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(i) * invN)));
            anaWin_[static_cast<std::size_t>(i)] = w;
            synWin_[static_cast<std::size_t>(i)] = w * (2.0f / 3.0f);
        }
        reset();
    }

    void reset() noexcept HBA_NONBLOCKING {
        inL_.fill(0.0f);
        inR_.fill(0.0f);
        scInL_.fill(0.0f);
        scInR_.fill(0.0f);
        outL_.fill(0.0f);
        outR_.fill(0.0f);
        pos_ = 0;
        hopCounter_ = 0;
    }

    int fftSize() const noexcept HBA_NONBLOCKING { return size_; }
    int hopSize() const noexcept HBA_NONBLOCKING { return hop_; }
    int latencySamples() const noexcept HBA_NONBLOCKING { return size_; }

    // Processes one stereo sample (and optional stereo sidechain sample).
    // Invokes `processFrame(FrameBuffers&)` every `hopSize()` samples.
    template <typename FrameFn>
    void processSample(float inL, float inR, float scL, float scR, bool analyzeSidechain, float& outL, float& outR,
                       FrameFn&& processFrame) noexcept HBA_NONBLOCKING {
        const auto p = static_cast<std::size_t>(pos_);
        outL = outL_[p];
        outR = outR_[p];
        outL_[p] = 0.0f;
        outR_[p] = 0.0f;

        inL_[p] = std::isfinite(inL) ? inL : 0.0f;
        inR_[p] = std::isfinite(inR) ? inR : 0.0f;
        scInL_[p] = std::isfinite(scL) ? scL : 0.0f;
        scInR_[p] = std::isfinite(scR) ? scR : 0.0f;

        pos_ = (pos_ + 1) & mask_;
        ++hopCounter_;

        if (hopCounter_ >= hop_) {
            hopCounter_ = 0;
            for (int i = 0; i < size_; ++i) {
                const auto idx = static_cast<std::size_t>((pos_ + i) & mask_);
                const float w = anaWin_[static_cast<std::size_t>(i)];
                timeL_[static_cast<std::size_t>(i)] = inL_[idx] * w;
                timeR_[static_cast<std::size_t>(i)] = inR_[idx] * w;
            }

            fft_.forwardStereo(timeL_.data(), timeR_.data(), reL_.data(), imL_.data(), reR_.data(), imR_.data(),
                               scratchRe_.data(), scratchIm_.data());

            if (analyzeSidechain) {
                for (int i = 0; i < size_; ++i) {
                    const auto idx = static_cast<std::size_t>((pos_ + i) & mask_);
                    const float w = anaWin_[static_cast<std::size_t>(i)];
                    timeL_[static_cast<std::size_t>(i)] = scInL_[idx] * w;
                    timeR_[static_cast<std::size_t>(i)] = scInR_[idx] * w;
                }
                fft_.forwardStereo(timeL_.data(), timeR_.data(), scReL_.data(), scImL_.data(), scReR_.data(),
                                   scImR_.data(), scratchRe_.data(), scratchIm_.data());
            }

            FrameBuffers fb;
            fb.reL = reL_.data();
            fb.imL = imL_.data();
            fb.reR = reR_.data();
            fb.imR = imR_.data();
            fb.scReL = analyzeSidechain ? scReL_.data() : nullptr;
            fb.scImL = analyzeSidechain ? scImL_.data() : nullptr;
            fb.scReR = analyzeSidechain ? scReR_.data() : nullptr;
            fb.scImR = analyzeSidechain ? scImR_.data() : nullptr;
            fb.numBins = fft_.numBins();
            fb.fftSize = size_;
            fb.hopSize = hop_;
            fb.hasSidechain = analyzeSidechain;

            processFrame(fb);

            fft_.inverseStereo(reL_.data(), imL_.data(), reR_.data(), imR_.data(), timeL_.data(), timeR_.data());

            for (int i = 0; i < size_; ++i) {
                const auto idx = static_cast<std::size_t>((pos_ + i) & mask_);
                const float w = synWin_[static_cast<std::size_t>(i)];
                outL_[idx] += timeL_[static_cast<std::size_t>(i)] * w;
                outR_[idx] += timeR_[static_cast<std::size_t>(i)] * w;
            }
        }
    }

  private:
    RealFft<MaxN> fft_{};
    int size_ = kDefaultSize;
    int hop_ = kDefaultSize / 4;
    int mask_ = kDefaultSize - 1;
    int pos_ = 0;
    int hopCounter_ = 0;

    std::array<float, MaxN> anaWin_{};
    std::array<float, MaxN> synWin_{};
    std::array<float, MaxN> inL_{};
    std::array<float, MaxN> inR_{};
    std::array<float, MaxN> scInL_{};
    std::array<float, MaxN> scInR_{};
    std::array<float, MaxN> outL_{};
    std::array<float, MaxN> outR_{};

    std::array<float, MaxN> timeL_{};
    std::array<float, MaxN> timeR_{};
    std::array<float, MaxN> scratchRe_{};
    std::array<float, MaxN> scratchIm_{};

    std::array<float, kMaxBins> reL_{};
    std::array<float, kMaxBins> imL_{};
    std::array<float, kMaxBins> reR_{};
    std::array<float, kMaxBins> imR_{};
    std::array<float, kMaxBins> scReL_{};
    std::array<float, kMaxBins> scImL_{};
    std::array<float, kMaxBins> scReR_{};
    std::array<float, kMaxBins> scImR_{};
};

} // namespace hba::dsp
