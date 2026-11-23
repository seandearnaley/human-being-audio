#include "PluginModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace pf {

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

ImprintData PluginModel::imprint() const { return processor_.imprint.get(); }
std::string PluginModel::imprintLabel() const { return processor_.imprint.label(); }
std::string PluginModel::imprintSlotLabel(int slot) const { return processor_.imprint.slotLabel(slot); }

bool PluginModel::imprintFileToSlot(const juce::File& file, int slot) {
    if (!file.existsAsFile() || slot < 0 || slot >= kNumImprintSlots)
        return false;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    // Read up to 4 seconds for Burg LPC analysis so even huge files imprint instantaneously.
    const int maxSamples = static_cast<int>(
        std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 4.0)));
    if (maxSamples <= 0)
        return false;

    juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), maxSamples);
    reader->read(&audio, 0, maxSamples, 0, true, true);

    std::vector<float> mono(static_cast<std::size_t>(maxSamples), 0.0f);
    const int channels = audio.getNumChannels();
    const float invCh = 1.0f / static_cast<float>(std::max(1, channels));
    for (int c = 0; c < channels; ++c) {
        const float* src = audio.getReadPointer(c);
        for (int i = 0; i < maxSamples; ++i)
            mono[static_cast<std::size_t>(i)] += src[i] * invCh;
    }

    const CornerSpec corner = imprintFromAudio(mono, reader->sampleRate);

    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    std::string shortName = file.getFileNameWithoutExtension().toStdString();
    if (shortName.size() > 16)
        shortName = shortName.substr(0, 15) + "\u2026";
    processor_.imprint.setSlot(slot, corner, std::move(shortName));

    // Switch immediately to Imprint Cube so the imprint is heard right away, keeping the undo step atomic.
    applyingSnapshot_ = true;
    begin(ParamId::cube);
    set(ParamId::cube, static_cast<float>(kNumCubes - 1));
    end(ParamId::cube);
    applyingSnapshot_ = false;
    return true;
}

void PluginModel::browseImprintAudio() { browseImprintAudioSlot(0); }

void PluginModel::browseImprintAudioSlot(int slot) {
    const int clampedSlot = std::clamp(slot, 0, kNumImprintSlots - 1);
    static constexpr std::array<const char*, kNumImprintSlots> kCornerLetters{"A", "B", "C", "D"};
    fileChooser_ = std::make_unique<juce::FileChooser>(
        juce::String("Imprint Corner ") + kCornerLetters[static_cast<std::size_t>(clampedSlot)] + " from audio",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aiff;*.aif;*.flac");

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    auto alive = alive_;
    fileChooser_->launchAsync(flags, [this, alive, clampedSlot](const juce::FileChooser& chooser) {
        if (!*alive)
            return;
        imprintFileToSlot(chooser.getResult(), clampedSlot);
    });
}

void PluginModel::captureLiveImprint(int slot) {
    const int clampedSlot = std::clamp(slot, 0, kNumImprintSlots - 1);
    std::vector<float> mono;
    double sr = 48000.0;
    processor_.analysis.snapshotInput(mono, sr);
    if (mono.size() < 256)
        return;

    double energy = 0.0;
    for (float s : mono)
        energy += static_cast<double>(s) * static_cast<double>(s);
    const double rms = std::sqrt(energy / static_cast<double>(mono.size()));
    if (rms < 1.0e-5)
        return;

    const CornerSpec corner = imprintFromAudio(mono, sr);
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());

    static constexpr std::array<const char*, kNumImprintSlots> kLiveNames{"Live A", "Live B", "Live C", "Live D"};
    processor_.imprint.setSlot(clampedSlot, corner, kLiveNames[static_cast<std::size_t>(clampedSlot)]);

    applyingSnapshot_ = true;
    begin(ParamId::cube);
    set(ParamId::cube, static_cast<float>(kNumCubes - 1));
    end(ParamId::cube);
    applyingSnapshot_ = false;
}

void PluginModel::clearImprint() {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    processor_.imprint.clear();
}

void PluginModel::clearImprintSlot(int slot) {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    processor_.imprint.clearSlot(slot);
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
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (static_cast<ParamId>(i) == ParamId::quality)
            continue;
        s.params[i] = parameters_[i]->convertFrom0to1(parameters_[i]->getValue());
    }
    s.curves = processor_.curves.all();
    s.imprint = processor_.imprint.get();
    s.imprintLabels = processor_.imprint.slotLabels();
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
    if (snapshot.imprint.hasAny())
        processor_.imprint.setAll(snapshot.imprint, snapshot.imprintLabels);
    else
        processor_.imprint.clear();
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
