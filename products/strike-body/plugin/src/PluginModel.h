// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "sb/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <random>

namespace sb {

class PluginModel final : public ui::Model {
  public:
    explicit PluginModel(PluginProcessor& processor);

    void setDragSource(juce::Component* source) { dragSource_ = source; }

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    void triggerAudition(Articulation art, float velocity = 0.96f) override;
    void runAutoAlign() override;
    void loadArchetype(ui::Archetype kind) override;
    void mutateSound() override;

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

    ui::BounceState bounceState() const override { return processor_.bouncer.state(); }
    void startBounce() override;
    void dragBouncedWav() override;

    void refreshVisuals() override { visuals_ = processor_.telemetry.read(); }
    const TelemetrySnapshot& visuals() const override { return visuals_; }

  private:
    using Snapshot = PluginProcessor::HistorySnapshot;
    Snapshot captureSnapshot() const;
    void applySnapshot(const Snapshot& snapshot);
    void recordUndoBoundary();
    void commitParam(ParamId id, float real);
    void applyParamsBatch(const Params& next, const std::string& label);

    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> params_{};
    juce::Component* dragSource_ = nullptr;
    TelemetrySnapshot visuals_{};
    bool applyingSnapshot_ = false;
    std::mt19937 rng_;
};

} // namespace sb
