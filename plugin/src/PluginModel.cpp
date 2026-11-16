#include "PluginModel.h"

namespace tf {

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
    const auto now = juce::Time::getHighResolutionTicks();
    const double seconds = lastUpdateTicks_ == 0 ? 0.0 : juce::Time::highResolutionTicksToSeconds(now - lastUpdateTicks_);
    lastUpdateTicks_ = now;
    processor_.analysis.update(visuals_, static_cast<float>(std::min(seconds, 0.25)));
    return visuals_;
}

std::string PluginModel::presetName() const { return processor_.presets.currentName(); }
std::vector<ui::PresetInfo> PluginModel::presets() const {
    std::vector<ui::PresetInfo> result;
    for (const auto& preset : processor_.presets.all()) result.push_back({ preset.name, preset.category });
    return result;
}
void PluginModel::loadPreset(int index) { processor_.presets.load(index); }
void PluginModel::stepPreset(int delta) { processor_.presets.step(delta); }

// The name is typed into a native dialog: text entry inside a plugin's own view
// is unreliable in some hosts.
void PluginModel::savePreset() {
    saveDialog_ = std::make_unique<juce::AlertWindow>("Save preset", juce::String(), juce::MessageBoxIconType::NoIcon);
    saveDialog_->addTextEditor("name", toJuce(processor_.presets.currentName()));
    saveDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    saveDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
        if (result == 1) processor_.presets.save(saveDialog_->getTextEditorContents("name"));
    }));
}

ui::ResampleState PluginModel::resampleState() const { return processor_.resampler.state(); }

void PluginModel::resample() {
    const auto performance = processor_.lastPerformance();
    RenderRequest request;
    request.params = processor_.currentParams();
    request.note = performance.note;
    request.bpm = performance.bpm;
    request.sampleRate = processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0;
    processor_.resampler.start(request, toJuce(processor_.presets.currentName()));
}

std::string PluginModel::resampledFile() const { return processor_.resampler.file().getFullPathName().toStdString(); }

}
