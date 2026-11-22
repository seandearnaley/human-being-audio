#pragma once

#include "PluginProcessor.h"
#include "pf/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace pf {

class PluginModel final : public ui::Model {
public:
    explicit PluginModel(PluginProcessor& processor);
    ~PluginModel() override;

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    const ui::Visuals& visuals() override;

    Curve laneCurve(int lane) const override;
    void setLaneCurve(int lane, const Curve& curve) override;

    ImprintData imprint() const override;
    std::string imprintLabel() const override;
    void browseImprintAudio() override;
    void clearImprint() override;

    std::string presetName() const override;
    std::vector<ui::PresetInfo> presets() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override;

private:
    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> parameters_ {};
    ui::Visuals visuals_ {};
    juce::int64 lastUpdateTicks_ = 0;
    std::unique_ptr<juce::AlertWindow> saveDialog_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
};

}
