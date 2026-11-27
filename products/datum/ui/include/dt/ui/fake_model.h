// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dt/ui/model.h"

#include <string>
#include <vector>

namespace dt::ui {

class FakeModel final : public Model {
  public:
    FakeModel();

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    void refreshVisuals() override;
    const TelemetrySnapshot& visuals() override;
    const PreviewSnapshot& preview() override;

    hba::dsp::EnvelopeCurve pitchCurve() const override { return pitchCurve_; }
    hba::dsp::EnvelopeCurve ampCurve() const override { return ampCurve_; }
    void setPitchCurve(const hba::dsp::EnvelopeCurve& curve) override;
    void setAmpCurve(const hba::dsp::EnvelopeCurve& curve) override;

    ComposePattern pattern() const override { return pattern_; }
    void setPattern(const ComposePattern& pattern) override;

    void triggerAudition(int slotIndex = -1, float velocity = 0.96f) override;
    void applyArchetype(int archetypeIndex) override;
    void mutateKick() override;

    std::string clickSampleName() const override { return clickName_; }
    bool hasClickSample() const override { return !clickName_.empty(); }
    void browseClickSample() override {}
    void alignClickSample() override {}
    void clearClickSample() override { clickName_.clear(); }

    void matchSubLinkPhase() override;
    void tuneToSubLinkRoot() override;

    void setPresetName(std::string name) { presetName_ = std::move(name); }
    std::string presetName() const override { return presetName_; }
    std::vector<PresetInfo> presets() const override { return presets_; }
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override {}

    void setDemoSidechain(bool on);

  private:
    void rebuildPreview();

    Params params_ = Params::defaults();
    hba::dsp::EnvelopeCurve pitchCurve_ = hba::dsp::defaultKickPitchCurve();
    hba::dsp::EnvelopeCurve ampCurve_ = hba::dsp::defaultKickAmpCurve();
    ComposePattern pattern_ = defaultComposePattern();
    PreviewSnapshot preview_{};
    TelemetrySnapshot telemetry_{};
    std::string presetName_ = "Neuro Core 52";
    std::string clickName_;
    std::vector<PresetInfo> presets_;
    int presetIndex_ = 0;
    bool previewDirty_ = true;
    bool demoSidechain_ = false;
    std::uint32_t rng_ = 0x51a7f319u;
};

} // namespace dt::ui
