// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/ui/editor.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
class FakeModel final : public tg::ui::Model {
  public:
    explicit FakeModel(int mode) {
        params_ = tg::Params::defaults();
        params_.set(tg::ParamId::mode, static_cast<float>(mode));
        if (mode == 1) {
            params_.set(tg::ParamId::gravity, 65.0f);
            params_.set(tg::ParamId::stagger, 52.0f);
            presetName_ = "Phrygian Slide Table";
            matrix_ = tg::matrixForPack(0);
        } else if (mode == 2) {
            params_.set(tg::ParamId::voicingPack, 6.0f);
            params_.set(tg::ParamId::gravity, 64.0f);
            presetName_ = "Mobius Dim7 Rise 4Bar";
            matrix_ = tg::matrixForPack(6);
        } else {
            presetName_ = "Sidewinder F Bass 4Bar";
            matrix_ = tg::matrixForPack(0);
        }
        tel_.mode = mode;
        tel_.barPhase = 0.37f;
        tel_.spiralPos = 1.37f;
        tel_.morphPos = 0.42f;
        tel_.lane1Phase = 0.37f;
        tel_.lane2Phase = 0.62f;
        tel_.barNumber = 2;
        tel_.beatNumber = 3;
        tel_.bpm = 172.0f;
        tel_.sounding = true;
        tel_.peakL = 0.72f;
        tel_.peakR = 0.68f;
        tel_.subPeak = 0.84f;
        tel_.rmsLevel = 0.48f;
        tel_.limiterGain = 0.92f;
        tel_.effectiveBellHz = 320.0f;
        tel_.heldNoteCount = 3;
        tel_.heldNotes = {41, 48, 54, 0, 0};
    }

    float get(tg::ParamId id) const override { return params_.real(id); }
    void begin(tg::ParamId) override {}
    void set(tg::ParamId id, float real) override { params_.set(id, real); }
    void end(tg::ParamId) override {}
    hba::dsp::Curve curve(int lane) const override { return curves_[static_cast<std::size_t>(lane)]; }
    void setCurve(int lane, const hba::dsp::Curve& next) override { curves_[static_cast<std::size_t>(lane)] = next; }
    tg::SceneMatrix sceneMatrix() const override { return matrix_; }
    void setSceneMatrix(const tg::SceneMatrix& next) override { matrix_ = next; }
    void optimizeVoiceLeading() override {}
    void resetSceneMatrix() override {}
    bool captureHeldChord(int) override { return true; }
    void jumpToScene(int) override {}
    std::string presetName() const override { return presetName_; }
    std::vector<tg::ui::PresetEntry> presets() const override { return {{presetName_, "Spiral"}}; }
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
    bool isAuditioning() const override { return true; }
    void setAudition(bool) override {}
    tg::ui::ResampleState resampleState() const override { return tg::ui::ResampleState::ready; }
    void startResample() override {}
    void dragResampledWav() override {}
    void dragExportedMidi() override {}
    void refreshVisuals() override {}
    const tg::TelemetrySnapshot& visuals() const override { return tel_; }

  private:
    tg::Params params_;
    tg::LaneCurves curves_ = tg::defaultLaneCurves();
    tg::SceneMatrix matrix_;
    std::string presetName_;
    tg::TelemetrySnapshot tel_{};
};
} // namespace

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "torus-glide-snapshot.png";
    const int mode = argc > 2 ? std::atoi(argv[2]) : 0;
    FakeModel model(mode);
    tg::ui::Editor editor(model);
    editor.renderToPng(out, tg::ui::kDefaultWidth, tg::ui::kDefaultHeight);
    std::cout << "Saved " << out << " (mode=" << mode << ")\n";
    return 0;
}
