// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PatternStore.h"

#include "Parameters.h"
#include "cl/groove_json.h"

namespace cl {

const juce::Identifier kGrooveBankProp{"grooveBank"};

PatternStore::PatternStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

GrooveState PatternStore::get() const { return mailbox_.get(); }

GrooveBank PatternStore::bank() const { return mailbox_.get().bank; }

void PatternStore::publish(const GrooveState& groove) {
    mailbox_.set(groove);
    revision_.fetch_add(1, std::memory_order_relaxed);
}

void PatternStore::set(const GrooveBank& bank) {
    const GrooveState groove(bank); // sanitises and ranks, here on the message thread
    publish(groove);
    state_.state.setProperty(kGrooveBankProp, toJuce(serialiseGrooveBank(groove.bank)), nullptr);
}

void PatternStore::clear() { set(GrooveBank{}); }

bool PatternStore::reloadFromState() {
    const auto text = state_.state.getProperty(kGrooveBankProp).toString();
    if (text.isNotEmpty()) {
        if (const auto parsed = parseGrooveBank(text.toStdString())) {
            publish(GrooveState(*parsed));
            return true;
        }
    }
    publish(GrooveState());
    return false;
}

} // namespace cl
