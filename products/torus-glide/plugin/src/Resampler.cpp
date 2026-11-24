// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Resampler.h"

namespace tg {

namespace {
juce::String noteName(int note) {
    static const char* const names[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}
} // namespace

juce::File Resampler::directory() {
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Human Being Audio")
        .getChildFile("Torus Glide")
        .getChildFile("Resamples");
}

Resampler::Resampler() : juce::Thread("Torus Glide resample") {}

Resampler::~Resampler() { stopThread(10000); }

void Resampler::start(const RenderRequest& request, const juce::String& presetName) {
    if (isThreadRunning())
        return;

    const auto safe = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Torus Glide") : presetName);
    const auto name = safe + " " + noteName(request.note) + " " + juce::String(juce::roundToInt(request.bpm)) + "bpm " +
                      juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav";
    {
        const juce::ScopedLock scoped(lock_);
        request_ = request;
        request_.oversampling = 8;
        target_ = directory().getChildFile(name);
    }
    state_.store(ui::ResampleState::rendering, std::memory_order_release);
    startThread();
}

void Resampler::invalidate() noexcept {
    ui::ResampleState expected = ui::ResampleState::ready;
    state_.compare_exchange_strong(expected, ui::ResampleState::idle, std::memory_order_acq_rel);
}

juce::File Resampler::file() const {
    if (state_.load(std::memory_order_acquire) != ui::ResampleState::ready)
        return {};
    const juce::ScopedLock scoped(lock_);
    return target_;
}

void Resampler::run() {
    RenderRequest req;
    juce::File target;
    {
        const juce::ScopedLock scoped(lock_);
        req = request_;
        target = target_;
    }
    const auto audio = renderOfflineLoop(req);
    const bool ok = target.getParentDirectory().createDirectory() &&
                    writeWavFloat(target.getFullPathName().toStdString(), audio, juce::roundToInt(req.sampleRate));
    state_.store(ok ? ui::ResampleState::ready : ui::ResampleState::idle, std::memory_order_release);
}

} // namespace tg
