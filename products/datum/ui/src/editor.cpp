// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dt/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>
#include <hba/ui/voice_bar.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace dt::ui {

namespace {
constexpr float kM = metric::margin;
constexpr float kW = kDefaultWidth - 2.0f * kM;
constexpr hba::ui::DesignRect kTopBar{kM, 10.0f, kW, 50.0f};
constexpr hba::ui::DesignRect kWorkspace{kM, 68.0f, kW, 644.0f};
constexpr hba::ui::DesignRect kVoiceBar{kM, 720.0f, kW, hba::ui::kVoiceBarHeight};

using VoiceBar = hba::ui::StudioVoiceBar<Context, ParamFrame, ParamId, ParamKind>;

// Hosts the adaptive StagePanel + mutually exclusive SCULPT / ANATOMY / COMPOSE surfaces.
class WorkspaceFrame final : public visage::Frame {
  public:
    WorkspaceFrame(Context& ctx, std::unique_ptr<visage::Frame> stage, std::unique_ptr<visage::Frame> sculpt,
                   std::unique_ptr<visage::Frame> anatomy, std::unique_ptr<visage::Frame> compose)
        : context_(ctx), stage_(std::move(stage)), sculpt_(std::move(sculpt)), anatomy_(std::move(anatomy)),
          compose_(std::move(compose)) {
        addChild(stage_.get());
        addChild(sculpt_.get());
        addChild(anatomy_.get());
        addChild(compose_.get());
        setView(Editor::View::sculpt);
    }

    void setView(Editor::View v) {
        view_ = v;
        sculpt_->setVisible(v == Editor::View::sculpt);
        anatomy_->setVisible(v == Editor::View::anatomy);
        compose_->setVisible(v == Editor::View::compose);
        layoutChildren();
    }

    void resized() override { layoutChildren(); }

  private:
    void layoutChildren() {
        const auto px = [this](float v) { return context_.px(v); };
        const float gap = px(metric::gap);
        const float w = width();
        const float h = height();

        if (view_ == Editor::View::sculpt) {
            const float stageH = px(468.0f);
            stage_->setBounds(0.0f, 0.0f, w, stageH);
            sculpt_->setBounds(0.0f, stageH + gap, w, std::max(px(80.0f), h - stageH - gap));
        } else if (view_ == Editor::View::anatomy) {
            const float stageH = px(236.0f);
            stage_->setBounds(0.0f, 0.0f, w, stageH);
            anatomy_->setBounds(0.0f, stageH + gap, w, std::max(px(120.0f), h - stageH - gap));
        } else {
            const float stageH = px(218.0f);
            stage_->setBounds(0.0f, 0.0f, w, stageH);
            compose_->setBounds(0.0f, stageH + gap, w, std::max(px(120.0f), h - stageH - gap));
        }
    }

    Context& context_;
    Editor::View view_ = Editor::View::sculpt;
    std::unique_ptr<visage::Frame> stage_, sculpt_, anatomy_, compose_;
};

} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)),
          topBar(context, std::move(showAbout), [this](Editor::View v) { workspace.setView(v); }),
          workspace(context, makeStagePanel(context), makeSculptPanel(context), makeAnatomyPanel(context),
                    makeComposePanel(context)),
          voiceBar(context, colour::sub, "VOICE",
                   {
                       {ParamId::keyTrack, "Track", colour::sub, 132.0f, false, false},
                       {ParamId::chokeMode, "Choke", colour::impact, 146.0f, false, false},
                       {ParamId::zeroCross, "Zero-X", colour::sub, 102.0f, false, false},
                       {ParamId::headroomMode, "Headroom", colour::body, 168.0f, false, false},
                       {ParamId::quality, "OS", colour::sub, 76.0f, false, true},
                   },
                   [this]() -> std::string {
                       const float hz = context.model.get(ParamId::tuneHz);
                       const int slot = std::clamp(
                           static_cast<int>(std::lround(context.model.get(ParamId::activeSlot))), 0, kKitSlots - 1);
                       const char slotCh = static_cast<char>('A' + slot);
                       return noteNameForHz(hz) + " \xc2\xb7 " + std::string(1, slotCh);
                   }) {
        host.onKeyPress = [this](const visage::KeyEvent& key) {
            // D auditions active kick slot (leaving Space free for Ableton Live transport!)
            if (key.keyCode() == visage::KeyCode::D && !key.isMainModifier()) {
                context.model.triggerAudition(-1, 0.96f);
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Number1 && !key.isMainModifier()) {
                topBar.setView(Editor::View::sculpt);
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Number2 && !key.isMainModifier()) {
                topBar.setView(Editor::View::anatomy);
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Number3 && !key.isMainModifier()) {
                topBar.setView(Editor::View::compose);
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

} // namespace dt::ui
