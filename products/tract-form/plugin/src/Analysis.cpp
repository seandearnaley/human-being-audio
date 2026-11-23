#include "Analysis.h"

#include <algorithm>
#include <cmath>

namespace tf {

namespace {
constexpr float kFloorDb = -100.0f;
constexpr float kMeterFallDbPerSecond = 30.0f;
constexpr float kSpectrumFallDbPerSecond = 40.0f;

float toDb(float linear) { return linear > 1.0e-5f ? 20.0f * std::log10(linear) : kFloorDb; }
}

Analysis::Analysis() {
    for (auto& channel : queued_) channel.assign(kQueueSize, 0.0f);
    for (auto& channel : history_) channel.assign(kHistorySize, 0.0f);
    fftBuffer_.assign(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
    spectrum_.fill(kFloorDb);
}

void Analysis::push(const float* masterIn, const float* growlIn, const float* leftIn, const float* rightIn,
                    int numSamples) noexcept {
    const std::array<const float*, numChannels> sources { masterIn, growlIn, leftIn, rightIn };
    const auto scope = queue_.write(std::min(numSamples, queue_.getFreeSpace()));
    for (int channel = 0; channel < numChannels; ++channel) {
        auto* destination = queued_[static_cast<std::size_t>(channel)].data();
        const float* source = sources[static_cast<std::size_t>(channel)];
        if (source == nullptr) {
            std::fill_n(destination + scope.startIndex1, scope.blockSize1, 0.0f);
            std::fill_n(destination + scope.startIndex2, scope.blockSize2, 0.0f);
        } else {
            std::copy_n(source, scope.blockSize1, destination + scope.startIndex1);
            std::copy_n(source + scope.blockSize1, scope.blockSize2, destination + scope.startIndex2);
        }
    }
}

void Analysis::setStatus(const Status& status) noexcept {
    hz_.store(status.hz, std::memory_order_relaxed);
    note_.store(status.note, std::memory_order_relaxed);
    gate_.store(status.gate, std::memory_order_relaxed);
    gainReductionDb_.store(status.gainReductionDb, std::memory_order_relaxed);
    ppq_.store(status.ppq, std::memory_order_relaxed);
    for (std::size_t i = 0; i < kLaneCount; ++i) {
        lanePhase_[i].store(status.lanePhase[i], std::memory_order_relaxed);
        laneValue_[i].store(status.laneValue[i], std::memory_order_relaxed);
    }
    effCoupling_.store(status.effective.coupling, std::memory_order_relaxed);
    effBeat_.store(status.effective.beat, std::memory_order_relaxed);
    effReach_.store(status.effective.reach, std::memory_order_relaxed);
    effMorph_.store(status.effective.morph, std::memory_order_relaxed);
    effDrive_.store(status.effective.drive, std::memory_order_relaxed);
    effSculpt_.store(status.effective.sculpt, std::memory_order_relaxed);
    effWidth_.store(status.effective.width, std::memory_order_relaxed);
    effActive_.store(status.effective.active, std::memory_order_relaxed);
}

void Analysis::drainQueue(float& peakLeft, float& peakRight) {
    const auto scope = queue_.read(queue_.getNumReady());
    const auto take = [&](int start, int count) {
        for (int i = 0; i < count; ++i) {
            const auto from = static_cast<std::size_t>(start + i);
            const auto to = static_cast<std::size_t>(historyWrite_);
            for (std::size_t channel = 0; channel < numChannels; ++channel) history_[channel][to] = queued_[channel][from];
            peakLeft = std::max(peakLeft, std::abs(history_[left][to]));
            peakRight = std::max(peakRight, std::abs(history_[right][to]));
            historyWrite_ = (historyWrite_ + 1) & (kHistorySize - 1);
        }
    };
    take(scope.startIndex1, scope.blockSize1);
    take(scope.startIndex2, scope.blockSize2);
}

float Analysis::history(Channel channel, int samplesAgo) const {
    const int index = (historyWrite_ - 1 - samplesAgo) & (kHistorySize - 1);
    return history_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(index)];
}

// Two periods of the master sine, starting at its most recent rising zero
// crossing that still leaves two full periods of history after it.
void Analysis::updateScope(ui::Visuals& visuals, double sampleRate, float hz) const {
    const double period = hz > 10.0f ? sampleRate / static_cast<double>(hz) : 0.0;
    const double span = 2.0 * period;
    if (period <= 0.0 || span + period + 2.0 >= kHistorySize) {
        visuals.scopeMaster.fill(0.0f);
        visuals.scopeGrowl.fill(0.0f);
        visuals.scopeOut.fill(0.0f);
        return;
    }

    // Search one period back from the latest possible start for a rising crossing.
    const int latestStart = static_cast<int>(std::ceil(span)) + 1;
    double startAgo = latestStart;
    for (int ago = latestStart; ago < latestStart + static_cast<int>(period) + 1; ++ago) {
        const float before = history(master, ago);
        const float after = history(master, ago - 1);
        if (before <= 0.0f && after > 0.0f) {
            startAgo = static_cast<double>(ago) - static_cast<double>(before / (before - after));
            break;
        }
    }

    const auto read = [&](Channel channel, double ago) {
        const int whole = static_cast<int>(std::floor(ago));
        const auto fraction = static_cast<float>(ago - whole);
        return history(channel, whole) * (1.0f - fraction) + history(channel, whole + 1) * fraction;
    };
    for (int i = 0; i < ui::Visuals::kScopePoints; ++i) {
        const double ago = std::max(0.0, startAgo - span * i / ui::Visuals::kScopePoints);
        const auto index = static_cast<std::size_t>(i);
        visuals.scopeMaster[index] = read(master, ago);
        visuals.scopeGrowl[index] = read(growl, ago);
        visuals.scopeOut[index] = 0.5f * (read(left, ago) + read(right, ago));
    }
}

void Analysis::updateStereo(ui::Visuals& visuals) const {
    constexpr int stride = 8;
    for (int i = 0; i < ui::Visuals::kGonioPoints; ++i) {
        const int ago = (ui::Visuals::kGonioPoints - 1 - i) * stride;
        visuals.gonioLeft[static_cast<std::size_t>(i)] = history(left, ago);
        visuals.gonioRight[static_cast<std::size_t>(i)] = history(right, ago);
    }

    double sumLeftRight = 0.0, sumLeft = 0.0, sumRight = 0.0;
    for (int ago = 0; ago < 4096; ++ago) {
        const double l = history(left, ago);
        const double r = history(right, ago);
        sumLeftRight += l * r;
        sumLeft += l * l;
        sumRight += r * r;
    }
    const double energy = std::sqrt(sumLeft * sumRight);
    visuals.correlation = energy > 1.0e-9 ? static_cast<float>(sumLeftRight / energy) : 0.0f;
}

void Analysis::updateSpectrum(ui::Visuals& visuals, double sampleRate, float seconds) {
    for (int i = 0; i < kFftSize; ++i) {
        const int ago = kFftSize - 1 - i;
        fftBuffer_[static_cast<std::size_t>(i)] = 0.5f * (history(left, ago) + history(right, ago));
    }
    std::fill(fftBuffer_.begin() + kFftSize, fftBuffer_.end(), 0.0f);
    window_.multiplyWithWindowingTable(fftBuffer_.data(), static_cast<size_t>(kFftSize));
    fft_.performFrequencyOnlyForwardTransform(fftBuffer_.data());

    // A full-scale sine reads 0 dB: divide by half the window's coherent gain.
    constexpr float scale = 4.0f / kFftSize;
    const double safeSr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const double binHz = safeSr / kFftSize;
    const int lastBin = kFftSize / 2 - 1;
    const float fall = kSpectrumFallDbPerSecond * seconds;

    for (int band = 0; band < ui::Visuals::kSpectrumBands; ++band) {
        const auto edge = [&](int index) {
            return 20.0 * std::pow(1000.0, static_cast<double>(index) / ui::Visuals::kSpectrumBands) / binHz;
        };
        const double low = edge(band);
        const double high = edge(band + 1);
        float magnitude;
        if (high - low < 1.0) {
            // Narrower than a bin: interpolate so the low end is a curve, not steps.
            const double centre = 0.5 * (low + high);
            const int bin = std::clamp(static_cast<int>(centre), 0, lastBin - 1);
            const auto fraction = static_cast<float>(std::clamp(centre - bin, 0.0, 1.0));
            magnitude = fftBuffer_[static_cast<std::size_t>(bin)] * (1.0f - fraction)
                      + fftBuffer_[static_cast<std::size_t>(bin + 1)] * fraction;
        } else {
            magnitude = 0.0f;
            const int first = std::clamp(static_cast<int>(low), 0, lastBin);
            const int last = std::clamp(static_cast<int>(high), first, lastBin);
            for (int bin = first; bin <= last; ++bin) magnitude = std::max(magnitude, fftBuffer_[static_cast<std::size_t>(bin)]);
        }
        const auto index = static_cast<std::size_t>(band);
        spectrum_[index] = std::max(toDb(magnitude * scale), spectrum_[index] - fall);
        visuals.spectrumDb[index] = spectrum_[index];
    }
}

void Analysis::update(ui::Visuals& visuals, float seconds) {
    float peakLeft = 0.0f, peakRight = 0.0f;
    drainQueue(peakLeft, peakRight);

    const double sampleRate = sampleRate_.load(std::memory_order_relaxed);
    const float hz = hz_.load(std::memory_order_relaxed);

    updateScope(visuals, sampleRate, hz);
    updateStereo(visuals);
    updateSpectrum(visuals, sampleRate, seconds);

    const float fall = kMeterFallDbPerSecond * seconds;
    peakLeftDb_ = std::max(toDb(peakLeft), peakLeftDb_ - fall);
    peakRightDb_ = std::max(toDb(peakRight), peakRightDb_ - fall);
    visuals.peakDbLeft = peakLeftDb_;
    visuals.peakDbRight = peakRightDb_;

    visuals.gainReductionDb = gainReductionDb_.load(std::memory_order_relaxed);
    visuals.ppq = ppq_.load(std::memory_order_relaxed);
    visuals.hz = hz;
    visuals.note = note_.load(std::memory_order_relaxed);
    visuals.gate = gate_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < kLaneCount; ++i) {
        visuals.lanePhase[i] = lanePhase_[i].load(std::memory_order_relaxed);
        visuals.laneValue[i] = laneValue_[i].load(std::memory_order_relaxed);
    }
    visuals.effCoupling = effCoupling_.load(std::memory_order_relaxed);
    visuals.effBeat = effBeat_.load(std::memory_order_relaxed);
    visuals.effReach = effReach_.load(std::memory_order_relaxed);
    visuals.effMorph = effMorph_.load(std::memory_order_relaxed);
    visuals.effDrive = effDrive_.load(std::memory_order_relaxed);
    visuals.effSculpt = effSculpt_.load(std::memory_order_relaxed);
    visuals.effWidth = effWidth_.load(std::memory_order_relaxed);
    visuals.effActive = effActive_.load(std::memory_order_relaxed);
}

}
