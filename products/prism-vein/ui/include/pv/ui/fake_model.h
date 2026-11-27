// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "pv/ui/model.h"

namespace pv::ui {

// Offline / snapshot model that drives a real pv::Engine for authentic UI previews.
class FakeModel final : public Model {
  public:
    FakeModel();

    float get(ParamId id) const override { return params_[id]; }
    void begin(ParamId) override {}
    void set(ParamId id, float real) override;
    void end(ParamId) override {}

    std::uint16_t chordMask() const override { return chordMask_; }
    void setChordMask(std::uint16_t mask) override;

    hba::dsp::Curve laneCurve(int lane) const override;
    void setLaneCurve(int lane, const hba::dsp::Curve& curve) override;

    const TelemetrySnapshot& visuals() override;

    std::string presetName() const override { return presetName_; }
    std::vector<PresetEntry> presets() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override {}

    bool canUndo() const override { return false; }
    bool canRedo() const override { return false; }
    void undo() override {}
    void redo() override {}
    bool isSlotB() const override { return slotB_; }
    void toggleAB() override { slotB_ = !slotB_; }
    void copyAB() override {}

    void setPresetName(std::string name) { presetName_ = std::move(name); }
    void warmUpEngine(bool simulateSidechain = false, bool simulateMidi = false);

  private:
    Params params_ = Params::defaults();
    std::uint16_t chordMask_ = kDefaultChordMask;
    LaneCurves curves_ = defaultLaneCurves();
    std::string presetName_ = "Iridescent F Min9";
    bool slotB_ = false;
    Engine engine_{};
    TelemetrySnapshot telemetry_{};
};

} // namespace pv::ui
