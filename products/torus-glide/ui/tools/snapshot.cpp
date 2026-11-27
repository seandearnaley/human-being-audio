// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/ui/editor.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
class FakeModel final : public tg::ui::Model {
  public:
    FakeModel(int mode, int padProgram) {
        params_ = tg::Params::defaults();
        params_.set(tg::ParamId::mode, static_cast<float>(mode));
        params_.set(tg::ParamId::padProgram, static_cast<float>(padProgram));
        params_.set(tg::ParamId::padX, 62.0f);
        params_.set(tg::ParamId::padY, 68.0f);
        params_.set(tg::ParamId::padHold, 1.0f);

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
        tel_.padActive = true;
        tel_.padAmount = 1.0f;
        tel_.spiralVel = 1.48f;
        tel_.peakL = 0.72f;
        tel_.peakR = 0.68f;
        tel_.subPeak = 0.84f;
        tel_.rmsLevel = 0.48f;
        tel_.limiterGain = 0.92f;
        tel_.effectiveBellHz = 360.0f;
        tel_.effectiveFilterHz = 2850.0f;
        tel_.effectiveSpace = 0.46f;
        tel_.effectiveDrive = 0.54f;
        tel_.effectiveSubAnchor = 0.86f;
        tel_.voicePitches = {41.0f, 48.0f, 54.0f, 56.0f, 63.0f};
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
    bool isAuditioning() const override { return params_.flag(tg::ParamId::padHold); }
    void setAudition(bool a) override { params_.set(tg::ParamId::padHold, a ? 1.0f : 0.0f); }
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
    // Usage: tg-ui-snapshot <out.png> [padProgram=0..5 | --studio] [mode=0..2] [width=1280] [height=760]
    const std::string out = argc > 1 ? argv[1] : "torus-glide-snapshot.png";
    bool studio = false;
    int padProg = 0;
    int mode = 0;
    if (argc > 2) {
        const std::string arg2 = argv[2];
        if (arg2 == "--studio" || arg2 == "studio") {
            studio = true;
            mode = argc > 3 ? std::clamp(std::atoi(argv[3]), 0, 2) : 0;
        } else {
            padProg = std::clamp(std::atoi(argv[2]), 0, tg::kPadProgramCount - 1);
            mode = argc > 3 ? std::clamp(std::atoi(argv[3]), 0, 2) : (padProg == 1 ? 2 : (padProg == 2 ? 1 : 0));
        }
    }
    const int w = argc > 4 ? std::max(640, std::atoi(argv[4])) : tg::ui::kDefaultWidth;
    const int h = argc > 5 ? std::max(380, std::atoi(argv[5])) : tg::ui::kDefaultHeight;

    FakeModel model(mode, padProg);
    tg::ui::Editor editor(model);
    if (studio)
        editor.setView(tg::ui::Editor::View::studio);
    editor.renderToPng(out, w, h);
    std::cout << "Saved " << out << " (" << (studio ? "studio" : "play") << ", pad=" << padProg << ", mode=" << mode
              << ", " << w << "x" << h << ")\n";
    return 0;
}
