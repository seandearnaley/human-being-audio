// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include "Parameters.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace cl {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        parameters_[i] = processor_.state.getParameter(toJuce(kParams[i].key));
        jassert(parameters_[i] != nullptr);
    }
    if (processor_.seedHistory.empty()) {
        processor_.seedHistory.push_back(std::clamp(static_cast<int>(std::lround(get(ParamId::seed))), 1, 999));
        processor_.seedCursor = 0;
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
    // When host transport is stopped, compile the live preview from current parameter values
    // and overrides so tweaking Grammar/Gravity/Seed/Overrides updates the piano roll immediately.
    if (!visuals_.playing) {
        Params p;
        for (int i = 0; i < kParamCount; ++i)
            p[static_cast<ParamId>(i)] = get(static_cast<ParamId>(i));
        const PhraseOverrides ov = processor_.overrides.get();
        const std::uint32_t sig = phraseSignature(p, 0, visuals_.heldNotes, ov);
        if (visuals_.plan.signature != sig || visuals_.plan.phraseIndex != 0)
            compilePhrase(p, 0, visuals_.heldNotes, visuals_.plan, ov);
        visuals_.bpm = processor_.telemetry.lastBpm();
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

PhraseOverrides PluginModel::overrides() const { return processor_.overrides.get(); }
void PluginModel::setOverrides(const PhraseOverrides& overrides) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 0x2000u,
                                         static_cast<std::int64_t>(juce::Time::currentTimeMillis()));
    processor_.overrides.set(overrides);
}
void PluginModel::clearOverrides() {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    processor_.overrides.clear();
}

void PluginModel::pushSeedHistory(int seed) {
    const int clamped = std::clamp(seed, 1, 999);
    auto& seeds = processor_.seedHistory;
    auto& cursor = processor_.seedCursor;
    if (!seeds.empty() && seeds[cursor] == clamped)
        return;
    if (cursor + 1 < seeds.size())
        seeds.resize(cursor + 1);
    seeds.push_back(clamped);
    if (seeds.size() > 64)
        seeds.erase(seeds.begin());
    cursor = seeds.size() - 1;
}

void PluginModel::rollSeed() {
    const int current = std::clamp(static_cast<int>(std::lround(get(ParamId::seed))), 1, 999);
    pushSeedHistory(current);
    const auto ticks = static_cast<std::uint32_t>(juce::Time::getHighResolutionTicks());
    int next =
        1 + static_cast<int>(hba::dsp::laneHash(static_cast<std::uint32_t>(current) * 2654435761U ^ ticks) % 999U);
    if (next == current)
        next = (next % 999) + 1;
    pushSeedHistory(next);
    begin(ParamId::seed);
    set(ParamId::seed, static_cast<float>(next));
    end(ParamId::seed);
}

bool PluginModel::canStepSeedHistory(int delta) const {
    if (delta < 0)
        return processor_.seedCursor > 0;
    if (delta > 0)
        return processor_.seedCursor + 1 < processor_.seedHistory.size();
    return false;
}

void PluginModel::stepSeedHistory(int delta) {
    if (!canStepSeedHistory(delta))
        return;
    processor_.seedCursor = static_cast<std::size_t>(static_cast<int>(processor_.seedCursor) + (delta < 0 ? -1 : 1));
    begin(ParamId::seed);
    set(ParamId::seed, static_cast<float>(processor_.seedHistory[processor_.seedCursor]));
    end(ParamId::seed);
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
    const auto file =
        processor_.exporter.writeTempFile(params, processor_.curves.all(), processor_.overrides.get(), snap.heldNotes,
                                          bpm, snap.phraseIndex, toJuce(processor_.presets.currentName()));
    if (!file.existsAsFile())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, dragSource_);
}

std::string PluginModel::exportStatus() const {
    const auto idx = static_cast<std::size_t>(std::clamp(static_cast<int>(get(ParamId::phraseBars) + 0.5f), 0, 2));
    return "Drag " + std::to_string(kPhraseBarCounts[idx]) + "-bar .mid";
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
    pushSeedHistory(std::clamp(static_cast<int>(std::lround(get(ParamId::seed))), 1, 999));
}

void PluginModel::stepPreset(int delta) {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    processor_.presets.step(delta);
    pushSeedHistory(std::clamp(static_cast<int>(std::lround(get(ParamId::seed))), 1, 999));
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
    s.overrides = processor_.overrides.get();
    s.presetName = processor_.presets.currentName();
    return s;
}

void PluginModel::applySnapshot(const Snapshot& snapshot) {
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParams.size(); ++i)
        parameters_[i]->setValueNotifyingHost(parameters_[i]->convertTo0to1(snapshot.params[i]));
    processor_.curves.setAll(snapshot.curves);
    processor_.overrides.set(snapshot.overrides);
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
