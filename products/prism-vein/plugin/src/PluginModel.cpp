// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

namespace pv {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParamCount; ++i)
        params_[i] = processor_.state.getParameter(std::string(kParamSpecs[i].key));
}

float PluginModel::get(ParamId id) const {
    const auto idx = static_cast<std::size_t>(id);
    if (params_[idx] == nullptr)
        return kParamSpecs[idx].def;
    return params_[idx]->convertFrom0to1(params_[idx]->getValue());
}

void PluginModel::begin(ParamId id) {
    if (!applyingSnapshot_ && id != ParamId::demoAudition)
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

void PluginModel::setChordMask(std::uint16_t mask) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 301u, juce::Time::currentTimeMillis());
    processor_.stateStore.setChordMask(mask);
}

void PluginModel::setLaneCurve(int lane, const hba::dsp::Curve& curve) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 310u + static_cast<std::uint32_t>(lane),
                                         juce::Time::currentTimeMillis());
    processor_.stateStore.setLaneCurve(lane, curve);
}

void PluginModel::loadPreset(int index) {
    recordUndoBoundary();
    processor_.presets.load(index);
}

void PluginModel::stepPreset(int delta) {
    recordUndoBoundary();
    processor_.presets.step(delta);
}

PluginModel::Snapshot PluginModel::captureSnapshot() const {
    Snapshot s;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::demoAudition)
            continue;
        s.params[i] = params_[i] != nullptr ? params_[i]->convertFrom0to1(params_[i]->getValue()) : kParamSpecs[i].def;
    }
    s.extra = processor_.stateStore.all();
    s.presetName = processor_.presets.currentName();
    return s;
}

void PluginModel::applySnapshot(const Snapshot& snapshot) {
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::demoAudition || params_[i] == nullptr)
            continue;
        params_[i]->setValueNotifyingHost(params_[i]->convertTo0to1(snapshot.params[i]));
    }
    processor_.stateStore.setAll(snapshot.extra);
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

} // namespace pv
