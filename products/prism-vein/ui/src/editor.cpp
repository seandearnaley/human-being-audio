// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pv/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>
#include <hba/ui/voice_bar.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace pv::ui {

namespace {
constexpr float kM = metric::margin;
constexpr float kW = kDefaultWidth - 2.0f * kM;
constexpr hba::ui::DesignRect kTopBar{kM, 10.0f, kW, 50.0f};
constexpr hba::ui::DesignRect kWorkspace{kM, 68.0f, kW, 644.0f};
constexpr hba::ui::DesignRect kVoiceBar{kM, 720.0f, kW, hba::ui::kVoiceBarHeight};

using VoiceBar = hba::ui::StudioVoiceBar<Context, ParamFrame, ParamId, ParamKind>;

class WorkspaceFrame final : public visage::Frame {
  public:
    WorkspaceFrame(Context& ctx, std::unique_ptr<visage::Frame> stage, std::unique_ptr<visage::Frame> color,
                   std::unique_ptr<visage::Frame> lattice)
        : context_(ctx), stage_(std::move(stage)), color_(std::move(color)), lattice_(std::move(lattice)) {
        addChild(stage_.get());
        addChild(color_.get());
        addChild(lattice_.get());
        setView(Editor::View::color);
    }

    void setView(Editor::View v) {
        view_ = v;
        color_->setVisible(v == Editor::View::color);
        lattice_->setVisible(v == Editor::View::lattice);
        layoutChildren();
    }

    void resized() override { layoutChildren(); }

  private:
    void layoutChildren() {
        const auto px = [this](float v) { return context_.px(v); };
        const float gap = px(metric::gap);
        const float w = width();
        const float h = height();

        if (view_ == Editor::View::color) {
            const float stageH = px(434.0f);
            stage_->setBounds(0.0f, 0.0f, w, stageH);
            color_->setBounds(0.0f, stageH + gap, w, std::max(px(120.0f), h - stageH - gap));
        } else {
            const float stageH = px(206.0f);
            stage_->setBounds(0.0f, 0.0f, w, stageH);
            lattice_->setBounds(0.0f, stageH + gap, w, std::max(px(280.0f), h - stageH - gap));
        }
    }

    Context& context_;
    Editor::View view_ = Editor::View::color;
    std::unique_ptr<visage::Frame> stage_, color_, lattice_;
};

} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)),
          topBar(context, std::move(showAbout), [this](Editor::View v) { workspace.setView(v); }),
          workspace(context, makeStagePanel(context), makeColorPanel(context), makeLatticePanel(context)),
          voiceBar(context, colour::prism, "VOICE",
                   {
                       {ParamId::pitchSource, "Source", colour::prism, 152.0f, false, false},
                       {ParamId::tuningMode, "Tuning", colour::harmonic, 126.0f, false, false},
                       {ParamId::latencyMode, "Latency", colour::tame, 154.0f, false, false},
                       {ParamId::subMono, "Sub Mono", colour::sub, 118.0f, false, false},
                       {ParamId::quality, "OS", colour::prism, 74.0f, false, true},
                   },
                   [this]() -> std::string {
                       const auto& tel = context.model.visuals();
                       if (tel.midiWaiting)
                           return "WAIT";
                       return tel.midiActive ? "MIDI" : describeChordMask(context.model.chordMask());
                   }) {
        host.onKeyPress = [this](const visage::KeyEvent& key) {
            if (key.keyCode() == visage::KeyCode::Number1 && !key.isMainModifier()) {
                topBar.setView(Editor::View::color);
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Number2 && !key.isMainModifier()) {
                topBar.setView(Editor::View::lattice);
                return true;
            }
            return false;
        };

        host.addPlaced(&topBar, kTopBar);
        host.addPlaced(&workspace, kWorkspace);
        host.addPlaced(&voiceBar, kVoiceBar);
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    WorkspaceFrame workspace;
    VoiceBar voiceBar;
};

Editor::Editor(Model& model, std::function<void()> showAbout)
    : impl_(std::make_unique<Impl>(model, std::move(showAbout))) {}

Editor::~Editor() { detach(); }
void Editor::attach(void* parentView, int width, int height) { impl_->host.attach(parentView, width, height); }
void Editor::detach() { impl_->host.detach(); }
void Editor::setSize(int width, int height) { impl_->host.setSize(width, height); }
void Editor::setView(View view) { impl_->topBar.setView(view); }

#if defined(__linux__)
int Editor::eventFd() const { return impl_->host.eventFd(); }
void Editor::dispatchEvents() { impl_->host.dispatchEvents(); }
#endif

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->host.renderToPng(path, width, height, true);
}

} // namespace pv::ui
