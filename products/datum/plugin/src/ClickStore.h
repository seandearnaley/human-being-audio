// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dt/engine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace dt {

class ClickStore {
  public:
    using PublishFn = std::function<void(const ClickSample&)>;
    using ClearFn = std::function<void()>;

    ClickStore(PublishFn onPublish, ClearFn onClear);
    ~ClickStore();

    bool loadAudioFile(const juce::File& file, const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                       const hba::dsp::EnvelopeCurve& ampCurve);
    void browseLoadWavAsync(std::function<void(const juce::File&)> onChosen);
    void alignCurrent(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                      const hba::dsp::EnvelopeCurve& ampCurve);
    void clear();
    void appendState(juce::ValueTree& session) const;
    static bool decodeState(juce::ValueTree& session, ClickSample& sample);
    void replace(ClickSample sample);

    bool hasSample() const noexcept { return hasSample_.load(std::memory_order_acquire); }
    std::uint64_t version() const noexcept { return version_.load(std::memory_order_acquire); }
    std::string name() const;
    ClickSample copySample() const;

  private:
    PublishFn onPublish_;
    ClearFn onClear_;
    juce::AudioFormatManager formats_;
    mutable juce::CriticalSection lock_;
    ClickSample sample_;
    std::atomic<bool> hasSample_{false};
    std::atomic<std::uint64_t> version_{0};
    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace dt
