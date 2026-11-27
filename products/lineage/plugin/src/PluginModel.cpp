// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include <algorithm>
#include <cmath>

namespace ln {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParamCount; ++i)
        params_[i] = processor_.state.getParameter(std::string(kParams[i].key));
    peaks_ = processor_.printStore.peaks();
}

float PluginModel::get(ParamId id) const {
    const auto idx = static_cast<std::size_t>(id);
    if (params_[idx] == nullptr)
        return kParams[idx].def;
    return params_[idx]->convertFrom0to1(params_[idx]->getValue());
}

void PluginModel::begin(ParamId id) {
    if (!applyingSnapshot_ && id != ParamId::padTouch && id != ParamId::padHold)
        recordUndoBoundary();
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->beginChangeGesture();
}

void PluginModel::set(ParamId id, float real) {
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->setValueNotifyingHost(p->convertTo0to1(clampToRange(spec(id), real)));
}

void PluginModel::end(ParamId id) {
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->endChangeGesture();
}

void PluginModel::setLaneCurve(int lane, const Curve& next) {
    if (!applyingSnapshot_) {
        processor_.history.pushCoalesced(captureSnapshot(), 100u + static_cast<std::uint64_t>(lane),
                                         juce::Time::currentTimeMillis());
    }
    processor_.curves.set(lane, next);
    processor_.resampler.invalidate();
}

void PluginModel::triggerPrint(bool stackGeneration) {
    processor_.triggerPrint(stackGeneration);
    // If Gen Mix is still 0%, gently nudge it to 75% on first PRINT GEN so the user hears their Strata right away!
    if (!stackGeneration && get(ParamId::genMix) < 0.05f) {
        begin(ParamId::genMix);
        set(ParamId::genMix, 0.75f);
        end(ParamId::genMix);
    }
}

void PluginModel::browseLoadWav() {
    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    processor_.printStore.browseLoadWavAsync(sr);
    if (get(ParamId::genMix) < 0.05f) {
        begin(ParamId::genMix);
        set(ParamId::genMix, 0.85f);
        end(ParamId::genMix);
    }
}

void PluginModel::clearPrint() {
    processor_.printStore.clear();
    peaks_.fill(0.0f);
}

void PluginModel::startWavExport() {
    const auto perf = processor_.lastPerformance();
    ExportRequest req;
    req.params = processor_.currentParams();
    req.curves = processor_.curves.all();
    req.printBuf = processor_.printStore.copyBuffer();
    req.note = perf.note;
    req.bpm = perf.bpm;
    req.sampleRate = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    const int barIdx =
        std::clamp(req.params.choice(ParamId::printBars), 0, static_cast<int>(kPrintBarCounts.size()) - 1);
    req.bars = kPrintBarCounts[static_cast<std::size_t>(barIdx)];
    processor_.resampler.start(req, juce::String(processor_.presets.currentName()));
}

void PluginModel::dragExportedWav() {
    const auto file = processor_.resampler.file();
    if (dragSource_ == nullptr || !file.existsAsFile())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

void PluginModel::loadPreset(int index) {
    recordUndoBoundary();
    processor_.presets.load(index);
    processor_.resampler.invalidate();
}

void PluginModel::stepPreset(int delta) {
    recordUndoBoundary();
    processor_.presets.step(delta);
    processor_.resampler.invalidate();
}

PluginModel::Snapshot PluginModel::captureSnapshot() const {
    Snapshot s;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::padTouch || id == ParamId::padHold)
            continue;
        s.params[i] = params_[i] != nullptr ? params_[i]->convertFrom0to1(params_[i]->getValue()) : kParams[i].def;
    }
    s.print = processor_.printStore.snapshot();
    s.curves = processor_.curves.all();
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
    if (snapshot.print != processor_.printStore.snapshot() || processor_.printStore.state() == ui::PrintState::printing)
        processor_.printStore.restore(snapshot.print);
    processor_.curves.setAll(snapshot.curves);
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

} // namespace ln
