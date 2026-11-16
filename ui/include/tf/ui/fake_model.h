// A model with canned, deterministic data, for headless snapshots and tests.
#pragma once

#include "tf/ui/model.h"

namespace tf::ui {

class FakeModel final : public Model {
public:
    // `time` in seconds positions the lanes and the beating, so a snapshot is repeatable.
    explicit FakeModel(double time = 0.37);

    float get(ParamId id) const override { return params_[id]; }
    void begin(ParamId) override {}
    void set(ParamId id, float real) override { params_[id] = real; }
    void end(ParamId) override {}

    const Visuals& visuals() override;

    std::string presetName() const override { return "Coupled Reese 01"; }
    std::vector<PresetInfo> presets() const override {
        return { { "Coupled Reese 01", "Reese" }, { "Talking Sub", "Talk" }, { "Glass Jaw", "Neuro" } };
    }
    void loadPreset(int) override {}
    void stepPreset(int) override {}
    void savePreset() override {}

    ResampleState resampleState() const override { return ResampleState::idle; }
    void resample() override {}
    void dragResampledFile() override {}

private:
    Params params_ = Params::defaults();
    Visuals visuals_;
    double time_;
};

}
