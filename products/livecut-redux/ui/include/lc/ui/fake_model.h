// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lc/ui/model.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lc::ui {

class FakeModel final : public Model {
  public:
    FakeModel();

    float getNormalised(std::string_view id) const override;
    float getReal(std::string_view id) const override;
    float defaultNormalised(std::string_view id) const override;
    void beginGesture(std::string_view id) override;
    void setNormalised(std::string_view id, float normalised) override;
    void endGesture(std::string_view id) override;
    std::string formatValue(std::string_view id) const override;
    std::vector<std::string> choices(std::string_view id) const override;
    bool isBool(std::string_view id) const override;

    std::vector<PresetEntry> presets() const override { return presets_; }
    std::string presetName() const override { return currentPreset_; }
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override {}

    bool canUndo() const override { return undoDepth_ > 0; }
    bool canRedo() const override { return redoDepth_ > 0; }
    void undo() override;
    void redo() override;
    bool isSlotB() const override { return slotB_; }
    void toggleAB() override { slotB_ = !slotB_; }
    void copyAB() override {}

    void rollSeed() override;
    void requestCapture() override;
    void saveCapture() override {}
    void beginDragCaptureWav() override {}
    void applyRequestedMode() override;

    void refreshVisuals() override;
    const VisualState& visuals() const override { return visuals_; }

    // Helpers for deterministic headless snapshots
    void setReal(std::string_view id, float value);
    void populateDemoTelemetry();

  private:
    struct Spec {
        float min = 0.0f;
        float max = 1.0f;
        float def = 0.0f;
        float value = 0.0f;
        bool isBool = false;
        bool isInt = false;
        std::string unit;
        std::vector<std::string> choices;
    };

    void addFloat(std::string id, float min, float max, float def, std::string unit = {});
    void addInt(std::string id, int min, int max, int def, std::string unit = {});
    void addBool(std::string id, bool def);
    void addChoice(std::string id, std::vector<std::string> choices, int def = 0);

    std::unordered_map<std::string, Spec> specs_;
    std::vector<PresetEntry> presets_;
    std::string currentPreset_ = "Amen Instant";
    int presetIndex_ = 0;
    int undoDepth_ = 1;
    int redoDepth_ = 0;
    bool slotB_ = false;
    VisualState visuals_;
};

} // namespace lc::ui
