// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/ui/model.h"

namespace ln::ui {

class FakeModel final : public Model {
  public:
    FakeModel();

    float get(ParamId id) const override { return params_[id]; }
    void begin(ParamId) override {}
    void set(ParamId id, float real) override { params_[id] = clampToRange(spec(id), real); }
    void end(ParamId) override {}

    const TelemetrySnapshot& visuals() override { return visuals_; }

    Curve laneCurve(int lane) const override { return curves_[static_cast<std::size_t>(lane)]; }
    void setLaneCurve(int lane, const Curve& curve) override { curves_[static_cast<std::size_t>(lane)] = curve; }

    const std::array<float, PrintBuffer::kWaveformPoints>& printPeaks() const override { return peaks_; }
    std::string printLabel() const override { return label_; }
    int printGeneration() const override { return gen_; }
    PrintState printState() const override { return gen_ > 0 ? PrintState::ready : PrintState::idle; }
    void triggerPrint(bool stack) override {
        gen_ = stack ? (gen_ + 1) : 2;
        label_ = "GEN " + std::to_string(gen_) + " \xc2\xb7 F1 \xc2\xb7 2b";
        visuals_.hasPrint = true;
        visuals_.generation = gen_;
    }
    bool canStepBackPrint() const override { return gen_ > 2; }
    void stepBackPrint() override {
        if (gen_ > 2) {
            --gen_;
            label_ = "GEN " + std::to_string(gen_) + " \xc2\xb7 F1 \xc2\xb7 2b";
            visuals_.generation = gen_;
        }
    }
    void browseLoadWav() override {}
    void clearPrint() override {
        gen_ = 0;
        label_ = "Not printed";
        visuals_.hasPrint = false;
        visuals_.generation = 0;
        peaks_.fill(0.0f);
    }

    std::string presetName() const override { return presetName_; }
    void setPresetName(std::string name) { presetName_ = std::move(name); }
    std::vector<PresetInfo> presets() const override { return {{presetName_, "Reese"}}; }
    void loadPreset(int) override {}
    void stepPreset(int) override {}
    void savePreset() override {}

  private:
    Params params_ = Params::defaults();
    LaneCurves curves_ = defaultLaneCurves();
    TelemetrySnapshot visuals_{};
    std::array<float, PrintBuffer::kWaveformPoints> peaks_{};
    std::string label_ = "GEN 2 \xc2\xb7 F1 \xc2\xb7 2b";
    std::string presetName_ = "Velvet Strata";
    int gen_ = 2;
};

} // namespace ln::ui
