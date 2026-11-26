// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PatternStore.h"

#include "Parameters.h"
#include "cl/preset.h"

namespace cl {

namespace {
const juce::Identifier kPatternBankProp{"patternBank"};
} // namespace

PatternStore::PatternStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

PatternBank PatternStore::get() const { return mailbox_.get(); }

void PatternStore::writeToState(const PatternBank& bank) {
    if (bank.hasAny())
        state_.state.setProperty(kPatternBankProp, toJuce(serialisePatternBank(bank)), nullptr);
    else
        state_.state.removeProperty(kPatternBankProp, nullptr);
}

void PatternStore::set(const PatternBank& bank) {
    mailbox_.set(bank);
    writeToState(bank);
}

void PatternStore::clear() { set(PatternBank{}); }

void PatternStore::reloadFromState() {
    PatternBank loaded{};
    const auto text = state_.state.getProperty(kPatternBankProp).toString();
    if (text.isNotEmpty()) {
        if (const auto parsed = parsePatternBank(text.toStdString()))
            loaded = *parsed;
    }
    mailbox_.set(loaded);
}

} // namespace cl
