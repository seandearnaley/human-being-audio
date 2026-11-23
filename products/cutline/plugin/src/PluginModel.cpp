// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include "Parameters.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <string>

namespace cl {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        parameters_[i] = processor_.state.getParameter(toJuce(kParams[i].key));
        jassert(parameters_[i] != nullptr);
    }
}

PluginModel::~PluginModel() = default;

float PluginModel::get(ParamId id) const {
    const auto* parameter = parameters_[static_cast<std::size_t>(id)];
    return parameter->convertFrom0to1(parameter->getValue());
}

void PluginModel::begin(ParamId id) { parameters_[static_cast<std::size_t>(id)]->beginChangeGesture(); }

void PluginModel::set(ParamId id, float real) {
    auto* parameter = parameters_[static_cast<std::size_t>(id)];
    parameter->setValueNotifyingHost(parameter->convertTo0to1(real));
}

void PluginModel::end(ParamId id) { parameters_[static_cast<std::size_t>(id)]->endChangeGesture(); }

const ui::Visuals& PluginModel::visuals() {
    processor_.telemetry.read(visuals_);
    // When host transport is stopped, compile the live preview from current parameter values
    // so tweaking Grammar/Gravity/Seed updates the piano roll immediately even before pressing Play.
    if (!visuals_.playing) {
        Params p;
        for (int i = 0; i < kParamCount; ++i)
            p[static_cast<ParamId>(i)] = get(static_cast<ParamId>(i));
        HeldNotes held;
        const std::uint32_t sig = phraseSignature(p, 0, held);
        if (visuals_.plan.signature != sig || visuals_.plan.phraseIndex != 0)
            compilePhrase(p, 0, held, visuals_.plan);
    }
    return visuals_;
}

Curve PluginModel::laneCurve(int lane) const { return processor_.curves.get(lane); }
void PluginModel::setLaneCurve(int lane, const Curve& curve) { processor_.curves.set(lane, curve); }

void PluginModel::rollSeed() {
    const auto current = static_cast<std::uint32_t>(std::lround(get(ParamId::seed)));
    const auto ticks = static_cast<std::uint32_t>(juce::Time::getHighResolutionTicks());
    const int next = 1 + static_cast<int>(hba::dsp::laneHash(current * 2654435761U ^ ticks) % 999U);
    begin(ParamId::seed);
    set(ParamId::seed, static_cast<float>(next));
    end(ParamId::seed);
}

void PluginModel::dragMidiPhrase() {
    if (dragSource_ == nullptr)
        return;
    Params params;
    for (int i = 0; i < kParamCount; ++i)
        params[static_cast<ParamId>(i)] = get(static_cast<ParamId>(i));
    const double bpm = processor_.telemetry.lastBpm();
    const auto file = processor_.exporter.writeTempFile(params, processor_.curves.all(), bpm,
                                                        toJuce(processor_.presets.currentName()));
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

void PluginModel::loadPreset(int index) { processor_.presets.load(index); }
void PluginModel::stepPreset(int delta) { processor_.presets.step(delta); }

void PluginModel::savePreset() {
    saveDialog_ = std::make_unique<juce::AlertWindow>("Save preset", juce::String(), juce::MessageBoxIconType::NoIcon);
    saveDialog_->addTextEditor("name", toJuce(processor_.presets.currentName()));
    saveDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    saveDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
                                     if (result == 1)
                                         processor_.presets.save(saveDialog_->getTextEditorContents("name"));
                                 }));
}

} // namespace cl
