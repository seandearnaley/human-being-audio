// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OverrideStore.h"

#include "Parameters.h"
#include "cl/preset.h"

namespace cl {

namespace {
const juce::Identifier kOverridesProp{"phraseOverrides"};
} // namespace

OverrideStore::OverrideStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

PhraseOverrides OverrideStore::get() const { return mailbox_.get(); }

void OverrideStore::writeToState(const PhraseOverrides& overrides) {
    if (overrides.hasAny())
        state_.state.setProperty(kOverridesProp, toJuce(serialiseOverrides(overrides)), nullptr);
    else
        state_.state.removeProperty(kOverridesProp, nullptr);
}

void OverrideStore::set(const PhraseOverrides& overrides) {
    mailbox_.set(overrides);
    writeToState(overrides);
}

void OverrideStore::clear() { set(PhraseOverrides{}); }

void OverrideStore::reloadFromState() {
    PhraseOverrides loaded{};
    const auto text = state_.state.getProperty(kOverridesProp).toString();
    if (text.isNotEmpty()) {
        if (const auto parsed = parseOverrides(text.toStdString()))
            loaded = *parsed;
    }
    mailbox_.set(loaded);
}

} // namespace cl
