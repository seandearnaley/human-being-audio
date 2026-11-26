// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "ln/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace ln {

class PluginModel final : public ui::Model {
  public:
    explicit PluginModel(PluginProcessor& processor);

    void setDragSource(juce::Component* source) { dragSource_ = source; }

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    void refreshVisuals() override {
        visuals_ = processor_.telemetry.read();
        peaks_ = processor_.printStore.peaks();
    }
    const TelemetrySnapshot& visuals() override { return visuals_; }

    Curve laneCurve(int lane) const override { return processor_.curves.get(lane); }
    void setLaneCurve(int lane, const Curve& curve) override;

    const std::array<float, PrintBuffer::kWaveformPoints>& printPeaks() const override { return peaks_; }
    std::string printLabel() const override { return processor_.printStore.label(); }
    int printGeneration() const override { return processor_.printStore.generation(); }
    ui::PrintState printState() const override { return processor_.printStore.state(); }
    void triggerPrint(bool stackGeneration) override;
    void browseLoadWav() override;
    void clearPrint() override;

    ui::ExportState exportState() const override { return processor_.resampler.state(); }
    void startWavExport() override;
    void dragExportedWav() override;

    std::string presetName() const override { return processor_.presets.currentName(); }
    std::vector<ui::PresetInfo> presets() const override { return processor_.presets.entries(); }
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

  private:
    using Snapshot = PluginProcessor::HistorySnapshot;
    Snapshot captureSnapshot() const;
    void applySnapshot(const Snapshot& snapshot);
    void recordUndoBoundary();

    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> params_{};
    juce::Component* dragSource_ = nullptr;
    TelemetrySnapshot visuals_{};
    std::array<float, PrintBuffer::kWaveformPoints> peaks_{};
    bool applyingSnapshot_ = false;
};

} // namespace ln
