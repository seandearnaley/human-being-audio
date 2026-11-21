// The interface's view of the plugin: parameters through the host, visuals from
// the processor's analysis, presets from disk.
#pragma once

#include "PluginProcessor.h"
#include "tf/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tf {

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

    std::string presetName() const override;
    std::vector<ui::PresetInfo> presets() const override;
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override;

    ui::ResampleState resampleState() const override;
    void resample() override;
    void dragResampledFile() override;

    // The component whose window file drags start from: the editor.
    void setDragSource(juce::Component* source) { dragSource_ = source; }

private:
    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> parameters_ {};
    ui::Visuals visuals_;
    juce::int64 lastUpdateTicks_ = 0;
    std::unique_ptr<juce::AlertWindow> saveDialog_;
    juce::Component* dragSource_ = nullptr;
};

}
