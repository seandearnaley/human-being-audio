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

    PhraseOverrides overrides() const override { return overrides_; }
    void setOverrides(const PhraseOverrides& ov) override {
        overrides_ = ov;
        recompile();
    }
    void clearOverrides() override {
        overrides_.clear();
        recompile();
    }

    void rollSeed() override;
    bool canStepSeedHistory(int delta) const override;
    void stepSeedHistory(int delta) override;

    void dragMidiPhrase() override {}
    std::string exportStatus() const override { return "Drag 4-bar .mid"; }

    std::string presetName() const override { return presetName_; }
    std::vector<PresetInfo> presets() const override { return {{"Neuro Whip", "Neuro"}, {"Amen Ghosts", "Breaks"}}; }
    void loadPreset(int) override {}
    void stepPreset(int) override {}
    void savePreset() override {}

  private:
    void recompile();

    Params params_;
    LaneCurves curves_ = defaultLaneCurves();
    PhraseOverrides overrides_{};
    Visuals visuals_;
    std::vector<int> seedHistory_{407};
    std::size_t seedCursor_ = 0;
    std::string presetName_ = "Neuro Whip";
};

} // namespace cl::ui
