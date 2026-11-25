// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Bouncer.h"

namespace sb {

juce::File Bouncer::directory() {
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Human Being Audio")
        .getChildFile("Strike Body")
        .getChildFile("One-Shots");
}

Bouncer::Bouncer() : juce::Thread("Strike Body bounce") {}

Bouncer::~Bouncer() { stopThread(10000); }

void Bouncer::start(const OneShotRequest& request, const juce::String& presetName) {
    if (isThreadRunning())
        return;

    const std::string stem = keyTaggedStem(presetName.isEmpty() ? std::string("Strike Body") : presetName.toStdString(),
                                           request.params.real(ParamId::bodyHz));
    const auto safe = juce::File::createLegalFileName(juce::String(stem));
    const auto name = safe + "_" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav";
    {
        const juce::ScopedLock scoped(lock_);
        request_ = request;
        request_.oversampling = 8;
        request_.normalizePeak = true;
        target_ = directory().getChildFile(name);
    }
    const std::uint64_t gen = generation_.fetch_add(1, std::memory_order_acq_rel) + 1u;
    activeGen_.store(gen, std::memory_order_release);
    state_.store(ui::BounceState::rendering, std::memory_order_release);
    startThread();
}

void Bouncer::invalidate() noexcept {
    generation_.fetch_add(1, std::memory_order_acq_rel);
    ui::BounceState expected = ui::BounceState::ready;
    state_.compare_exchange_strong(expected, ui::BounceState::idle, std::memory_order_acq_rel);
}

juce::File Bouncer::file() const {
    if (state_.load(std::memory_order_acquire) != ui::BounceState::ready)
        return {};
    const juce::ScopedLock scoped(lock_);
    return target_;
}

void Bouncer::run() {
    const std::uint64_t myGen = activeGen_.load(std::memory_order_acquire);
    OneShotRequest req;
    juce::File target;
    {
        const juce::ScopedLock scoped(lock_);
        req = request_;
        target = target_;
    }
    const auto audio = renderOneShot(req);
    if (generation_.load(std::memory_order_acquire) != myGen) {
        state_.store(ui::BounceState::idle, std::memory_order_release);
        return;
    }
    const bool ok = target.getParentDirectory().createDirectory() &&
                    writeWavFloat(target.getFullPathName().toStdString(), audio, juce::roundToInt(req.sampleRate));
    const bool stillCurrent = generation_.load(std::memory_order_acquire) == myGen;
    state_.store((ok && stillCurrent) ? ui::BounceState::ready : ui::BounceState::idle, std::memory_order_release);
}

} // namespace sb
