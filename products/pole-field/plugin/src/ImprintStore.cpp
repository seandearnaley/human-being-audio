#include "ImprintStore.h"

#include "Parameters.h"
#include "pf/preset.h"

namespace pf {

namespace {
const juce::Identifier kImprintProp { "imprintData" };
const juce::Identifier kImprintLabelProp { "imprintName" };

juce::Identifier slotLabelProp(int slot) {
    return slot == 0 ? kImprintLabelProp : juce::Identifier("imprintName" + juce::String(slot));
}

std::string defaultSlotName(int slot) {
    static constexpr std::array<const char*, kNumImprintSlots> kNames{"Imprint A", "Imprint B", "Imprint C",
                                                                      "Imprint D"};
    return kNames[static_cast<std::size_t>(std::clamp(slot, 0, kNumImprintSlots - 1))];
}
} // namespace

ImprintStore::ImprintStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

ImprintData ImprintStore::get() const { return mailbox_.get(); }

std::string ImprintStore::label() const {
    const auto imp = mailbox_.get();
    const juce::SpinLock::ScopedLockType scoped(labelLock_);
    int activeCount = 0;
    std::string first;
    for (int i = 0; i < kNumImprintSlots; ++i) {
        if (imp.slotEnabled(i)) {
            ++activeCount;
            if (first.empty())
                first = slotLabels_[static_cast<std::size_t>(i)].empty() ? defaultSlotName(i)
                                                                         : slotLabels_[static_cast<std::size_t>(i)];
        }
    }
    if (activeCount == 0)
        return "Imprint WAV";
    if (activeCount == 1)
        return first;
    return first + " (+" + std::to_string(activeCount - 1) + ")";
}

std::string ImprintStore::slotLabel(int slot) const {
    if (slot < 0 || slot >= kNumImprintSlots)
        return {};
    const auto imp = mailbox_.get();
    if (!imp.slotEnabled(slot))
        return {};
    const juce::SpinLock::ScopedLockType scoped(labelLock_);
    const auto& s = slotLabels_[static_cast<std::size_t>(slot)];
    return s.empty() ? defaultSlotName(slot) : s;
}

std::array<std::string, kNumImprintSlots> ImprintStore::slotLabels() const {
    const juce::SpinLock::ScopedLockType scoped(labelLock_);
    return slotLabels_;
}

void ImprintStore::writeToState(const ImprintData& imprint, const std::array<std::string, kNumImprintSlots>& labels) {
    state_.state.setProperty(kImprintProp, toJuce(serialiseImprint(imprint)), nullptr);
    for (int i = 0; i < kNumImprintSlots; ++i)
        state_.state.setProperty(slotLabelProp(i), toJuce(labels[static_cast<std::size_t>(i)]), nullptr);
}

void ImprintStore::setAll(const ImprintData& imprint, const std::array<std::string, kNumImprintSlots>& labels) {
    ImprintData normalized = imprint;
    if (normalized.active && !normalized.slotActive[0])
        normalized.setSlot(0, normalized.corner);
    else if (normalized.slotActive[0]) {
        normalized.active = true;
        normalized.corner = normalized.slots[0];
    }

    std::array<std::string, kNumImprintSlots> cleanLabels{};
    for (int i = 0; i < kNumImprintSlots; ++i) {
        if (normalized.slotEnabled(i)) {
            const auto& src = labels[static_cast<std::size_t>(i)];
            cleanLabels[static_cast<std::size_t>(i)] = src.empty() ? defaultSlotName(i) : src;
        }
    }

    mailbox_.set(normalized);
    {
        const juce::SpinLock::ScopedLockType scoped(labelLock_);
        slotLabels_ = cleanLabels;
    }
    writeToState(normalized, cleanLabels);
}

void ImprintStore::set(const ImprintData& imprint, std::string sourceName) {
    ImprintData normalized = imprint;
    // Sync legacy active/corner <-> slot 0 so both single-slot and multi-slot callers stay coherent.
    if (normalized.active && !normalized.slotActive[0])
        normalized.setSlot(0, normalized.corner);
    else if (normalized.slotActive[0]) {
        normalized.active = true;
        normalized.corner = normalized.slots[0];
    }

    std::array<std::string, kNumImprintSlots> labels{};
    for (int i = 0; i < kNumImprintSlots; ++i) {
        if (normalized.slotEnabled(i))
            labels[static_cast<std::size_t>(i)] = (i == 0 && !sourceName.empty()) ? sourceName : defaultSlotName(i);
    }

    mailbox_.set(normalized);
    {
        const juce::SpinLock::ScopedLockType scoped(labelLock_);
        slotLabels_ = labels;
    }
    writeToState(normalized, labels);
}

void ImprintStore::setSlot(int slot, const CornerSpec& corner, std::string sourceName) {
    if (slot < 0 || slot >= kNumImprintSlots)
        return;
    ImprintData updated = mailbox_.get();
    updated.setSlot(slot, corner);
    std::array<std::string, kNumImprintSlots> labels{};
    {
        const juce::SpinLock::ScopedLockType scoped(labelLock_);
        slotLabels_[static_cast<std::size_t>(slot)] =
            sourceName.empty() ? defaultSlotName(slot) : std::move(sourceName);
        labels = slotLabels_;
    }
    mailbox_.set(updated);
    writeToState(updated, labels);
}

void ImprintStore::clearSlot(int slot) {
    if (slot < 0 || slot >= kNumImprintSlots)
        return;
    ImprintData updated = mailbox_.get();
    updated.clearSlot(slot);
    std::array<std::string, kNumImprintSlots> labels{};
    {
        const juce::SpinLock::ScopedLockType scoped(labelLock_);
        slotLabels_[static_cast<std::size_t>(slot)].clear();
        labels = slotLabels_;
    }
    mailbox_.set(updated);
    writeToState(updated, labels);
}

void ImprintStore::clear() { set({}, {}); }

void ImprintStore::reloadFromState() {
    ImprintData loaded {};
    std::array<std::string, kNumImprintSlots> loadedLabels{};
    const auto text = state_.state.getProperty(kImprintProp).toString();
    if (text.isNotEmpty()) {
        if (const auto parsed = parseImprint(text.toStdString())) {
            loaded = *parsed;
            for (int i = 0; i < kNumImprintSlots; ++i) {
                if (loaded.slotEnabled(i)) {
                    const auto name = state_.state.getProperty(slotLabelProp(i)).toString().toStdString();
                    loadedLabels[static_cast<std::size_t>(i)] = name.empty() ? defaultSlotName(i) : name;
                }
            }
        }
    }
    mailbox_.set(loaded);
    {
        const juce::SpinLock::ScopedLockType scoped(labelLock_);
        slotLabels_ = std::move(loadedLabels);
    }
}

}
