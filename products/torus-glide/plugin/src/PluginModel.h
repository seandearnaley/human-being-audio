// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "tg/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace tg {

class PluginModel final : public ui::Model {
  public:
    explicit PluginModel(PluginProcessor& processor);

    void setDragSource(juce::Component* source) { dragSource_ = source; }

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    hba::dsp::Curve curve(int lane) const override { return processor_.curves.get(lane); }
    void setCurve(int lane, const hba::dsp::Curve& next) override;

    SceneMatrix sceneMatrix() const override { return processor_.matrix.get(); }
    void setSceneMatrix(const SceneMatrix& next) override;
    void optimizeVoiceLeading() override;
    void resetSceneMatrix() override;
    bool captureHeldChord(int sceneIndex) override;
    void jumpToScene(int sceneIndex) override;

    std::string presetName() const override { return processor_.presets.currentName(); }
    std::vector<ui::PresetEntry> presets() const override { return processor_.presets.entries(); }
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override { processor_.presets.saveUserPresetAsync(); }

    bool canUndo() const override { return processor_.history.canUndo(); }
    bool canRedo() const override { return processor_.history.canRedo(); }
    void undo() override;
    void redo() override;
    bool isSlotB() const override { return processor_.history.isSlotB(); }
    void toggleAB() override;
    void copyAB() override;

    bool isAuditioning() const override { return processor_.isAuditioning(); }
    void setAudition(bool active) override { processor_.setAudition(active); }
    ui::ResampleState resampleState() const override { return processor_.resampler.state(); }
    void startResample() override;
    void dragResampledWav() override;
    void dragExportedMidi() override;

    void refreshVisuals() override { visuals_ = processor_.telemetry.read(); }
    const TelemetrySnapshot& visuals() const override { return visuals_; }

  private:
    using Snapshot = PluginProcessor::HistorySnapshot;
    Snapshot captureSnapshot() const;
    void applySnapshot(const Snapshot& snapshot);
    void recordUndoBoundary();

    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> params_{};
    juce::Component* dragSource_ = nullptr;
    TelemetrySnapshot visuals_{};
    bool applyingSnapshot_ = false;
};

} // namespace tg
