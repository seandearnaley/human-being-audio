// SPDX-License-Identifier: GPL-3.0-or-later
// Shared lock-free multi-channel audio capture ring + stereo goniometer + Hann RMS log spectrum.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace hba::host {

template <int NumChannels, int QueueSize, int HistorySize, int FftOrder, int SpectrumBands> class StereoAnalyzerBase {
    static_assert(NumChannels >= 1, "NumChannels must be >= 1");
    static_assert(QueueSize >= 1, "QueueSize must be >= 1");
    static_assert(HistorySize >= 1 && (HistorySize & (HistorySize - 1)) == 0, "HistorySize must be a power of 2");
    static_assert(FftOrder >= 2, "FftOrder must be >= 2");
    static_assert(SpectrumBands >= 1, "SpectrumBands must be >= 1");

  public:
    static constexpr int kNumChannels = NumChannels;
    static constexpr int kQueueSize = QueueSize;
    static constexpr int kHistorySize = HistorySize;
    static constexpr int kFftOrder = FftOrder;
    static constexpr int kFftSize = 1 << FftOrder;
    static constexpr int kSpectrumBands = SpectrumBands;
    static constexpr float kFloorDb = -100.0f;
    static constexpr float kMeterFallDbPerSecond = 30.0f;
    static constexpr float kSpectrumFallDbPerSecond = 40.0f;

    StereoAnalyzerBase()
        : queue_(kQueueSize), fft_(kFftOrder),
          window_(static_cast<std::size_t>(kFftSize), juce::dsp::WindowingFunction<float>::hann) {
        for (auto& ch : queued_)
            ch.assign(static_cast<std::size_t>(kQueueSize), 0.0f);
        for (auto& ch : history_)
            ch.assign(static_cast<std::size_t>(kHistorySize), 0.0f);
        fftBufferL_.assign(static_cast<std::size_t>(kFftSize) * 2u, 0.0f);
        fftBufferR_.assign(static_cast<std::size_t>(kFftSize) * 2u, 0.0f);
        spectrum_.fill(kFloorDb);
        for (int idx = 0; idx <= kSpectrumBands; ++idx) {
            bandEdgeHz_[static_cast<std::size_t>(idx)] =
                20.0 * std::pow(1000.0, static_cast<double>(idx) / static_cast<double>(kSpectrumBands));
        }
    }

    static float linearToDb(float linear) noexcept { return linear > 1.0e-5f ? 20.0f * std::log10(linear) : kFloorDb; }

    // Audio thread: lock-free copy of `NumChannels` pointers into the FIFO (null channels fill with 0).
    void pushChannels(const std::array<const float*, NumChannels>& sources, int numSamples, int fallbackSumChannel = -1,
                      int sumLeftCh = 0, int sumRightCh = 1) noexcept {
        const auto scope = queue_.write(std::min(numSamples, queue_.getFreeSpace()));
        const bool canSum = sumLeftCh >= 0 && sumLeftCh < NumChannels && sumRightCh >= 0 && sumRightCh < NumChannels;
        for (int ch = 0; ch < NumChannels; ++ch) {
            auto* dst = queued_[static_cast<std::size_t>(ch)].data();
            const float* src = sources[static_cast<std::size_t>(ch)];
            if (src == nullptr) {
                const float* l =
                    (ch == fallbackSumChannel && canSum) ? sources[static_cast<std::size_t>(sumLeftCh)] : nullptr;
                const float* r =
                    (ch == fallbackSumChannel && canSum) ? sources[static_cast<std::size_t>(sumRightCh)] : nullptr;
                if (l != nullptr && r != nullptr) {
                    for (int i = 0; i < scope.blockSize1; ++i)
                        dst[scope.startIndex1 + i] = 0.5f * (l[i] + r[i]);
                    for (int i = 0; i < scope.blockSize2; ++i)
                        dst[scope.startIndex2 + i] = 0.5f * (l[scope.blockSize1 + i] + r[scope.blockSize1 + i]);
                } else {
                    std::fill_n(dst + scope.startIndex1, scope.blockSize1, 0.0f);
                    std::fill_n(dst + scope.startIndex2, scope.blockSize2, 0.0f);
                }
            } else {
                std::copy_n(src, scope.blockSize1, dst + scope.startIndex1);
                std::copy_n(src + scope.blockSize1, scope.blockSize2, dst + scope.startIndex2);
            }
        }
    }

    void drainQueue(int leftCh, int rightCh, float& peakLeft, float& peakRight) {
        const auto scope = queue_.read(queue_.getNumReady());
        const auto take = [&](int start, int count) {
            for (int i = 0; i < count; ++i) {
                const auto from = static_cast<std::size_t>(start + i);
                const auto to = static_cast<std::size_t>(historyWrite_);
                for (std::size_t ch = 0; ch < static_cast<std::size_t>(NumChannels); ++ch)
                    history_[ch][to] = queued_[ch][from];
                peakLeft = std::max(peakLeft, std::abs(history_[static_cast<std::size_t>(leftCh)][to]));
                peakRight = std::max(peakRight, std::abs(history_[static_cast<std::size_t>(rightCh)][to]));
                historyWrite_ = (historyWrite_ + 1) & (kHistorySize - 1);
            }
        };
        take(scope.startIndex1, scope.blockSize1);
        take(scope.startIndex2, scope.blockSize2);
    }

    float history(int channel, int samplesAgo) const noexcept {
        const int idx = (historyWrite_ - 1 - samplesAgo) & (kHistorySize - 1);
        return history_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(idx)];
    }

    template <std::size_t GonioPoints>
    void computeStereo(int leftCh, int rightCh, std::array<float, GonioPoints>& gonioL,
                       std::array<float, GonioPoints>& gonioR, float& correlationOut) const {
        constexpr int stride = 8;
        for (int i = 0; i < static_cast<int>(GonioPoints); ++i) {
            const int ago = (static_cast<int>(GonioPoints) - 1 - i) * stride;
            gonioL[static_cast<std::size_t>(i)] = history(leftCh, ago);
            gonioR[static_cast<std::size_t>(i)] = history(rightCh, ago);
        }
        double sumLR = 0.0, sumL = 0.0, sumR = 0.0;
        const int corrSamples = std::min(4096, kHistorySize);
        for (int ago = 0; ago < corrSamples; ++ago) {
            const double l = history(leftCh, ago);
            const double r = history(rightCh, ago);
            sumLR += l * r;
            sumL += l * l;
            sumR += r * r;
        }
        const double energy = std::sqrt(sumL * sumR);
        correlationOut = energy > 1.0e-9 ? static_cast<float>(sumLR / energy) : 0.0f;
    }

    void computeSpectrum(int leftCh, int rightCh, double sampleRate, float seconds,
                         std::array<float, static_cast<std::size_t>(SpectrumBands)>& outBandsDb) {
        for (int i = 0; i < kFftSize; ++i) {
            const int ago = kFftSize - 1 - i;
            fftBufferL_[static_cast<std::size_t>(i)] = history(leftCh, ago);
            fftBufferR_[static_cast<std::size_t>(i)] = history(rightCh, ago);
        }
        std::fill(fftBufferL_.begin() + kFftSize, fftBufferL_.end(), 0.0f);
        std::fill(fftBufferR_.begin() + kFftSize, fftBufferR_.end(), 0.0f);
        window_.multiplyWithWindowingTable(fftBufferL_.data(), static_cast<std::size_t>(kFftSize));
        window_.multiplyWithWindowingTable(fftBufferR_.data(), static_cast<std::size_t>(kFftSize));
        fft_.performFrequencyOnlyForwardTransform(fftBufferL_.data());
        fft_.performFrequencyOnlyForwardTransform(fftBufferR_.data());

        constexpr float scale = 4.0f / static_cast<float>(kFftSize);
        const double safeSr = sampleRate > 1000.0 ? sampleRate : 48000.0;
        const double binHz = safeSr / static_cast<double>(kFftSize);
        const int lastBin = kFftSize / 2 - 1;
        const float fall = kSpectrumFallDbPerSecond * seconds;

        const auto binMag = [&](int bin) -> float {
            const float l = fftBufferL_[static_cast<std::size_t>(bin)];
            const float r = fftBufferR_[static_cast<std::size_t>(bin)];
            return std::sqrt(0.5f * (l * l + r * r));
        };

        const double invBinHz = 1.0 / binHz;
        for (int band = 0; band < SpectrumBands; ++band) {
            const double low = bandEdgeHz_[static_cast<std::size_t>(band)] * invBinHz;
            const double high = bandEdgeHz_[static_cast<std::size_t>(band + 1)] * invBinHz;
            float magnitude = 0.0f;
            if (high - low < 1.0) {
                const double centre = 0.5 * (low + high);
                const int bin = std::clamp(static_cast<int>(centre), 0, std::max(0, lastBin - 1));
                const auto fraction = static_cast<float>(std::clamp(centre - static_cast<double>(bin), 0.0, 1.0));
                magnitude = binMag(bin) * (1.0f - fraction) + binMag(bin + 1) * fraction;
            } else {
                const int first = std::clamp(static_cast<int>(low), 0, lastBin);
                const int last = std::clamp(static_cast<int>(high), first, lastBin);
                for (int bin = first; bin <= last; ++bin)
                    magnitude = std::max(magnitude, binMag(bin));
            }
            const auto idx = static_cast<std::size_t>(band);
            spectrum_[idx] = std::max(linearToDb(magnitude * scale), spectrum_[idx] - fall);
            outBandsDb[idx] = spectrum_[idx];
        }
    }

    void updatePeakMeters(float peakLeft, float peakRight, float seconds, float& outLeftDb,
                          float& outRightDb) noexcept {
        const float fall = kMeterFallDbPerSecond * seconds;
        peakLeftDb_ = std::max(linearToDb(peakLeft), peakLeftDb_ - fall);
        peakRightDb_ = std::max(linearToDb(peakRight), peakRightDb_ - fall);
        outLeftDb = peakLeftDb_;
        outRightDb = peakRightDb_;
    }

  private:
    juce::AbstractFifo queue_;
    std::array<std::vector<float>, static_cast<std::size_t>(NumChannels)> queued_;
    std::array<std::vector<float>, static_cast<std::size_t>(NumChannels)> history_;
    int historyWrite_ = 0;

    juce::dsp::FFT fft_;
    juce::dsp::WindowingFunction<float> window_;
    std::vector<float> fftBufferL_;
    std::vector<float> fftBufferR_;
    std::array<double, static_cast<std::size_t>(SpectrumBands) + 1u> bandEdgeHz_{};
    std::array<float, static_cast<std::size_t>(SpectrumBands)> spectrum_{};
    float peakLeftDb_ = kFloorDb;
    float peakRightDb_ = kFloorDb;
};

} // namespace hba::host
