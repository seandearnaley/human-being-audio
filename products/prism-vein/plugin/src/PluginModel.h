// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "pv/ui/model.h"

#include <array>

namespace pv {

class PluginModel final : public ui::Model {
  public:
    explicit PluginModel(PluginProcessor& processor);

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    std::uint16_t chordMask() const override { return processor_.stateStore.chordMask(); }
    void setChordMask(std::uint16_t mask) override;

    hba::dsp::Curve laneCurve(int lane) const override { return processor_.stateStore.laneCurve(lane); }
    void setLaneCurve(int lane, const hba::dsp::Curve& curve) override;

    const TelemetrySnapshot& visuals() override { return visuals_; }
    void refreshVisuals() override { visuals_ = processor_.telemetry.read(); }

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

  private:
    using Snapshot = PluginProcessor::HistorySnapshot;

    Snapshot captureSnapshot() const;
    void applySnapshot(const Snapshot& snapshot);
    void recordUndoBoundary();

    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> params_{};
    TelemetrySnapshot visuals_{};
    bool applyingSnapshot_ = false;
};

} // namespace pv
