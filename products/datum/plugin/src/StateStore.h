// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dt/pattern.h"

#include <hba/dsp/envelope_curve.h>
#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace dt {

struct AudioStateBundle {
    hba::dsp::EnvelopeCurve pitchCurve = hba::dsp::defaultKickPitchCurve();
    hba::dsp::EnvelopeCurve ampCurve = hba::dsp::defaultKickAmpCurve();
    ComposePattern pattern = defaultComposePattern();

    friend bool operator==(const AudioStateBundle&, const AudioStateBundle&) = default;
};

class StateStore {
  public:
    explicit StateStore(juce::AudioProcessorValueTreeState& state);

    AudioStateBundle all() const { return mailbox_.get(); }
    hba::dsp::EnvelopeCurve pitchCurve() const { return mailbox_.get().pitchCurve; }
    hba::dsp::EnvelopeCurve ampCurve() const { return mailbox_.get().ampCurve; }
    ComposePattern pattern() const { return mailbox_.get().pattern; }

    void setPitchCurve(hba::dsp::EnvelopeCurve c);
    void setAmpCurve(hba::dsp::EnvelopeCurve c);
    void setPattern(ComposePattern p);
    void setAll(AudioStateBundle bundle);

    void reloadFromState();

    // Audio thread non-blocking pull.
    bool pull(AudioStateBundle& dst) noexcept { return mailbox_.pull(dst); }

  private:
    void writeToTree(const AudioStateBundle& bundle);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<AudioStateBundle> mailbox_{AudioStateBundle{}};
};

} // namespace dt
