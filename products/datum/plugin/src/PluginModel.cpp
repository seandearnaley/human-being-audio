// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include <hba/host/offline_export.h>

#include <algorithm>
#include <cmath>

namespace dt {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParamCount; ++i)
        params_[i] = processor_.state.getParameter(std::string(kParamSpecs[i].key));
    updatePreviewIfNeeded();
}

float PluginModel::get(ParamId id) const {
    const auto idx = static_cast<std::size_t>(id);
    if (params_[idx] == nullptr)
        return kParamSpecs[idx].def;
    return params_[idx]->convertFrom0to1(params_[idx]->getValue());
}

void PluginModel::begin(ParamId id) {
    if (!applyingSnapshot_ && id != ParamId::seqRun && id != ParamId::refBassMode)
        recordUndoBoundary();
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->beginChangeGesture();
}

void PluginModel::set(ParamId id, float real) {
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->setValueNotifyingHost(p->convertTo0to1(clampParam(spec(id), real)));
}

void PluginModel::end(ParamId id) {
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->endChangeGesture();
}

void PluginModel::refreshVisuals() {
    visuals_ = processor_.telemetry.read();
    updatePreviewIfNeeded();
}

const PreviewSnapshot& PluginModel::preview() {
    updatePreviewIfNeeded();
    return preview_;
}

void PluginModel::updatePreviewIfNeeded() {
    const Params curP = processor_.currentParams();
    const AudioStateBundle curB = processor_.stateStore.all();
    const std::uint64_t clickVer = processor_.clickStore.version();

    bool sameParams = previewValid_;
    if (sameParams) {
        for (std::size_t i = 0; i < kParamCount; ++i) {
            if (std::abs(curP.values[i] - cachedParams_.values[i]) > 1e-5f) {
                sameParams = false;
                break;
            }
        }
    }
    if (sameParams && curB == cachedBundle_ && clickVer == cachedClickVer_)
        return;

    cachedParams_ = curP;
    cachedBundle_ = curB;
    cachedClickVer_ = clickVer;
    const auto click = processor_.clickStore.copySample();
    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    preview_ = Engine::renderPreview(curP, curB.pitchCurve, curB.ampCurve, curB.pattern,
                                     click.valid() ? &click : nullptr, curP.choice(ParamId::activeSlot), sr);
    previewValid_ = true;
}

void PluginModel::setPitchCurve(const hba::dsp::EnvelopeCurve& curve) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 201u, juce::Time::currentTimeMillis());
    processor_.stateStore.setPitchCurve(curve);
    processor_.resampler.invalidate();
    previewValid_ = false;
}

void PluginModel::setAmpCurve(const hba::dsp::EnvelopeCurve& curve) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 202u, juce::Time::currentTimeMillis());
    processor_.stateStore.setAmpCurve(curve);
    processor_.resampler.invalidate();
    previewValid_ = false;
}

void PluginModel::setPattern(const ComposePattern& pat) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 203u, juce::Time::currentTimeMillis());
    processor_.stateStore.setPattern(pat);
    processor_.resampler.invalidate();
    previewValid_ = false;
}

void PluginModel::triggerAudition(int slotIndex, float velocity) { processor_.requestAudition(slotIndex, velocity); }

void PluginModel::applyArchetype(int archetypeIndex) {
    recordUndoBoundary();
    Params p = Params::defaults();
    auto pitch = hba::dsp::defaultKickPitchCurve();
    auto amp = hba::dsp::defaultKickAmpCurve();
    std::string label = "Neuro Core 52";

    switch (archetypeIndex) {
    case 0: // Neuro
        p.set(ParamId::coreTopology, 2.0f);
        p.set(ParamId::tuneHz, 52.0f);
        p.set(ParamId::lengthMs, 138.0f);
        p.set(ParamId::punchAmt, 0.72f);
        p.set(ParamId::clickAmt, 0.56f);
        p.set(ParamId::drive, 0.44f);
        p.set(ParamId::tight, 0.42f);
        p.set(ParamId::boxCutDb, 4.5f);
        label = "Neuro Core 52";
        break;
    case 1: // 808
        p.set(ParamId::coreTopology, 1.0f);
        p.set(ParamId::tuneHz, 44.0f);
        p.set(ParamId::lengthMs, 420.0f);
        p.set(ParamId::punchAmt, 0.36f);
        p.set(ParamId::clickAmt, 0.16f);
        p.set(ParamId::drive, 0.28f);
        p.set(ParamId::tight, 0.10f);
        p.set(ParamId::boxCutDb, 1.5f);
        label = "Twin-T 808";
        break;
    case 2: // 909
        p.set(ParamId::coreTopology, 1.0f);
        p.set(ParamId::tuneHz, 55.0f);
        p.set(ParamId::lengthMs, 195.0f);
        p.set(ParamId::punchAmt, 0.78f);
        p.set(ParamId::clickAmt, 0.62f);
        p.set(ParamId::drive, 0.42f);
        p.set(ParamId::tight, 0.28f);
        p.set(ParamId::beaterType, 2.0f);
        label = "Analog 909";
        break;
    case 3: // Tech
        p.set(ParamId::coreTopology, 3.0f);
        p.set(ParamId::tuneHz, 49.0f);
        p.set(ParamId::lengthMs, 180.0f);
        p.set(ParamId::punchAmt, 0.66f);
        p.set(ParamId::clickAmt, 0.54f);
        p.set(ParamId::drive, 0.62f);
        p.set(ParamId::tight, 0.32f);
        p.set(ParamId::tension, 0.45f);
        p.set(ParamId::cavityQ, 0.42f);
        label = "Tech Membrane";
        break;
    case 4: // Half
        p.set(ParamId::coreTopology, 0.0f);
        p.set(ParamId::tuneHz, 41.2f);
        p.set(ParamId::lengthMs, 260.0f);
        p.set(ParamId::punchAmt, 0.52f);
        p.set(ParamId::clickAmt, 0.42f);
        p.set(ParamId::drive, 0.34f);
        p.set(ParamId::tight, 0.18f);
        label = "Halftime 41";
        break;
    case 5: // Sub
        p.set(ParamId::coreTopology, 0.0f);
        p.set(ParamId::tuneHz, 46.2f);
        p.set(ParamId::lengthMs, 175.0f);
        p.set(ParamId::punchAmt, 0.45f);
        p.set(ParamId::clickAmt, 0.18f);
        p.set(ParamId::drive, 0.14f);
        p.set(ParamId::tight, 0.25f);
        label = "Pure Sub 46";
        break;
    case 6: // Click
        p.set(ParamId::coreTopology, 2.0f);
        p.set(ParamId::tuneHz, 58.0f);
        p.set(ParamId::lengthMs, 112.0f);
        p.set(ParamId::punchAmt, 0.80f);
        p.set(ParamId::clickAmt, 0.82f);
        p.set(ParamId::drive, 0.50f);
        p.set(ParamId::tight, 0.62f);
        p.set(ParamId::beaterType, 4.0f);
        label = "Scalpel Snap";
        break;
    default:
        break;
    }

    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::seqRun || id == ParamId::refBassMode || params_[i] == nullptr)
            continue;
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1(p[id]));
    }
    auto bundle = processor_.stateStore.all();
    bundle.pitchCurve = pitch;
    bundle.ampCurve = amp;
    processor_.stateStore.setAll(bundle);
    processor_.presets.setCurrentName(label);
    processor_.resampler.invalidate();
    applyingSnapshot_ = false;
    previewValid_ = false;
}

void PluginModel::mutateKick() {
    recordUndoBoundary();
    auto nextFloat = [this](float lo, float hi) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float u = static_cast<float>(rng_ >> 8) / static_cast<float>(1u << 24);
        return lo + u * (hi - lo);
    };
    const auto nudge = [&](ParamId id, float delta) {
        begin(id);
        set(id, get(id) + delta);
        end(id);
    };
    nudge(ParamId::punchAmt, nextFloat(-0.16f, 0.16f));
    nudge(ParamId::clickAmt, nextFloat(-0.20f, 0.20f));
    nudge(ParamId::drive, nextFloat(-0.16f, 0.16f));
    nudge(ParamId::harm2, nextFloat(-0.15f, 0.15f));
    nudge(ParamId::harm3, nextFloat(-0.12f, 0.12f));
    nudge(ParamId::fmRatio, nextFloat(-0.75f, 0.75f));
    nudge(ParamId::beaterTone, nextFloat(-1200.0f, 1200.0f));
    nudge(ParamId::disperseAmt, nextFloat(-0.12f, 0.12f));
    previewValid_ = false;
}

void PluginModel::browseClickSample() {
    processor_.clickStore.browseLoadWavAsync([this](const juce::File& file) {
        const auto bundle = processor_.stateStore.all();
        if (processor_.clickStore.loadAudioFile(file, processor_.currentParams(), bundle.pitchCurve, bundle.ampCurve)) {
            if (get(ParamId::clickWavMix) < 0.05f) {
                begin(ParamId::clickWavMix);
                set(ParamId::clickWavMix, 0.65f);
                end(ParamId::clickWavMix);
            }
            previewValid_ = false;
        }
    });
}

void PluginModel::alignClickSample() {
    const auto bundle = processor_.stateStore.all();
    processor_.clickStore.alignCurrent(processor_.currentParams(), bundle.pitchCurve, bundle.ampCurve);
    previewValid_ = false;
}

void PluginModel::clearClickSample() {
    processor_.clickStore.clear();
    previewValid_ = false;
}

void PluginModel::matchSubLinkPhase() {
    if (!visuals_.scActive)
        return;
    const auto bundle = processor_.stateStore.all();
    float bestCorr = 0.0f;
    const float bestDeg = Engine::solveSubLinkPhase(processor_.currentParams(), bundle.pitchCurve, bundle.ampCurve,
                                                    bundle.pattern, visuals_.scScope, &bestCorr);
    begin(ParamId::startPhase);
    set(ParamId::startPhase, bestDeg);
    end(ParamId::startPhase);
    previewValid_ = false;
}

void PluginModel::tuneToSubLinkRoot() {
    if (!visuals_.scActive || visuals_.scRootHz < 28.0f || visuals_.scRootHz > 120.0f)
        return;
    begin(ParamId::tuneHz);
    set(ParamId::tuneHz, visuals_.scRootHz);
    end(ParamId::tuneHz);
    previewValid_ = false;
}

void PluginModel::startWavExport(bool patternLoop) {
    const auto bundle = processor_.stateStore.all();
    ExportRequest req;
    req.params = processor_.currentParams();
    req.pitchCurve = bundle.pitchCurve;
    req.ampCurve = bundle.ampCurve;
    req.pattern = bundle.pattern;
    req.click = processor_.clickStore.copySample();
    req.sampleRate = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    req.bpm = processor_.lastBpm();
    req.renderPatternLoop = patternLoop;
    req.slotIndex = req.params.choice(ParamId::activeSlot);
    processor_.resampler.start(req, juce::String(processor_.presets.currentName()));
}

void PluginModel::dragExportedWav() {
    const auto file = processor_.resampler.file();
    if (dragSource_ == nullptr || !file.existsAsFile())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

void PluginModel::dragPatternMidi() {
    if (dragSource_ == nullptr)
        return;
    const auto p = processor_.currentParams();
    const auto pat = processor_.stateStore.pattern();
    const double bpm = processor_.lastBpm();
    const auto bytes = Engine::encodePatternMidi(p, pat, bpm);
    const auto safe = juce::File::createLegalFileName(processor_.presets.currentName().empty()
                                                          ? juce::String("Datum")
                                                          : juce::String(processor_.presets.currentName()));
    const auto filename = safe + "_" + juce::String(juce::roundToInt(bpm)) + "bpm_" +
                          juce::Time::getCurrentTime().formatted("%H%M%S") + ".mid";
    const auto file = hba::host::writeTempMidiFile("Datum", filename, bytes);
    if (!file.existsAsFile()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "MIDI export failed",
            "Could not write the MIDI file. Check that the temporary folder is writable, then try the export again.");
        return;
    }
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

void PluginModel::loadPreset(int index) {
    recordUndoBoundary();
    processor_.presets.load(index);
    processor_.resampler.invalidate();
    previewValid_ = false;
}

void PluginModel::stepPreset(int delta) {
    recordUndoBoundary();
    processor_.presets.step(delta);
    processor_.resampler.invalidate();
    previewValid_ = false;
}

PluginModel::Snapshot PluginModel::captureSnapshot() const {
    Snapshot s;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::seqRun || id == ParamId::refBassMode)
            continue;
        s.params[i] = params_[i] != nullptr ? params_[i]->convertFrom0to1(params_[i]->getValue()) : kParamSpecs[i].def;
    }
    s.bundle = processor_.stateStore.all();
    s.presetName = processor_.presets.currentName();
    return s;
}

void PluginModel::applySnapshot(const Snapshot& snapshot) {
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::seqRun || id == ParamId::refBassMode || params_[i] == nullptr)
            continue;
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1(snapshot.params[i]));
    }
    processor_.stateStore.setAll(snapshot.bundle);
    processor_.resampler.invalidate();
    if (!snapshot.presetName.empty())
        processor_.presets.setCurrentName(snapshot.presetName);
    applyingSnapshot_ = false;
    previewValid_ = false;
}

void PluginModel::recordUndoBoundary() {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
}

void PluginModel::undo() {
    if (const auto target = processor_.history.undo(captureSnapshot()))
        applySnapshot(*target);
}

void PluginModel::redo() {
    if (const auto target = processor_.history.redo(captureSnapshot()))
        applySnapshot(*target);
}

void PluginModel::toggleAB() { applySnapshot(processor_.history.toggleAB(captureSnapshot())); }

void PluginModel::copyAB() { processor_.history.copyActiveToOther(captureSnapshot()); }

} // namespace dt
