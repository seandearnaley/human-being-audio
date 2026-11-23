#pragma once

#include "pf/ui/model.h"

namespace pf::ui {

class FakeModel final : public Model {
public:
    FakeModel();

    float get(ParamId id) const override { return params_[id]; }
    void begin(ParamId) override {}
    void set(ParamId id, float real) override { params_[id] = clampToRange(spec(id), real); }
    void end(ParamId) override {}

    const Visuals& visuals() override { return visuals_; }

    Curve laneCurve(int lane) const override { return curves_[static_cast<std::size_t>(lane)]; }
    void setLaneCurve(int lane, const Curve& curve) override { curves_[static_cast<std::size_t>(lane)] = curve; }

    ImprintData imprint() const override { return imprint_; }
    std::string imprintLabel() const override { return imprint_.hasAny() ? "Imprinted" : "Imprint WAV"; }
    std::string imprintSlotLabel(int slot) const override {
        return imprint_.slotEnabled(slot) ? "Imprinted" : std::string();
    }
    void browseImprintAudio() override {}
    void clearImprint() override { imprint_ = {}; }
    void clearImprintSlot(int slot) override { imprint_.clearSlot(slot); }

    std::string presetName() const override { return "Neuro Chew"; }
    std::vector<PresetInfo> presets() const override { return { { "Neuro Chew", "Neuro" } }; }
    void loadPreset(int) override {}
    void stepPreset(int) override {}
    void savePreset() override {}

private:
    Params params_ = Params::defaults();
    LaneCurves curves_ = defaultLaneCurves();
    ImprintData imprint_ {};
    Visuals visuals_ {};
};

}
