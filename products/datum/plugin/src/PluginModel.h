// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "dt/ui/model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>

namespace dt {

class PluginModel final : public ui::Model {
  public:
    explicit PluginModel(PluginProcessor& processor);

    void setDragSource(juce::Component* source) { dragSource_ = source; }

    float get(ParamId id) const override;
    void begin(ParamId id) override;
    void set(ParamId id, float real) override;
    void end(ParamId id) override;

    void refreshVisuals() override;
    const TelemetrySnapshot& visuals() override { return visuals_; }
    const PreviewSnapshot& preview() override;

    hba::dsp::EnvelopeCurve pitchCurve() const override { return processor_.stateStore.pitchCurve(); }
    hba::dsp::EnvelopeCurve ampCurve() const override { return processor_.stateStore.ampCurve(); }
    void setPitchCurve(const hba::dsp::EnvelopeCurve& curve) override;
    void setAmpCurve(const hba::dsp::EnvelopeCurve& curve) override;

    ComposePattern pattern() const override { return processor_.stateStore.pattern(); }
    void setPattern(const ComposePattern& pattern) override;

    void triggerAudition(int slotIndex = -1, float velocity = 0.96f) override;
    void applyArchetype(int archetypeIndex) override;
    void mutateKick() override;

    std::string clickSampleName() const override { return processor_.clickStore.name(); }
    bool hasClickSample() const override { return processor_.clickStore.hasSample(); }
    void browseClickSample() override;
    void alignClickSample() override;
    void clearClickSample() override;

    void matchSubLinkPhase() override;
    void tuneToSubLinkRoot() override;

    ui::ExportState wavExportState() const override { return processor_.resampler.state(); }
    void startWavExport(bool patternLoop) override;
    void dragExportedWav() override;
    void dragPatternMidi() override;

    std::string presetName() const override { return processor_.presets.currentName(); }
    std::vector<ui::PresetInfo> presets() const override { return processor_.presets.entries(); }
    void loadPreset(int index) override;
    void stepPreset(int delta) override;
    void savePreset() override { processor_.presets.saveUserPresetAsync(); }

    bool canUndo() const override { return processor_.history.canUndo(); }
    bool canRedo() const override { return processor_.history.canRedo(); }
    void undo() override;
    void redo() override;
    bool isSlotB() const override { return processor_.history.isSlotB(); }
    void toggleAB() override;
    void copyAB() override;

  private:
    using Snapshot = PluginProcessor::HistorySnapshot;
    Snapshot captureSnapshot() const;
    void applySnapshot(const Snapshot& snapshot);
    void recordUndoBoundary();
    void updatePreviewIfNeeded();

    PluginProcessor& processor_;
    std::array<juce::RangedAudioParameter*, kParamCount> params_{};
    juce::Component* dragSource_ = nullptr;
    TelemetrySnapshot visuals_{};
    PreviewSnapshot preview_{};

    // Change-detection cache for zero-waste preview rendering
    Params cachedParams_{};
    AudioStateBundle cachedBundle_{};
    std::uint64_t cachedClickVer_ = ~0ull;
    bool previewValid_ = false;
    bool applyingSnapshot_ = false;
    std::uint32_t rng_ = 0x73c91a4bu;
};

} // namespace dt
