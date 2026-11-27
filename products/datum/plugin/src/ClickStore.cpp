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
    if (!reader->read(&tmp, 0, maxRead, 0, true, true))
        return false;

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
        if (!std::isfinite(v))
            return false;
        s.mono[static_cast<std::size_t>(i)] = v;
        peak = std::max(peak, std::abs(v));
    }
    if (peak > 1.0e-5f) {
        const float norm = 0.95f / peak;
        for (float& x : s.mono)
            x *= norm;
    }

    Engine::autoAlignClick(params, pitchCurve, ampCurve, s);

    replace(std::move(s));
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
    const juce::ScopedLock scoped(lock_);
    if (!sample_.valid())
        return;
    auto s = sample_;
    Engine::autoAlignClick(params, pitchCurve, ampCurve, s);
    replace(std::move(s));
}

void ClickStore::replace(ClickSample sample) {
    const juce::ScopedLock scoped(lock_);
    sample_ = std::move(sample);
    if (sample_.valid())
        onPublish_(sample_);
    else
        onClear_();
    hasSample_.store(sample_.valid(), std::memory_order_release);
    version_.fetch_add(1, std::memory_order_release);
}

void ClickStore::clear() { replace({}); }

void ClickStore::appendState(juce::ValueTree& session) const {
    const auto sample = copySample();
    if (!sample.valid())
        return;
    juce::MemoryOutputStream pcm;
    for (float v : sample.mono)
        pcm.writeFloat(v);
    juce::ValueTree click("ClickSample");
    click.setProperty("version", 1, nullptr);
    click.setProperty("rate", sample.sampleRate, nullptr);
    click.setProperty("offset", sample.offsetSamples, nullptr);
    click.setProperty("polarity", sample.polarity, nullptr);
    click.setProperty("name", juce::String(sample.name), nullptr);
    click.setProperty("pcm", pcm.getMemoryBlock(), nullptr);
    session.appendChild(click, nullptr);
}

bool ClickStore::decodeState(juce::ValueTree& session, ClickSample& sample) {
    const auto click = session.getChildWithName("ClickSample");
    if (!click.isValid())
        return true;
    const auto* pcm = click["pcm"].getBinaryData();
    if (static_cast<int>(click["version"]) != 1 || pcm == nullptr || pcm->isEmpty() ||
        pcm->getSize() % sizeof(float) != 0 || pcm->getSize() > ClickSample::kMaxSamples * sizeof(float))
        return false;
    sample.sampleRate = click["rate"];
    sample.offsetSamples = click["offset"];
    sample.polarity = click["polarity"];
    sample.name = click["name"].toString().toStdString();
    const auto frames = pcm->getSize() / sizeof(float);
    if (!std::isfinite(sample.sampleRate) || sample.sampleRate <= 0 || sample.offsetSamples < 0 ||
        static_cast<std::size_t>(sample.offsetSamples) >= frames || std::abs(sample.polarity) != 1.0f)
        return false;
    juce::MemoryInputStream stream(*pcm, false);
    sample.mono.resize(frames);
    for (float& v : sample.mono) {
        v = stream.readFloat();
        if (!std::isfinite(v))
            return false;
    }
    session.removeChild(click, nullptr);
    return true;
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
