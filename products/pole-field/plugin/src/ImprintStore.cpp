#include "ImprintStore.h"

#include "Parameters.h"
#include "pf/preset.h"

namespace pf {

namespace {
const juce::Identifier kImprintProp { "imprintData" };
const juce::Identifier kImprintLabelProp { "imprintName" };
}

ImprintStore::ImprintStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

ImprintData ImprintStore::get() const { return mailbox_.get(); }

std::string ImprintStore::label() const {
    const juce::SpinLock::ScopedLockType scoped(labelLock_);
    return label_;
}

void ImprintStore::writeToState(const ImprintData& imprint, const std::string& label) {
    state_.state.setProperty(kImprintProp, toJuce(serialiseImprint(imprint)), nullptr);
    state_.state.setProperty(kImprintLabelProp, toJuce(label), nullptr);
}

void ImprintStore::set(const ImprintData& imprint, std::string sourceName) {
    std::string finalLabel =
        imprint.active ? (sourceName.empty() ? "Imprinted" : std::move(sourceName)) : "Imprint WAV";
    mailbox_.set(imprint);
    {
        const juce::SpinLock::ScopedLockType scoped(labelLock_);
        label_ = finalLabel;
    }
    writeToState(imprint, finalLabel);
}

void ImprintStore::clear() { set({}, "Imprint WAV"); }

void ImprintStore::reloadFromState() {
    ImprintData loaded {};
    std::string loadedLabel = "Imprint WAV";
    const auto text = state_.state.getProperty(kImprintProp).toString();
    if (text.isNotEmpty()) {
        if (const auto parsed = parseImprint(text.toStdString())) {
            loaded = *parsed;
            if (loaded.active) {
                const auto name = state_.state.getProperty(kImprintLabelProp).toString().toStdString();
                loadedLabel = name.empty() ? "Imprinted" : name;
            }
        }
    }
    mailbox_.set(loaded);
    {
        const juce::SpinLock::ScopedLockType scoped(labelLock_);
        label_ = std::move(loadedLabel);
    }
}

}
