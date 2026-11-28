// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"

#include <algorithm>
#include <hba/host/external_file_drag.h>
#include <hba/host/offline_export.h>

#include <cmath>

namespace lg {

PluginModel::PluginModel(PluginProcessor& processor) : processor_(processor) { refreshVisuals(); }

PluginModel::~PluginModel() {
    *alive_ = false;
    if (pendingPublish_)
        processor_.songChanged();
}

std::int16_t PluginModel::itemCode(ItemRef item) noexcept {
    return static_cast<std::int16_t>(item.isBridge() ? kPreviewBridgeBase + item.index : item.index);
}

// A follower plays its leader's song: edits there would be overwritten at the next sync, so it does not take them.
void PluginModel::edit(const std::function<void(Document&)>& change) {
    if (following())
        return;
    change(processor_.document());
    pendingPublish_ = false;
    processor_.songChanged();
}

void PluginModel::editLive(const std::function<void(Document&)>& change) {
    if (following())
        return;
    change(processor_.document());
    pendingPublish_ = true; // published at the next frame, at most once a frame
}

void PluginModel::launch(int section, bool direct) {
    processor_.pushCommand(
        {direct ? CommandKind::launchDirect : CommandKind::launch, static_cast<std::int16_t>(section)});
}

void PluginModel::fire(int bridge) { processor_.pushCommand({CommandKind::fire, static_cast<std::int16_t>(bridge)}); }

void PluginModel::hold(int bridge, bool down) {
    processor_.pushCommand(down ? Command{CommandKind::hold, static_cast<std::int16_t>(bridge)}
                                : Command{CommandKind::release, 0});
}

void PluginModel::togglePreview(ItemRef from) {
    if (telemetry_.previewPlaying)
        processor_.pushCommand({CommandKind::previewStop, 0});
    else if (!telemetry_.hostPlaying)
        processor_.pushCommand({CommandKind::previewPlay, itemCode(from)});
}

int PluginModel::land() const { return landBars(static_cast<int>(processor_.paramValue(ParamId::land) + 0.5f)); }

void PluginModel::setLand(int bars) { processor_.setParamValue(ParamId::land, static_cast<float>(landChoice(bars))); }

bool PluginModel::beat() const { return processor_.paramValue(ParamId::beat) >= 0.5f; }

void PluginModel::setBeat(bool on) { processor_.setParamValue(ParamId::beat, on ? 1.0f : 0.0f); }

void PluginModel::auditionChord(ItemRef item, int span) {
    if (!telemetry_.playing)
        processor_.pushCommand({CommandKind::audition, itemCode(item), static_cast<std::int16_t>(span)});
}

void PluginModel::refreshVisuals() {
    telemetry_ = processor_.telemetry();
    if (pendingPublish_) {
        pendingPublish_ = false;
        processor_.songChanged();
    }
}

void PluginModel::dragMidi(ui::DragScope scope, ItemRef item, std::optional<Role> role) {
    if (dragSource_ == nullptr)
        return;
    auto& doc = processor_.document();
    const double bpm = telemetry_.playing ? telemetry_.bpm : doc.song().bpm;
    std::vector<std::uint8_t> bytes;
    std::string name = doc.song().name;
    switch (scope) {
    case ui::DragScope::item:
        bytes = itemMidiFile(doc, item, role, bpm);
        name += " " + itemKey(doc.song(), item);
        break;
    case ui::DragScope::song:
        bytes = songMidiFile(doc, role, bpm);
        break;
    case ui::DragScope::performance: {
        auto notes = processor_.capturedPerformance();
        if (notes.empty()) {
            status_ = "Play something first: the performance is captured while it plays";
            return;
        }
        double first = notes.front().startBeats;
        for (const auto& n : notes)
            first = std::min(first, n.startBeats);
        const double shift = std::floor(first / 4.0) * 4.0; // keep the bar grid
        for (auto& n : notes) {
            n.startBeats -= shift;
            n.endBeats -= shift;
        }
        bytes = notesMidiFile(notes, role, bpm);
        name += " performance";
        break;
    }
    }
    if (role)
        name += " " + std::string(roleName(*role));
    const auto file = hba::host::writeTempMidiFile("Ligature", juce::File::createLegalFileName(toJuce(name)) + ".mid",
                                                   std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    if (!file.existsAsFile()) {
        status_ = "Could not write the MIDI file: check that the temporary folder is writable";
        return;
    }
    status_.clear();
    hba::host::dragExternalFile(file, dragSource_);
}

std::vector<ui::PresetInfo> PluginModel::presets() const {
    std::vector<ui::PresetInfo> out;
    for (const auto& s : processor_.presets.all())
        out.push_back({s.name, s.category});
    return out;
}

void PluginModel::loadPreset(int index) {
    if (!following())
        processor_.loadPreset(index);
}

void PluginModel::stepPreset(int delta) {
    if (!following())
        processor_.loadPreset(processor_.presets.step(processor_.presetName(), delta));
}

void PluginModel::savePreset() {
    saveDialog_ = std::make_unique<juce::AlertWindow>("Save preset", juce::String(), juce::MessageBoxIconType::NoIcon);
    saveDialog_->addTextEditor("name", toJuce(processor_.document().song().name));
    saveDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto alive = alive_;
    saveDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this, alive](int result) {
                                     if (!*alive || result != 1 || saveDialog_ == nullptr)
                                         return;
                                     const auto name = saveDialog_->getTextEditorContents("name");
                                     if (processor_.presets.save(name, processor_.document().song())) {
                                         processor_.document().setName(name.trim().toStdString());
                                         processor_.state.state.setProperty("presetName", name.trim(), nullptr);
                                         processor_.songChanged();
                                     }
                                 }));
}

void PluginModel::undo() {
    if (following())
        return;
    processor_.document().undo();
    processor_.songChanged();
}

void PluginModel::redo() {
    if (following())
        return;
    processor_.document().redo();
    processor_.songChanged();
}

void PluginModel::toggleAB() {
    if (following())
        return;
    processor_.document().toggleAB();
    processor_.songChanged();
}

void PluginModel::copyAB() {
    if (following())
        return;
    processor_.document().copyAB();
}

} // namespace lg
