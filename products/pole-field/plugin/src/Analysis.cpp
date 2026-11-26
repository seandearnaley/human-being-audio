#include "Analysis.h"

#include <algorithm>

namespace pf {

Analysis::Analysis() { envHistory_.fill(0.0f); }

void Analysis::push(const float* leftOut, const float* rightOut, int numSamples) noexcept {
    push(nullptr, leftOut, rightOut, numSamples);
}

void Analysis::push(const float* preInMono, const float* leftOut, const float* rightOut, int numSamples) noexcept {
    core_.pushChannels({leftOut, rightOut, preInMono}, numSamples, preIn, left, right);
}

void Analysis::setStatus(const Status& status) noexcept {
    gainReductionDb_.store(status.gainReductionDb, std::memory_order_relaxed);
    ppq_.store(status.ppq, std::memory_order_relaxed);
    osFactor_.store(status.osFactor, std::memory_order_relaxed);
    for (std::size_t i = 0; i < kLaneCount; ++i) {
        lanePhase_[i].store(status.lanePhase[i], std::memory_order_relaxed);
        laneValue_[i].store(status.laneValue[i], std::memory_order_relaxed);
    }
    effMorphX_.store(status.effective.morphX, std::memory_order_relaxed);
    effMorphY_.store(status.effective.morphY, std::memory_order_relaxed);
    effMorphZ_.store(status.effective.morphZ, std::memory_order_relaxed);
    effFreq_.store(status.effective.freq, std::memory_order_relaxed);
    effRes_.store(status.effective.res, std::memory_order_relaxed);
    effDrive_.store(status.effective.drive, std::memory_order_relaxed);
    effTransform_.store(status.effective.transform, std::memory_order_relaxed);
    effSpread_.store(status.effective.spread, std::memory_order_relaxed);
    effFmAmount_.store(status.effective.fmAmount, std::memory_order_relaxed);
    envLevel_.store(status.effective.envLevel, std::memory_order_relaxed);
    midiShiftSt_.store(status.effective.midiShiftSt, std::memory_order_relaxed);
    activeNote_.store(status.effective.activeNote, std::memory_order_relaxed);
}

void Analysis::snapshotInput(std::vector<float>& destination, double& outSampleRate) {
    float dummyL = 0.0f, dummyR = 0.0f;
    core_.drainQueue(left, right, dummyL, dummyR);
    outSampleRate = sampleRate_.load(std::memory_order_relaxed);
    destination.resize(static_cast<std::size_t>(kHistorySize));
    for (int i = 0; i < kHistorySize; ++i)
        destination[static_cast<std::size_t>(i)] = core_.history(preIn, kHistorySize - 1 - i);
}

void Analysis::update(ui::Visuals& visuals, float seconds) {
    float peakLeft = 0.0f, peakRight = 0.0f;
    core_.drainQueue(left, right, peakLeft, peakRight);

    const double sampleRate = sampleRate_.load(std::memory_order_relaxed);
    core_.computeStereo(left, right, visuals.gonioLeft, visuals.gonioRight, visuals.correlation);
    core_.computeSpectrum(left, right, sampleRate, seconds, visuals.spectrumDb);
    core_.updatePeakMeters(peakLeft, peakRight, seconds, visuals.peakDbLeft, visuals.peakDbRight);

    visuals.sampleRate = sampleRate;
    visuals.osFactor = osFactor_.load(std::memory_order_relaxed);
    visuals.gainReductionDb = gainReductionDb_.load(std::memory_order_relaxed);
    visuals.ppq = ppq_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < kLaneCount; ++i) {
        visuals.lanePhase[i] = lanePhase_[i].load(std::memory_order_relaxed);
        visuals.laneValue[i] = laneValue_[i].load(std::memory_order_relaxed);
    }

    visuals.effMorphX = effMorphX_.load(std::memory_order_relaxed);
    visuals.effMorphY = effMorphY_.load(std::memory_order_relaxed);
    visuals.effMorphZ = effMorphZ_.load(std::memory_order_relaxed);
    visuals.effFreq = effFreq_.load(std::memory_order_relaxed);
    visuals.effRes = effRes_.load(std::memory_order_relaxed);
    visuals.effDrive = effDrive_.load(std::memory_order_relaxed);
    visuals.effTransform = effTransform_.load(std::memory_order_relaxed);
    visuals.effSpread = effSpread_.load(std::memory_order_relaxed);
    visuals.effFmAmount = effFmAmount_.load(std::memory_order_relaxed);
    visuals.envLevel = envLevel_.load(std::memory_order_relaxed);
    visuals.midiShiftSt = midiShiftSt_.load(std::memory_order_relaxed);
    visuals.activeNote = activeNote_.load(std::memory_order_relaxed);

    // Shift scrolling envelope history left by one frame.
    std::rotate(envHistory_.begin(), envHistory_.begin() + 1, envHistory_.end());
    envHistory_.back() = visuals.envLevel;
    visuals.envHistory = envHistory_;
}

} // namespace pf
