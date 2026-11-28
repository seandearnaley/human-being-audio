// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "lg/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace lg {

class PluginModel final : public ui::Model {
  public:
    explicit PluginModel(PluginProcessor& processor);
    ~PluginModel() override;

    Document& document() override { return processor_.document(); }
    void edit(const std::function<void(Document&)>& change) override;
    void editLive(const std::function<void(Document&)>& change) override;

    void launch(int section, bool direct) override;
    void fire(int bridge) override;
    void hold(int bridge, bool down) override;
    void togglePreview(ItemRef from) override;
    int land() const override;
    void setLand(int bars) override;
    bool beat() const override;
    void setBeat(bool on) override;
    bool studioVoices() const override { return processor_.paramValue(ParamId::voices) >= 0.5f; }
    void setStudioVoices(bool on) override { processor_.setParamValue(ParamId::voices, on ? 1.0f : 0.0f); }
    void auditionChord(ItemRef item, int span) override;

    int outputRole() const override { return processor_.outputRole(); }
    void setOutputRole(int role) override { processor_.setOutputRole(role); }
    bool leaderFound() const override { return processor_.leaderFound(); }
    std::optional<Chord> playedChord() const override { return processor_.playedChord(); }

    void refreshVisuals() override;
    const EngineTelemetry& telemetry() override { return telemetry_; }

    void dragMidi(ui::DragScope scope, ItemRef item, std::optional<Role> role) override;
    std::string exportStatus() const override { return status_; }

    std::string presetName() const override { return processor_.presetName(); }
    std::vector<ui::PresetInfo> presets() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override;

    bool canUndo() const override { return processor_.document().canUndo(); }
    bool canRedo() const override { return processor_.document().canRedo(); }
    void undo() override;
    void redo() override;
    bool isSlotB() const override { return processor_.document().isSlotB(); }
    void toggleAB() override;
    void copyAB() override;

    void setDragSource(juce::Component* source) { dragSource_ = source; }

  private:
    static std::int16_t itemCode(ItemRef item) noexcept;
    bool following() const { return processor_.outputRole() >= 0; }

    PluginProcessor& processor_;
    EngineTelemetry telemetry_{};
    bool pendingPublish_ = false;
    std::string status_;
    std::unique_ptr<juce::AlertWindow> saveDialog_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    juce::Component* dragSource_ = nullptr;
};

} // namespace lg
