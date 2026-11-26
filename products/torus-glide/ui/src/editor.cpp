// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>

namespace tg::ui {

namespace {
constexpr float kM = metric::margin;
constexpr float kW = kDefaultWidth - 2.0f * kM;
constexpr hba::ui::DesignRect kTopBar{kM, 10.0f, kW, 56.0f};
constexpr hba::ui::DesignRect kStage{kM, 74.0f, kW, 286.0f};
constexpr hba::ui::DesignRect kControls{kM, 368.0f, kW, 224.0f};
constexpr hba::ui::DesignRect kMovement{kM, 600.0f, kW, 150.0f};
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)), topBar(context, std::move(showAbout)),
          stage(makeStagePanel(context)), controls(makeControlsStrip(context)), movement(makeMovementPanel(context)) {
        host.onKeyPress = [this](const visage::KeyEvent& key) {
            if (key.keyCode() == visage::KeyCode::Space && !key.isMainModifier()) {
                context.model.setAudition(!context.model.isAuditioning());
                return true;
            }
            return false;
        };
        host.addPlaced(&topBar, kTopBar);
        host.addPlaced(stage.get(), kStage);
        host.addPlaced(controls.get(), kControls);
        host.addPlaced(movement.get(), kMovement);
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    std::unique_ptr<Panel> stage, controls, movement;
};

Editor::Editor(Model& model, std::function<void()> showAbout)
    : impl_(std::make_unique<Impl>(model, std::move(showAbout))) {}

Editor::~Editor() { detach(); }
void Editor::attach(void* parentView, int width, int height) { impl_->host.attach(parentView, width, height); }
void Editor::detach() { impl_->host.detach(); }
void Editor::setSize(int width, int height) { impl_->host.setSize(width, height); }

#if defined(__linux__)
int Editor::eventFd() const { return impl_->host.eventFd(); }
void Editor::dispatchEvents() { impl_->host.dispatchEvents(); }
#endif

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->host.renderToPng(path, width, height, true);
}

} // namespace tg::ui
