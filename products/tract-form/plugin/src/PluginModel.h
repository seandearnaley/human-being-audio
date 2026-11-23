// The interface's view of the plugin: parameters through the host, visuals from
// the processor's analysis, presets from disk.
#pragma once

#include "PluginProcessor.h"
#include "StudioHistory.h"
#include "tf/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace tf {

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

    std::string presetName() const override;
    std::vector<ui::PresetInfo> presets() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override;

    ui::ResampleState resampleState() const override;
    void resample() override;
    void dragResampledFile() override;

    bool canUndo() const override { return history_.canUndo(); }
    bool canRedo() const override { return history_.canRedo(); }
    void undo() override;
    void redo() override;
    bool isSlotB() const override { return history_.isSlotB(); }
    void toggleAB() override;
    void copyAB() override;

    // The component whose window file drags start from: the editor.
    void setDragSource(juce::Component* source) { dragSource_ = source; }

private:
  struct Snapshot {
      std::array<float, kParamCount> params{};
      LaneCurves curves = defaultLaneCurves();

      bool operator==(const Snapshot&) const = default;
  };

  Snapshot captureSnapshot() const;
  void applySnapshot(const Snapshot& snapshot);

  PluginProcessor& processor_;
  std::array<juce::RangedAudioParameter*, kParamCount> parameters_{};
  ui::Visuals visuals_;
  hba::studio::History<Snapshot> history_;
  bool applyingSnapshot_ = false;
  juce::int64 lastUpdateTicks_ = 0;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  std::unique_ptr<juce::AlertWindow> saveDialog_;
  juce::Component* dragSource_ = nullptr;
};

}
