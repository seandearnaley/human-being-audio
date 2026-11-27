// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ClickStore.h"

#include <algorithm>
#include <cmath>

namespace dt {

ClickStore::ClickStore(PublishFn onPublish, ClearFn onClear)
    : onPublish_(std::move(onPublish)), onClear_(std::move(onClear)) {
    formats_.registerBasicFormats();
}

ClickStore::~ClickStore() { *alive_ = false; }

bool ClickStore::loadAudioFile(const juce::File& file, const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                               const hba::dsp::EnvelopeCurve& ampCurve) {
    if (!file.existsAsFile())
        return false;
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return false;

    const int maxRead = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, ClickSample::kMaxSamples));
    juce::AudioBuffer<float> tmp(static_cast<int>(reader->numChannels), maxRead);
    reader->read(&tmp, 0, maxRead, 0, true, true);

    ClickSample s;
    s.sampleRate = reader->sampleRate > 8000.0 ? reader->sampleRate : 48000.0;
    s.name = file.getFileNameWithoutExtension().toStdString();
    s.mono.resize(static_cast<std::size_t>(maxRead));

    float peak = 0.0f;
    const float invCh = 1.0f / static_cast<float>(tmp.getNumChannels());
    for (int i = 0; i < maxRead; ++i) {
        float sum = 0.0f;
        for (int ch = 0; ch < tmp.getNumChannels(); ++ch)
            sum += tmp.getSample(ch, i);
        const float v = sum * invCh;
        s.mono[static_cast<std::size_t>(i)] = v;
        peak = std::max(peak, std::abs(v));
    }
    if (peak > 1.0e-5f) {
        const float norm = 0.95f / peak;
        for (float& x : s.mono)
            x *= norm;
    }

    Engine::autoAlignClick(params, pitchCurve, ampCurve, s);

    {
        const juce::ScopedLock scoped(lock_);
        sample_ = s;
    }
    hasSample_.store(true, std::memory_order_release);
    version_.fetch_add(1, std::memory_order_acq_rel);
    if (onPublish_)
        onPublish_(s);
    return true;
}

void ClickStore::browseLoadWavAsync(std::function<void(const juce::File&)> onChosen) {
    chooser_ =
        std::make_unique<juce::FileChooser>("Load Click Transient Sample", juce::File{}, "*.wav;*.aif;*.aiff;*.flac");
    const auto alive = alive_;
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [alive, cb = std::move(onChosen)](const juce::FileChooser& fc) {
                              if (!*alive)
                                  return;
                              const auto file = fc.getResult();
                              if (file.existsAsFile() && cb)
                                  cb(file);
                          });
}

void ClickStore::alignCurrent(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                              const hba::dsp::EnvelopeCurve& ampCurve) {
    ClickSample s = copySample();
    if (!s.valid())
        return;
    Engine::autoAlignClick(params, pitchCurve, ampCurve, s);
    {
        const juce::ScopedLock scoped(lock_);
        sample_ = s;
    }
    version_.fetch_add(1, std::memory_order_acq_rel);
    if (onPublish_)
        onPublish_(s);
}

void ClickStore::clear() {
    {
        const juce::ScopedLock scoped(lock_);
        sample_ = {};
    }
    hasSample_.store(false, std::memory_order_release);
    version_.fetch_add(1, std::memory_order_acq_rel);
    if (onClear_)
        onClear_();
}

std::string ClickStore::name() const {
    const juce::ScopedLock scoped(lock_);
    return sample_.name;
}

ClickSample ClickStore::copySample() const {
    const juce::ScopedLock scoped(lock_);
    return sample_;
}

} // namespace dt
