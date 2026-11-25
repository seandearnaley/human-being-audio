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
    state_.store(ui::BounceState::rendering, std::memory_order_release);
    startThread();
}

void Bouncer::invalidate() noexcept {
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
    OneShotRequest req;
    juce::File target;
    {
        const juce::ScopedLock scoped(lock_);
        req = request_;
        target = target_;
    }
    const auto audio = renderOneShot(req);
    const bool ok = target.getParentDirectory().createDirectory() &&
                    writeWavFloat(target.getFullPathName().toStdString(), audio, juce::roundToInt(req.sampleRate));
    state_.store(ok ? ui::BounceState::ready : ui::BounceState::idle, std::memory_order_release);
}

} // namespace sb
