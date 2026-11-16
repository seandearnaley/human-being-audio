// The interface's view of the plugin: parameters through the host, visuals from
// the processor's analysis, presets from disk.
#pragma once

#include "PluginProcessor.h"
#include "tf/ui/model.h"

namespace tf {

class PluginModel final : public ui::Model {
public:
    explicit PluginModel(PluginProcessor& processor);

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    const ui::Visuals& visuals() override;

    std::string presetName() const override;
    std::vector<std::string> presetNames() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override;

    ui::ResampleState resampleState() const override;
    void resample() override;
    void dragResampledFile() override;

private:
    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> parameters_ {};
    ui::Visuals visuals_;
};

}
