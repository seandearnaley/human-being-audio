// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>

namespace tg::ui {

namespace {
constexpr float kM = metric::margin;
constexpr float kW = kDefaultWidth - 2.0f * kM;
constexpr hba::ui::DesignRect kTopBar{kM, 10.0f, kW, 52.0f};

// Hero PLAY surface (default)
constexpr hba::ui::DesignRect kRibbon{kM, 70.0f, kW, 56.0f};
constexpr hba::ui::DesignRect kPlayPad{kM, 134.0f, kW, 616.0f};

// Deep STUDIO surface (1-click toggle)
constexpr hba::ui::DesignRect kStage{kM, 70.0f, kW, 288.0f};
constexpr hba::ui::DesignRect kControls{kM, 366.0f, kW, 226.0f};
constexpr hba::ui::DesignRect kMovement{kM, 600.0f, kW, 150.0f};
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)),
          topBar(context, std::move(showAbout), [this](bool studio) { applyView(studio); }),
          ribbon(makeManifoldRibbon(context)), playPad(makePlayPanel(context)), stage(makeStagePanel(context)),
          controls(makeControlsStrip(context)), movement(makeMovementPanel(context)) {
        host.onKeyPress = [this](const visage::KeyEvent& key) {
            if (key.keyCode() == visage::KeyCode::Space && !key.isMainModifier()) {
                const bool next = context.model.get(ParamId::padHold) < 0.5f;
                context.model.begin(ParamId::padHold);
                context.model.set(ParamId::padHold, next ? 1.0f : 0.0f);
                context.model.end(ParamId::padHold);
                return true;
            }
            return false;
        };
        host.addPlaced(&topBar, kTopBar);
        host.addPlaced(ribbon.get(), kRibbon);
        host.addPlaced(playPad.get(), kPlayPad);
        host.addPlaced(stage.get(), kStage);
        host.addPlaced(controls.get(), kControls);
        host.addPlaced(movement.get(), kMovement);
        applyView(false);
    }

    void applyView(bool studio) {
        studioView = studio;
        ribbon->setVisible(!studio);
        playPad->setVisible(!studio);
        stage->setVisible(studio);
        controls->setVisible(studio);
        movement->setVisible(studio);
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    std::unique_ptr<Panel> ribbon, playPad;
    std::unique_ptr<Panel> stage, controls, movement;
    bool studioView = false;
};

Editor::Editor(Model& model, std::function<void()> showAbout)
    : impl_(std::make_unique<Impl>(model, std::move(showAbout))) {}

Editor::~Editor() { detach(); }
void Editor::attach(void* parentView, int width, int height) { impl_->host.attach(parentView, width, height); }
void Editor::detach() { impl_->host.detach(); }
void Editor::setSize(int width, int height) { impl_->host.setSize(width, height); }
void Editor::setView(View view) { impl_->topBar.setStudioView(view == View::studio); }

#if defined(__linux__)
int Editor::eventFd() const { return impl_->host.eventFd(); }
void Editor::dispatchEvents() { impl_->host.dispatchEvents(); }
#endif

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->host.renderToPng(path, width, height, true);
}

} // namespace tg::ui
