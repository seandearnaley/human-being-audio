// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include <algorithm>
#include <cmath>

namespace sb {

PluginModel::PluginModel(PluginProcessor& processor)
    : processor_(processor), rng_(static_cast<std::uint32_t>(juce::Time::getHighResolutionTicks() & 0xffffffffu)) {
    for (std::size_t i = 0; i < kParamCount; ++i)
        params_[i] = processor_.state.getParameter(std::string(paramSpecs()[i].key));
    visuals_ = processor_.telemetry.read();
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
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->setValueNotifyingHost(p->convertTo0to1(clampValue(specOf(id), real)));
}

void PluginModel::end(ParamId id) {
    if (auto* p = params_[static_cast<std::size_t>(id)])
        p->endChangeGesture();
}

void PluginModel::commitParam(ParamId id, float real) {
    if (auto* p = params_[static_cast<std::size_t>(id)]) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(clampValue(specOf(id), real)));
        p->endChangeGesture();
    }
}

void PluginModel::applyParamsBatch(const Params& next, const std::string& label) {
    recordUndoBoundary();
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::bypass)
            continue;
        commitParam(id, next.real(id));
    }
    if (!label.empty())
        processor_.presets.setCurrentName(label);
    applyingSnapshot_ = false;
    processor_.bouncer.invalidate();
    processor_.triggerAudition(Articulation::main, 1.0f);
}

void PluginModel::triggerAudition(Articulation art, float velocity) { processor_.triggerAudition(art, velocity); }

void PluginModel::runAutoAlign() {
    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    const AlignResult res = solveAutoAlign(processor_.currentParams(), sr);

    recordUndoBoundary();
    applyingSnapshot_ = true;
    commitParam(ParamId::bodyPhase, res.bodyPhaseDeg);
    commitParam(ParamId::shellOffsetMs, res.shellOffsetMs);
    commitParam(ParamId::shellInvert, res.shellInvert ? 1.0f : 0.0f);
    commitParam(ParamId::wireOffsetMs, res.wireOffsetMs);
    commitParam(ParamId::wireInvert, res.wireInvert ? 1.0f : 0.0f);
    applyingSnapshot_ = false;

    processor_.bouncer.invalidate();
    processor_.triggerAudition(Articulation::main, 1.0f);
}

void PluginModel::loadArchetype(ui::Archetype kind) {
    auto recipe = makeArchetypeRecipe(kind);
    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    const auto align = solveAutoAlign(recipe.params, sr);
    recipe.params.set(ParamId::bodyPhase, align.bodyPhaseDeg);
    recipe.params.set(ParamId::shellOffsetMs, align.shellOffsetMs);
    recipe.params.set(ParamId::shellInvert, align.shellInvert ? 1.0f : 0.0f);
    recipe.params.set(ParamId::wireOffsetMs, align.wireOffsetMs);
    recipe.params.set(ParamId::wireInvert, align.wireInvert ? 1.0f : 0.0f);

    applyParamsBatch(recipe.params, std::string(recipe.name));
}

void PluginModel::mutateSound() {
    Params p = mutateParams(processor_.currentParams(), rng_);
    const double sr = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    const auto align = solveAutoAlign(p, sr);
    p.set(ParamId::bodyPhase, align.bodyPhaseDeg);
    p.set(ParamId::shellOffsetMs, align.shellOffsetMs);
    p.set(ParamId::shellInvert, align.shellInvert ? 1.0f : 0.0f);
    p.set(ParamId::wireOffsetMs, align.wireOffsetMs);
    p.set(ParamId::wireInvert, align.wireInvert ? 1.0f : 0.0f);

    std::string base = processor_.presets.currentName();
    if (base.find(" (Mut)") == std::string::npos)
        base += " (Mut)";
    applyParamsBatch(p, base);
}

void PluginModel::loadPreset(int index) {
    recordUndoBoundary();
    processor_.presets.load(index);
    processor_.bouncer.invalidate();
    processor_.triggerAudition(Articulation::main, 0.96f);
}

void PluginModel::stepPreset(int delta) {
    recordUndoBoundary();
    processor_.presets.step(delta);
    processor_.bouncer.invalidate();
    processor_.triggerAudition(Articulation::main, 0.96f);
}

void PluginModel::startBounce() {
    OneShotRequest req;
    req.params = processor_.currentParams();
    req.articulation = Articulation::main;
    req.velocity = 1.0f;
    req.pitchRatio = 1.0f;
    req.sampleRate = processor_.getSampleRate() > 8000.0 ? processor_.getSampleRate() : 48000.0;
    req.oversampling = 8;
    req.normalizePeak = true;
    processor_.bouncer.start(req, juce::String(processor_.presets.currentName()));
}

void PluginModel::dragBouncedWav() {
    const auto file = processor_.bouncer.file();
    if (dragSource_ == nullptr || !file.existsAsFile())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

PluginModel::Snapshot PluginModel::captureSnapshot() const {
    Snapshot s;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        if (static_cast<ParamId>(i) == ParamId::quality)
            continue;
        s.params[i] = params_[i] != nullptr ? params_[i]->convertFrom0to1(params_[i]->getValue()) : paramSpecs()[i].def;
    }
    s.presetName = processor_.presets.currentName();
    return s;
}

void PluginModel::applySnapshot(const Snapshot& snapshot) {
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        if (static_cast<ParamId>(i) == ParamId::quality || params_[i] == nullptr)
            continue;
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1(snapshot.params[i]));
    }
    if (!snapshot.presetName.empty())
        processor_.presets.setCurrentName(snapshot.presetName);
    applyingSnapshot_ = false;
    processor_.bouncer.invalidate();
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

} // namespace sb
