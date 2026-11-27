// SPDX-License-Identifier: AGPL-3.0-or-later
#include "StateStore.h"

namespace pv {

namespace {
const juce::Identifier kChordMaskProp{"PrismVeinChordMask"};
const juce::Identifier kCurvesNode{"PrismVeinCurves"};
} // namespace

StateStore::StateStore(juce::AudioProcessorValueTreeState& state)
    : state_(state), chordMailbox_(kDefaultChordMask), curveStore_(state, defaultLaneCurves(), kCurvesNode, true) {
    state_.state.setProperty(kChordMaskProp, static_cast<int>(kDefaultChordMask), nullptr);
}

std::uint16_t StateStore::chordMask() const { return chordMailbox_.get(); }

void StateStore::setChordMask(std::uint16_t mask) {
    const auto clean = static_cast<std::uint16_t>(mask & 0x0FFFu);
    const std::uint16_t valid = clean != 0u ? clean : kDefaultChordMask;
    chordMailbox_.set(valid);
    state_.state.setProperty(kChordMaskProp, static_cast<int>(valid), nullptr);
}

hba::dsp::Curve StateStore::laneCurve(int lane) const { return curveStore_.get(lane); }

void StateStore::setLaneCurve(int lane, const hba::dsp::Curve& curve) { curveStore_.set(lane, curve); }

AudioExtraState StateStore::all() const { return {chordMailbox_.get(), curveStore_.all()}; }

void StateStore::setAll(const AudioExtraState& extra) {
    setChordMask(extra.chordMask);
    curveStore_.setAll(extra.curves);
}

void StateStore::reloadFromState() {
    std::uint16_t mask = kDefaultChordMask;
    if (state_.state.hasProperty(kChordMaskProp)) {
        const int raw = static_cast<int>(state_.state.getProperty(kChordMaskProp));
        const auto masked = static_cast<std::uint16_t>(static_cast<unsigned>(raw) & 0x0FFFu);
        if (masked != 0u)
            mask = masked;
    }
    chordMailbox_.set(mask);
    curveStore_.reloadFromState();
}

bool StateStore::pull(std::uint16_t& chordMaskOut, LaneCurves& curvesOut) noexcept {
    const bool a = chordMailbox_.pull(chordMaskOut);
    const bool b = curveStore_.pull(curvesOut);
    return a || b;
}

} // namespace pv
