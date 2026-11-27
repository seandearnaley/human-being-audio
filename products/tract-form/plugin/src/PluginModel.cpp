#include "PluginModel.h"

#include <hba/host/external_file_drag.h>

namespace tf {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) {
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        parameters_[i] = processor_.state.getParameter(toJuce(kParams[i].key));
        jassert(parameters_[i] != nullptr);
    }
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
    if (!applyingSnapshot_ && id != ParamId::quality)
        processor_.history.push(captureSnapshot());
    parameters_[static_cast<std::size_t>(id)]->beginChangeGesture();
}

void PluginModel::set(ParamId id, float real) {
    auto* parameter = parameters_[static_cast<std::size_t>(id)];
    parameter->setValueNotifyingHost(parameter->convertTo0to1(real));
}

void PluginModel::end(ParamId id) { parameters_[static_cast<std::size_t>(id)]->endChangeGesture(); }

void PluginModel::refreshVisuals() {
    const auto now = juce::Time::getHighResolutionTicks();
    const double seconds = lastUpdateTicks_ == 0 ? 0.0 : juce::Time::highResolutionTicksToSeconds(now - lastUpdateTicks_);
    lastUpdateTicks_ = now;
    processor_.analysis.update(visuals_, static_cast<float>(std::min(seconds, 0.25)));
}

const ui::Visuals& PluginModel::visuals() { return visuals_; }

Curve PluginModel::laneCurve(int lane) const { return processor_.curves.get(lane); }
void PluginModel::setLaneCurve(int lane, const Curve& curve) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 0x1000u + static_cast<std::uint64_t>(lane),
                                         static_cast<std::int64_t>(juce::Time::currentTimeMillis()));
    processor_.curves.set(lane, curve);
}

std::string PluginModel::presetName() const { return processor_.presets.currentName(); }
std::vector<ui::PresetInfo> PluginModel::presets() const {
    std::vector<ui::PresetInfo> result;
    for (const auto& preset : processor_.presets.all()) result.push_back({ preset.name, preset.category });
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

// The name is typed into a native dialog: text entry inside a plugin's own view
// is unreliable in some hosts.
void PluginModel::savePreset() {
    saveDialog_ = std::make_unique<juce::AlertWindow>("Save preset", juce::String(), juce::MessageBoxIconType::NoIcon);
    saveDialog_->addTextEditor("name", toJuce(processor_.presets.currentName()));
    saveDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    std::weak_ptr<bool> guard = alive_;
    saveDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this, guard](int result) {
                                     const auto alive = guard.lock();
                                     if (!alive || !*alive || !saveDialog_)
                                         return;
                                     if (result == 1)
                                         processor_.presets.save(saveDialog_->getTextEditorContents("name"));
                                 }));
}

ui::ResampleState PluginModel::resampleState() const { return processor_.resampler.state(); }

void PluginModel::resample() {
    const auto performance = processor_.lastPerformance();
    RenderRequest request;
    request.params = processor_.currentParams();
    request.curves = processor_.curves.all();
    request.note = performance.note;
    request.bpm = performance.bpm;
    request.sampleRate = processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0;
    processor_.resampler.start(request, toJuce(processor_.presets.currentName()));
}

// A real file URL on the pasteboard, which is what hosts accept as a sample.
void PluginModel::dragResampledFile() {
    const auto file = processor_.resampler.file();
    if (dragSource_ == nullptr || !file.existsAsFile()) return;
    hba::host::dragExternalFile(file, dragSource_);
}

PluginModel::Snapshot PluginModel::captureSnapshot() const {
    Snapshot s;
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (static_cast<ParamId>(i) == ParamId::quality)
            continue;
        s.params[i] = parameters_[i]->convertFrom0to1(parameters_[i]->getValue());
    }
    s.curves = processor_.curves.all();
    s.presetName = processor_.presets.currentName();
    return s;
}

void PluginModel::applySnapshot(const Snapshot& snapshot) {
    applyingSnapshot_ = true;
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (static_cast<ParamId>(i) == ParamId::quality)
            continue;
        parameters_[i]->setValueNotifyingHost(parameters_[i]->convertTo0to1(snapshot.params[i]));
    }
    processor_.curves.setAll(snapshot.curves);
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
}
