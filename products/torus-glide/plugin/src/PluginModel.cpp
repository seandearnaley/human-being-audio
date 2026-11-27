// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include "MidiExporter.h"

#include <algorithm>
#include <cmath>

namespace tg {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParamCount; ++i)
        params_[i] = processor_.state.getParameter(std::string(paramSpecs()[i].key));
}

float PluginModel::get(ParamId id) const {
    const auto idx = static_cast<std::size_t>(id);
    if (params_[idx] == nullptr)
        return paramSpecs()[idx].def;
    return params_[idx]->convertFrom0to1(params_[idx]->getValue());
}

void PluginModel::begin(ParamId id) {
    if (!applyingSnapshot_)
        recordUndoBoundary();
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->beginChangeGesture();
}

void PluginModel::set(ParamId id, float real) {
    if (id == ParamId::morph)
        processor_.clearKeySwitch();
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->setValueNotifyingHost(p->convertTo0to1(clampValue(specOf(id), real)));
}

void PluginModel::end(ParamId id) {
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->endChangeGesture();
}

void PluginModel::setCurve(int lane, const hba::dsp::Curve& next) {
    if (!applyingSnapshot_) {
        processor_.history.pushCoalesced(captureSnapshot(), 100u + static_cast<std::uint64_t>(lane),
                                         juce::Time::currentTimeMillis());
    }
    processor_.curves.set(lane, next);
    processor_.resampler.invalidate();
}

void PluginModel::setSceneMatrix(const SceneMatrix& next) {
    if (!applyingSnapshot_) {
        processor_.history.pushCoalesced(captureSnapshot(), 200u, juce::Time::currentTimeMillis());
    }
    processor_.matrix.set(next);
    processor_.resampler.invalidate();
}

void PluginModel::optimizeVoiceLeading() {
    recordUndoBoundary();
    SceneMatrix m = processor_.matrix.get();
    tg::optimizeVoiceLeading(m, get(ParamId::anchorSub) >= 0.5f);
    m.isCustom = true;
    processor_.matrix.set(m);
    processor_.resampler.invalidate();
}

void PluginModel::resetSceneMatrix() {
    recordUndoBoundary();
    const int pack = std::clamp(static_cast<int>(std::lround(get(ParamId::voicingPack))), 0, kVoicingPackCount - 1);
    processor_.matrix.loadPack(pack);
    processor_.resampler.invalidate();
}

bool PluginModel::captureHeldChord(int sceneIndex) {
    const auto snap = processor_.telemetry.read();
    if (snap.heldNoteCount <= 0)
        return false;
    recordUndoBoundary();
    const int rootPc = std::clamp(static_cast<int>(std::lround(get(ParamId::rootKey))), 0, 11);
    // Anchor captured offsets relative to root octave near lowest held note so voicings stay clean & transposable!
    const int lowestOct = std::max(1, (snap.heldNotes[0] / 12) - 1);
    const int refRootMidi = (lowestOct + 1) * 12 + rootPc;
    SceneMatrix m = processor_.matrix.get();
    if (!captureChordIntoScene(m, sceneIndex, snap.heldNotes.data(), snap.heldNoteCount, refRootMidi, true,
                               get(ParamId::anchorSub) >= 0.5f))
        return false;
    processor_.matrix.set(m);
    processor_.resampler.invalidate();
    return true;
}

void PluginModel::jumpToScene(int sceneIndex) {
    const int s = std::clamp(sceneIndex, 0, kSceneCount - 1);
    const int modeIdx = std::clamp(static_cast<int>(std::lround(get(ParamId::mode))), 0, 2);
    const int segTotal = (modeIdx == 2) ? kSceneCount : (kSceneCount - 1);
    const float targetPct = (static_cast<float>(s) / static_cast<float>(segTotal)) * 100.0f;
    recordUndoBoundary();
    processor_.clearKeySwitch();
    if (get(ParamId::morphAuto) >= 0.5f) {
        begin(ParamId::morphAuto);
        set(ParamId::morphAuto, 0.0f);
        end(ParamId::morphAuto);
    }
    begin(ParamId::morph);
    set(ParamId::morph, targetPct);
    end(ParamId::morph);
}

void PluginModel::loadPreset(int index) {
    recordUndoBoundary();
    processor_.clearKeySwitch();
    processor_.presets.load(index);
    processor_.resampler.invalidate();
}

void PluginModel::stepPreset(int delta) {
    recordUndoBoundary();
    processor_.clearKeySwitch();
    processor_.presets.step(delta);
    processor_.resampler.invalidate();
}

void PluginModel::startResample() {
    const auto perf = processor_.lastPerformance();
    RenderRequest req;
    req.params = processor_.currentParams();
    req.curves = processor_.curves.all();
    req.matrix = processor_.matrix.get();
    req.note = perf.note;
    req.bpm = perf.bpm;
    req.sampleRate = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    processor_.resampler.start(req, juce::String(processor_.presets.currentName()));
}

void PluginModel::dragResampledWav() {
    const auto file = processor_.resampler.file();
    if (dragSource_ == nullptr || !file.existsAsFile())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

void PluginModel::dragExportedMidi() {
    if (dragSource_ == nullptr)
        return;
    const auto perf = processor_.lastPerformance();
    const auto file =
        MidiExporter::writeTempFile(processor_.currentParams(), processor_.curves.all(), processor_.matrix.get(),
                                    perf.note, perf.bpm, juce::String(processor_.presets.currentName()));
    if (!file.existsAsFile()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "MIDI export failed",
            "Could not write the MIDI file. Check that the temporary folder is writable, then try the export again.");
        return;
    }
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

PluginModel::Snapshot PluginModel::captureSnapshot() const {
    Snapshot s;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::padTouch || id == ParamId::padHold)
            continue;
        s.params[i] = params_[i] != nullptr ? params_[i]->convertFrom0to1(params_[i]->getValue()) : paramSpecs()[i].def;
    }
    s.curves = processor_.curves.all();
    s.matrix = processor_.matrix.get();
    s.presetName = processor_.presets.currentName();
    return s;
}

void PluginModel::applySnapshot(const Snapshot& snapshot) {
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::padTouch || id == ParamId::padHold || params_[i] == nullptr)
            continue;
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1(snapshot.params[i]));
    }
    processor_.curves.setAll(snapshot.curves);
    processor_.matrix.set(snapshot.matrix);
    processor_.resampler.invalidate();
    if (!snapshot.presetName.empty())
        processor_.presets.setCurrentName(snapshot.presetName);
    applyingSnapshot_ = false;
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

} // namespace tg
