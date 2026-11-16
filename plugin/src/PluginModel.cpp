#include "PluginModel.h"

namespace tf {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        parameters_[i] = processor_.state.getParameter(toJuce(kParams[i].key));
        jassert(parameters_[i] != nullptr);
    }
}

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

const ui::Visuals& PluginModel::visuals() { return visuals_; }

std::string PluginModel::presetName() const { return "Init"; }
std::vector<std::string> PluginModel::presetNames() const { return { "Init" }; }
void PluginModel::loadPreset(int) {}
void PluginModel::stepPreset(int) {}
void PluginModel::savePreset() {}

ui::ResampleState PluginModel::resampleState() const { return ui::ResampleState::idle; }
void PluginModel::resample() {}
void PluginModel::dragResampledFile() {}

}
