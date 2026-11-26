#include "Analysis.h"

#include <algorithm>
#include <cmath>

namespace tf {

void Analysis::push(const float* masterIn, const float* growlIn, const float* leftIn, const float* rightIn,
                    int numSamples) noexcept {
    core_.pushChannels({masterIn, growlIn, leftIn, rightIn}, numSamples);
}

void Analysis::setStatus(const Status& status) noexcept {
    hz_.store(status.hz, std::memory_order_relaxed);
    note_.store(status.note, std::memory_order_relaxed);
    gate_.store(status.gate, std::memory_order_relaxed);
    gainReductionDb_.store(status.gainReductionDb, std::memory_order_relaxed);
    ppq_.store(status.ppq, std::memory_order_relaxed);
    notePpq_.store(status.notePpq, std::memory_order_relaxed);
    freeSec_.store(status.freeSec, std::memory_order_relaxed);
    noteSec_.store(status.noteSec, std::memory_order_relaxed);
    for (std::size_t i = 0; i < kLaneCount; ++i) {
        lanePhase_[i].store(status.lanePhase[i], std::memory_order_relaxed);
        laneValue_[i].store(status.laneValue[i], std::memory_order_relaxed);
    }
    effCoupling_.store(status.effective.coupling, std::memory_order_relaxed);
    effBeat_.store(status.effective.beat, std::memory_order_relaxed);
    effReach_.store(status.effective.reach, std::memory_order_relaxed);
    effMorph_.store(status.effective.morph, std::memory_order_relaxed);
    effShift_.store(status.effective.shift, std::memory_order_relaxed);
    effDrive_.store(status.effective.drive, std::memory_order_relaxed);
    effSculpt_.store(status.effective.sculpt, std::memory_order_relaxed);
    effWidth_.store(status.effective.width, std::memory_order_relaxed);
    effActive_.store(status.effective.active, std::memory_order_relaxed);
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

    const int latestStart = static_cast<int>(std::ceil(span)) + 1;
    double startAgo = latestStart;
    for (int ago = latestStart; ago < latestStart + static_cast<int>(period) + 1; ++ago) {
        const float before = core_.history(master, ago);
        const float after = core_.history(master, ago - 1);
        if (before <= 0.0f && after > 0.0f) {
            startAgo = static_cast<double>(ago) - static_cast<double>(before / (before - after));
            break;
        }
    }

    const auto read = [&](Channel channel, double ago) {
        const int whole = static_cast<int>(std::floor(ago));
        const auto fraction = static_cast<float>(ago - whole);
        return core_.history(channel, whole) * (1.0f - fraction) + core_.history(channel, whole + 1) * fraction;
    };
    for (int i = 0; i < ui::Visuals::kScopePoints; ++i) {
        const double ago = std::max(0.0, startAgo - span * i / ui::Visuals::kScopePoints);
        const auto index = static_cast<std::size_t>(i);
        visuals.scopeMaster[index] = read(master, ago);
        visuals.scopeGrowl[index] = read(growl, ago);
        visuals.scopeOut[index] = 0.5f * (read(left, ago) + read(right, ago));
    }
}

void Analysis::update(ui::Visuals& visuals, float seconds) {
    float peakLeft = 0.0f, peakRight = 0.0f;
    core_.drainQueue(left, right, peakLeft, peakRight);

    const double sampleRate = sampleRate_.load(std::memory_order_relaxed);
    const float hz = hz_.load(std::memory_order_relaxed);

    updateScope(visuals, sampleRate, hz);
    core_.computeStereo(left, right, visuals.gonioLeft, visuals.gonioRight, visuals.correlation);
    core_.computeSpectrum(left, right, sampleRate, seconds, visuals.spectrumDb);
    core_.updatePeakMeters(peakLeft, peakRight, seconds, visuals.peakDbLeft, visuals.peakDbRight);

    visuals.gainReductionDb = gainReductionDb_.load(std::memory_order_relaxed);
    visuals.ppq = ppq_.load(std::memory_order_relaxed);
    visuals.notePpq = notePpq_.load(std::memory_order_relaxed);
    visuals.freeSec = freeSec_.load(std::memory_order_relaxed);
    visuals.noteSec = noteSec_.load(std::memory_order_relaxed);
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
    visuals.effShift = effShift_.load(std::memory_order_relaxed);
    visuals.effDrive = effDrive_.load(std::memory_order_relaxed);
    visuals.effSculpt = effSculpt_.load(std::memory_order_relaxed);
    visuals.effWidth = effWidth_.load(std::memory_order_relaxed);
    visuals.effActive = effActive_.load(std::memory_order_relaxed);
}

} // namespace tf
