// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/ui/model.h"

namespace cl::ui {

class FakeModel final : public Model {
  public:
    FakeModel();

    float get(ParamId id) const override { return params_[id]; }
    void begin(ParamId) override {}
    void set(ParamId id, float real) override;
    void end(ParamId) override {}

    const Visuals& visuals() override;

    Curve laneCurve(int lane) const override { return curves_[static_cast<std::size_t>(lane)]; }
    void setLaneCurve(int lane, const Curve& curve) override { curves_[static_cast<std::size_t>(lane)] = curve; }

    const GrooveState& groove() const override { return groove_; }
    void setGrooveBank(const GrooveBank& bank) override {
        groove_ = GrooveState(bank);
        refresh();
    }

    // For screenshots: what the snapshot tool can ask the fake model to show.
    void showBars(int bars); // resize scene A to 1, 2 or 4 bars
    void showLive();         // the last bar with its fill live, and a one-beat Repeat window held
    void showSlicer();       // MIDI Slicer mode

    void cloneActiveSceneToNext() override;
    void clearActiveScene() override;

    void dragMidiPhrase() override {}
    std::string exportStatus() const override { return "Drag 2-bar .mid"; }

    std::string presetName() const override { return presetName_; }
    std::vector<PresetInfo> presets() const override {
        return {{"Amen", "Breaks"}, {"Neuro", "Neuro"}, {"Slicer: UKG Chord Stabs", "MIDI Slicer"}};
    }
    void loadPreset(int) override {}
    void stepPreset(int) override {}
    void savePreset() override {}

  private:
    void refresh();

    Params params_;
    LaneCurves curves_ = defaultLaneCurves();
    GrooveState groove_{};
    Visuals visuals_;
    bool live_ = false;
    std::string presetName_ = "Amen";
};

} // namespace cl::ui
