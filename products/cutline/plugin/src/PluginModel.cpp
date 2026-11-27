// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include "Parameters.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace cl {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        parameters_[i] = processor_.state.getParameter(toJuce(kParams[i].key));
        jassert(parameters_[i] != nullptr);
    }
    refreshVisuals();
}

PluginModel::~PluginModel() {
    *alive_ = false;
    if (saveDialog_)
        saveDialog_->exitModalState(0);
}

float PluginModel::get(ParamId id) const {
    const auto* parameter = parameters_[static_cast<std::size_t>(id)];
    return parameter->convertFrom0to1(parameter->getValue());
}

void PluginModel::begin(ParamId id) {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    parameters_[static_cast<std::size_t>(id)]->beginChangeGesture();
}

void PluginModel::set(ParamId id, float real) {
    auto* parameter = parameters_[static_cast<std::size_t>(id)];
    parameter->setValueNotifyingHost(parameter->convertTo0to1(real));
}

void PluginModel::end(ParamId id) { parameters_[static_cast<std::size_t>(id)]->endChangeGesture(); }

void PluginModel::refreshVisuals() {
    processor_.telemetry.read(visuals_);
    if (visuals_.playing)
        return;
    // Stopped: show what the parameters say will play from the top of the pattern.
    Params p;
    for (int i = 0; i < kParamCount; ++i)
        p[static_cast<ParamId>(i)] = get(static_cast<ParamId>(i));
    const int scene = std::clamp(p.choice(ParamId::scene), 0, kSceneCount - 1);
    const double bpm = processor_.telemetry.lastBpm();
    visuals_.activeScene = scene;
    visuals_.pendingScene = -1;
    visuals_.activePadProgram = std::clamp(p.choice(ParamId::padProgram), 0, kPerfPadCount - 1);
    visuals_.pendingPadProgram = -1;
    visuals_.padEngaged = p.on(ParamId::padTouch) || p.on(ParamId::padHold);
    visuals_.position = {};
    visuals_.bpm = bpm;
    if (p.choice(ParamId::engineMode) == static_cast<int>(EngineMode::slicer)) {
        // The preview phrase: recompiled only when something it depends on changed, not on every UI tick.
        const GrooveState& state = groove();
        const SlicerInputs now = slicerInputs(p, scene, visuals_.heldNotes, bpm, grooveRevision_);
        if (!now.sameAs(previewInputs_)) {
            compileSlicer(p, state, scene, visuals_.heldNotes, bpm, previewPlan_);
            previewInputs_ = now;
        }
        visuals_.slicerPlan = previewPlan_;
    }
}

const ui::Visuals& PluginModel::visuals() { return visuals_; }

Curve PluginModel::laneCurve(int lane) const { return processor_.curves.get(lane); }
void PluginModel::setLaneCurve(int lane, const Curve& curve) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 0x1000u + static_cast<std::uint64_t>(lane),
                                         static_cast<std::int64_t>(juce::Time::currentTimeMillis()));
    processor_.curves.set(lane, curve);
}

// The model keeps a copy of the published GrooveState and refetches it only when the store changed.
const GrooveState& PluginModel::groove() const {
    const std::uint32_t revision = processor_.patterns.revision();
    if (revision != grooveRevision_) {
        groove_ = processor_.patterns.get();
        grooveRevision_ = revision;
    }
    return groove_;
}

void PluginModel::setGrooveBank(const GrooveBank& bank) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 0x2000u,
                                         static_cast<std::int64_t>(juce::Time::currentTimeMillis()));
    processor_.patterns.set(bank);
}

void PluginModel::cloneActiveSceneToNext() {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    const int cur = std::clamp(static_cast<int>(get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    const int next = (cur + 1) % kSceneCount;
    GrooveBank bank = processor_.patterns.bank();
    bank.scenes[static_cast<std::size_t>(next)] = bank.scenes[static_cast<std::size_t>(cur)];
    processor_.patterns.set(bank);
    begin(ParamId::scene);
    set(ParamId::scene, static_cast<float>(next));
    end(ParamId::scene);
}

// A cleared scene is empty and stays empty. In MIDI Slicer mode, Clear empties the gate strip instead.
void PluginModel::clearActiveScene() {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    const int scene = std::clamp(static_cast<int>(get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    GrooveBank bank = processor_.patterns.bank();
    if (static_cast<int>(get(ParamId::engineMode) + 0.5f) == static_cast<int>(EngineMode::slicer)) {
        bank.slicerLength = 16;
        bank.slicerSteps = {};
    } else {
        bank.scenes[static_cast<std::size_t>(scene)] = Pattern{};
    }
    processor_.patterns.set(bank);
}

void PluginModel::dragMidiPhrase() {
    if (dragSource_ == nullptr)
        return;
    Params params;
    for (int i = 0; i < kParamCount; ++i)
        params[static_cast<ParamId>(i)] = get(static_cast<ParamId>(i));
    ui::Visuals snap{};
    processor_.telemetry.peek(snap);
    const double bpm = processor_.telemetry.lastBpm();
    const int scIdx = snap.playing ? snap.activeScene : std::clamp(params.choice(ParamId::scene), 0, kSceneCount - 1);
    const auto file =
        processor_.exporter.writeTempFile(params, processor_.curves.all(), processor_.patterns.bank(), snap.heldNotes,
                                          bpm, scIdx, toJuce(processor_.presets.currentName()));
    if (!file.existsAsFile())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

std::string PluginModel::exportStatus() const {
    const int scene = std::clamp(static_cast<int>(get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    return "Drag " + std::to_string(patternBars(groove().bank.scenes[static_cast<std::size_t>(scene)])) + "-bar .mid";
}

std::string PluginModel::presetName() const { return processor_.presets.currentName(); }

std::vector<ui::PresetInfo> PluginModel::presets() const {
    std::vector<ui::PresetInfo> result;
    for (const auto& preset : processor_.presets.all())
        result.push_back({preset.name, preset.category});
    return result;
}

void PluginModel::loadPreset(int index) {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    processor_.presets.load(index);
}

void PluginModel::stepPreset(int delta) {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    processor_.presets.step(delta);
}

void PluginModel::savePreset() {
    saveDialog_ = std::make_unique<juce::AlertWindow>("Save preset", juce::String(), juce::MessageBoxIconType::NoIcon);
    saveDialog_->addTextEditor("name", toJuce(processor_.presets.currentName()));
    saveDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto alive = alive_;
    saveDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this, alive](int result) {
                                     if (!*alive)
                                         return;
                                     if (result == 1 && saveDialog_ != nullptr)
                                         processor_.presets.save(saveDialog_->getTextEditorContents("name"));
                                 }));
}

PluginModel::Snapshot PluginModel::captureSnapshot() const {
    Snapshot s;
    for (std::size_t i = 0; i < kParams.size(); ++i)
        s.params[i] = parameters_[i]->convertFrom0to1(parameters_[i]->getValue());
    s.curves = processor_.curves.all();
    s.bank = processor_.patterns.bank();
    s.presetName = processor_.presets.currentName();
    return s;
}

void PluginModel::applySnapshot(const Snapshot& snapshot) {
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParams.size(); ++i)
        parameters_[i]->setValueNotifyingHost(parameters_[i]->convertTo0to1(snapshot.params[i]));
    processor_.curves.setAll(snapshot.curves);
    processor_.patterns.set(snapshot.bank);
    if (!snapshot.presetName.empty())
        processor_.presets.setCurrentName(snapshot.presetName);
    applyingSnapshot_ = false;
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

} // namespace cl
