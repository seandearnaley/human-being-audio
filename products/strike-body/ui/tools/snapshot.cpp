// SPDX-License-Identifier: AGPL-3.0-or-later
// Renders a live headless PNG screenshot of Strike Body v2's Visage interface after firing a real neuro snare hit!
#include "sb/engine.h"
#include "sb/ui/editor.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
class SnapshotModel final : public sb::ui::Model {
  public:
    SnapshotModel() {
        engine_.prepare(48000.0, 256, 4);
        params_ = sb::Params::defaults();
        params_.set(sb::ParamId::bodyHz, 185.0f);
        params_.set(sb::ParamId::drive, 44.0f);
        params_.set(sb::ParamId::disperserAmt, 36.0f);
        params_.set(sb::ParamId::macroPunch, 24.0f);
        params_.set(sb::ParamId::macroTight, 12.0f);
        params_.set(sb::ParamId::macroMetal, 18.0f);
        params_.set(sb::ParamId::macroSmack, 28.0f);
        params_.set(sb::ParamId::scoopAmt, 25.0f);
        params_.set(sb::ParamId::roomAmt, 18.0f);

        const auto align = sb::solveAutoAlign(params_, 48000.0);
        params_.set(sb::ParamId::bodyPhase, align.bodyPhaseDeg);
        params_.set(sb::ParamId::shellOffsetMs, align.shellOffsetMs);
        params_.set(sb::ParamId::shellInvert, align.shellInvert ? 1.0f : 0.0f);
        params_.set(sb::ParamId::wireOffsetMs, align.wireOffsetMs);
        params_.set(sb::ParamId::wireInvert, align.wireInvert ? 1.0f : 0.0f);

        engine_.process(params_, nullptr, nullptr, 0);
        engine_.trigger(sb::Articulation::main, 1.0f, 1.0f);
        std::vector<float> l(256), r(256);
        for (int b = 0; b < 11; ++b)
            engine_.process(params_, l.data(), r.data(), 256);
        vis_ = engine_.telemetry();
        engine_.trigger(sb::Articulation::main, 1.0f, 1.0f);
        engine_.process(params_, l.data(), r.data(), 256);
        vis_.modeEnergy = engine_.telemetry().modeEnergy;
        vis_.peakL = 0.88f;
        vis_.peakR = 0.86f;
        vis_.gainReductionDb = 1.6f;
    }

    float get(sb::ParamId id) const override { return params_.real(id); }
    void begin(sb::ParamId) override {}
    void set(sb::ParamId id, float v) override { params_.set(id, v); }
    void end(sb::ParamId) override {}
    void triggerAudition(sb::Articulation, float) override {}
    void runAutoAlign() override {}
    void loadArchetype(sb::ui::Archetype) override {}
    void mutateSound() override {}
    std::string presetName() const override { return "Neuro Crack 185Hz"; }
    std::vector<sb::ui::PresetEntry> presets() const override { return {{"Neuro Crack 185Hz", "Snare"}}; }
    void loadPreset(int) override {}
    void stepPreset(int) override {}
    void savePreset() override {}
    bool canUndo() const override { return true; }
    bool canRedo() const override { return false; }
    void undo() override {}
    void redo() override {}
    bool isSlotB() const override { return false; }
    void toggleAB() override {}
    void copyAB() override {}
    sb::ui::BounceState bounceState() const override { return sb::ui::BounceState::ready; }
    void startBounce() override {}
    void dragBouncedWav() override {}
    void refreshVisuals() override {}
    const sb::TelemetrySnapshot& visuals() const override { return vis_; }

  private:
    sb::Params params_;
    sb::Engine engine_;
    sb::TelemetrySnapshot vis_;
};
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: sb-ui-snapshot <out.png>\n";
        return 2;
    }
    SnapshotModel model;
    sb::ui::Editor editor(model);
    editor.renderToPng(argv[1]);
    std::cout << "Saved Strike Body UI snapshot -> " << argv[1] << "\n";
    return 0;
}
