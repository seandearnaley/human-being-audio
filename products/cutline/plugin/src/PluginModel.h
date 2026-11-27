// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "StudioHistory.h"
#include "cl/phrase.h"
#include "cl/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <memory>

namespace cl {

class PluginModel final : public ui::Model {
  public:
    explicit PluginModel(PluginProcessor& processor);
    ~PluginModel() override;

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    void refreshVisuals() override;
    const ui::Visuals& visuals() override;

    Curve laneCurve(int lane) const override;
    void setLaneCurve(int lane, const Curve& curve) override;

    const GrooveState& groove() const override;
    void setGrooveBank(const GrooveBank& bank) override;

    void cloneActiveSceneToNext() override;
    void clearActiveScene() override;

    void dragMidiPhrase() override;
    std::string exportStatus() const override;

    std::string presetName() const override;
    std::vector<ui::PresetInfo> presets() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override;

    bool canUndo() const override { return processor_.history.canUndo(); }
    bool canRedo() const override { return processor_.history.canRedo(); }
    void undo() override;
    void redo() override;
    bool isSlotB() const override { return processor_.history.isSlotB(); }
    void toggleAB() override;
    void copyAB() override;

    void setDragSource(juce::Component* source) { dragSource_ = source; }

  private:
    using Snapshot = PluginProcessor::HistorySnapshot;

    Snapshot captureSnapshot() const;
    void applySnapshot(const Snapshot& snapshot);

    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> parameters_{};
    ui::Visuals visuals_{};
    bool applyingSnapshot_ = false;
    mutable GrooveState groove_{};
    mutable std::uint32_t grooveRevision_ = 0xffffffffu; // never equal to the store's first revision
    SlicerPlan previewPlan_{};                           // the slicer phrase shown while the transport is stopped
    SlicerInputs previewInputs_{};                       // what previewPlan_ was compiled from
    std::unique_ptr<juce::AlertWindow> saveDialog_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    juce::Component* dragSource_ = nullptr;
};

} // namespace cl
