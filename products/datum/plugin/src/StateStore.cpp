// SPDX-License-Identifier: AGPL-3.0-or-later
#include "StateStore.h"

#include "dt/preset.h"

namespace dt {

namespace {
const juce::Identifier kBundleProp{"DatumStateJson"};
} // namespace

StateStore::StateStore(juce::AudioProcessorValueTreeState& state) : state_(state) { writeToTree(mailbox_.get()); }

void StateStore::setPitchCurve(hba::dsp::EnvelopeCurve c) {
    hba::dsp::sanitizeEnvelope(c);
    auto cur = mailbox_.get();
    cur.pitchCurve = c;
    mailbox_.set(cur);
    writeToTree(cur);
}

void StateStore::setAmpCurve(hba::dsp::EnvelopeCurve c) {
    hba::dsp::sanitizeEnvelope(c);
    auto cur = mailbox_.get();
    cur.ampCurve = c;
    mailbox_.set(cur);
    writeToTree(cur);
}

void StateStore::setPattern(ComposePattern p) {
    sanitizePattern(p);
    auto cur = mailbox_.get();
    cur.pattern = p;
    mailbox_.set(cur);
    writeToTree(cur);
}

void StateStore::setAll(AudioStateBundle bundle) {
    hba::dsp::sanitizeEnvelope(bundle.pitchCurve);
    hba::dsp::sanitizeEnvelope(bundle.ampCurve);
    sanitizePattern(bundle.pattern);
    mailbox_.set(bundle);
    writeToTree(bundle);
}

void StateStore::writeToTree(const AudioStateBundle& bundle) {
    Preset wrapper;
    wrapper.pitchCurve = bundle.pitchCurve;
    wrapper.ampCurve = bundle.ampCurve;
    wrapper.pattern = bundle.pattern;
    state_.state.setProperty(kBundleProp, juce::String(serializePreset(wrapper)), nullptr);
}

void StateStore::reloadFromState() {
    if (!state_.state.hasProperty(kBundleProp)) {
        mailbox_.set(AudioStateBundle{});
        return;
    }
    const auto text = state_.state.getProperty(kBundleProp).toString().toStdString();
    if (auto parsed = parsePreset(text)) {
        AudioStateBundle next;
        next.pitchCurve = parsed->pitchCurve;
        next.ampCurve = parsed->ampCurve;
        next.pattern = parsed->pattern;
        mailbox_.set(next);
    }
}

} // namespace dt
