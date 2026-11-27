// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sb/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>
#include <hba/ui/voice_bar.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace sb::ui {

namespace {
constexpr float kM = metric::margin;
constexpr float kW = kDefaultWidth - 2.0f * kM;
constexpr hba::ui::DesignRect kTopBar{kM, 10.0f, kW, 54.0f};
constexpr hba::ui::DesignRect kStage{kM, 72.0f, kW, 246.0f};
constexpr hba::ui::DesignRect kMacro{kM, 326.0f, kW, 114.0f};
constexpr hba::ui::DesignRect kRack{kM, 448.0f, kW, 362.0f};
constexpr hba::ui::DesignRect kVoiceBar{kM, 818.0f, kW, hba::ui::kVoiceBarHeight};

using VoiceBar = hba::ui::StudioVoiceBar<Context, ParamFrame, ParamId, ParamKind>;
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)), topBar(context, std::move(showAbout)),
          stage(makeStagePanel(context)), macro(makeMacroPanel(context)), rack(makeRackPanel(context)),
          voiceBar(context, colour::body, "VOICE",
                   {
                       {ParamId::chokeMode, "Mode", colour::body, 148.0f, false, false},
                       {ParamId::keyTrack, "Key", colour::shell, 136.0f, false, false},
                       {ParamId::subMono, "Sub Mono", colour::locked, 104.0f, false, false},
                       {ParamId::quality, "OS", colour::body, 116.0f, false, true},
                   },
                   [this]() -> std::string {
                       const int mode =
                           std::clamp(static_cast<int>(std::lround(context.model.get(ParamId::chokeMode))), 0, 2);
                       return mode == 1 ? "Poly 4v" : (mode == 2 ? "Mono" : "Split");
                   }) {
        host.onDrawBackground = [](visage::Canvas& canvas, float w, float h) {
            canvas.setBrush(visage::Brush::vertical(colour::obsidianBgTop, colour::obsidianBgBot));
            canvas.fill(0, 0, w, h);
        };
        host.onKeyPress = [this](const visage::KeyEvent& key) {
            if (key.isMainModifier())
                return false;
            if (key.keyCode() == visage::KeyCode::Space || key.keyCode() == visage::KeyCode::Number1) {
                context.model.triggerAudition(Articulation::main, 1.0f);
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Number2) {
                context.model.triggerAudition(Articulation::ghost, 0.72f);
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Number3) {
                context.model.triggerAudition(Articulation::rim, 0.96f);
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Number4) {
                context.model.triggerAudition(Articulation::roll, 0.92f);
                return true;
            }
            if (key.keyCode() == visage::KeyCode::M) {
                context.model.mutateSound();
                return true;
            }
            return false;
        };
        host.addPlaced(&topBar, kTopBar);
        host.addPlaced(stage.get(), kStage);
        host.addPlaced(macro.get(), kMacro);
        host.addPlaced(rack.get(), kRack);
        host.addPlaced(&voiceBar, kVoiceBar);
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    std::unique_ptr<Panel> stage, macro, rack;
    VoiceBar voiceBar;
};

Editor::Editor(Model& model, std::function<void()> showAbout)
    : impl_(std::make_unique<Impl>(model, std::move(showAbout))) {}

Editor::~Editor() { detach(); }
void Editor::attach(void* parentView, int width, int height) { impl_->host.attach(parentView, width, height); }
void Editor::detach() { impl_->host.detach(); }
visage::Frame& Editor::rootFrame() { return impl_->host.window(); }

void Editor::setSize(int width, int height) { impl_->host.setSize(width, height); }

#if defined(__linux__)
int Editor::eventFd() const { return impl_->host.eventFd(); }
void Editor::dispatchEvents() { impl_->host.dispatchEvents(); }
#endif

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->host.renderToPng(path, width, height, true);
}

} // namespace sb::ui
