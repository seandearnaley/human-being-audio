// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/chord_matrix.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace tg {

class MatrixStore {
  public:
    explicit MatrixStore(juce::AudioProcessorValueTreeState& state);

    SceneMatrix get() const { return mailbox_.get(); }
    void set(const SceneMatrix& next);
    void loadPack(int packIndex);
    void reloadFromState();

    bool pull(SceneMatrix& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void writeToTree(const SceneMatrix& m);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<SceneMatrix> mailbox_{matrixForPack(0)};
};

} // namespace tg
