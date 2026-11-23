#include "ImprintStore.h"

#include "Parameters.h"
#include "pf/preset.h"

namespace pf {

namespace {
const juce::Identifier kImprintProp { "imprintData" };
const juce::Identifier kImprintLabelProp { "imprintName" };
}

ImprintStore::ImprintStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

ImprintData ImprintStore::get() const {
    const juce::SpinLock::ScopedLockType scoped(lock_);
    return imprint_;
}

std::string ImprintStore::label() const {
    const juce::SpinLock::ScopedLockType scoped(lock_);
    return label_;
}

void ImprintStore::writeToState(const ImprintData& imprint, const std::string& label) {
    state_.state.setProperty(kImprintProp, toJuce(serialiseImprint(imprint)), nullptr);
    state_.state.setProperty(kImprintLabelProp, toJuce(label), nullptr);
}

void ImprintStore::set(const ImprintData& imprint, std::string sourceName) {
    std::string finalLabel =
        imprint.active ? (sourceName.empty() ? "Imprinted" : std::move(sourceName)) : "Imprint WAV";
    {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        imprint_ = imprint;
        label_ = finalLabel;
    }
    writeToState(imprint, finalLabel);
    changed_.store(true, std::memory_order_release);
}

void ImprintStore::clear() {
    set({}, "Imprint WAV");
}

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
    {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        imprint_ = loaded;
        label_ = std::move(loadedLabel);
    }
    changed_.store(true);
}

bool ImprintStore::pull(ImprintData& destination) noexcept {
    if (!changed_.load(std::memory_order_acquire)) return false;
    const juce::SpinLock::ScopedTryLockType scoped(lock_);
    if (!scoped.isLocked()) return false;
    destination = imprint_;
    changed_.store(false, std::memory_order_release);
    return true;
}

}
