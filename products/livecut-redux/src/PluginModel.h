// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "lc/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace livecut {

class PluginModel final : public lc::ui::Model {
  public:
    explicit PluginModel(LivecutAudioProcessor& processor);
    ~PluginModel() override;

    void setDragSource(juce::Component* source) { dragSource_ = source; }

    float getNormalised(std::string_view id) const override;
    float getReal(std::string_view id) const override;
    float defaultNormalised(std::string_view id) const override;
    void beginGesture(std::string_view id) override;
    void setNormalised(std::string_view id, float normalised) override;
    void endGesture(std::string_view id) override;
    std::string formatValue(std::string_view id) const override;
    std::vector<std::string> choices(std::string_view id) const override;
    bool isBool(std::string_view id) const override;

    std::vector<lc::ui::PresetEntry> presets() const override;
    std::string presetName() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override;

    bool canUndo() const override;
    bool canRedo() const override;
    void undo() override;
    void redo() override;
    bool isSlotB() const override;
    void toggleAB() override;
    void copyAB() override;

    void rollSeed() override;
    void requestCapture() override;
    void saveCapture() override;
    void beginDragCaptureWav() override;
    void applyRequestedMode() override;

    void refreshVisuals() override;
    const lc::ui::VisualState& visuals() const override { return visuals_; }

  private:
    juce::RangedAudioParameter* findParam(std::string_view id) const;

    LivecutAudioProcessor& processor_;
    juce::Component* dragSource_ = nullptr;
    std::unordered_map<std::string, juce::RangedAudioParameter*> params_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
    lc::ui::VisualState visuals_;
};

} // namespace livecut
