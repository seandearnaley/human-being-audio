// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/editor.h"

#include "panels.h"
#include "play_panel.h"
#include "pulse_ribbon.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>

namespace cl::ui {

namespace {
constexpr float kMargin = metric::margin;
constexpr float kContentWidth = kDefaultWidth - 2.0f * kMargin;

constexpr hba::ui::DesignRect kTopBar{kMargin, 12.0f, kContentWidth, 52.0f};

// PLAY Surface (default landing view)
constexpr hba::ui::DesignRect kPulseRibbon{kMargin, 72.0f, kContentWidth, 54.0f};
constexpr hba::ui::DesignRect kPlayStage{kMargin, 134.0f, kContentWidth, 574.0f};

// GRID Surface (1-click toggle)
constexpr hba::ui::DesignRect kMacroStrip{kMargin, 72.0f, kContentWidth, 64.0f};
constexpr hba::ui::DesignRect kGridStage{kMargin, 144.0f, kContentWidth, 362.0f};

constexpr float kBottomY = 514.0f;
constexpr float kBottomH = 194.0f;
constexpr float kSetupW = 278.0f;
constexpr float kDeliveryW = 288.0f;
constexpr float kExprW = kContentWidth - kSetupW - kDeliveryW - 2.0f * kMargin;

constexpr hba::ui::DesignRect kSetup{kMargin, kBottomY, kSetupW, kBottomH};
constexpr hba::ui::DesignRect kExpr{kMargin + kSetupW + kMargin, kBottomY, kExprW, kBottomH};
constexpr hba::ui::DesignRect kDelivery{kDefaultWidth - kMargin - kDeliveryW, kBottomY, kDeliveryW, kBottomH};
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)),
          topBar(context, std::move(showAbout), [this](bool grid) { setGridMode(grid); }),
          ribbon(context, [this] { setGridMode(true); }), play(context), macros(makeMacroStripPanel(context)),
          stage(makeGridStagePanel(context)), setup(makeSetupPanel(context)), expr(makeMovementPanel(context)),
          delivery(makeDeliveryPanel(context)) {
        host.addPlaced(&topBar, kTopBar);

        // PLAY surface children
        host.addPlaced(&ribbon, kPulseRibbon);
        host.addPlaced(&play, kPlayStage);

        // GRID surface children
        host.addPlaced(macros.get(), kMacroStrip);
        host.addPlaced(stage.get(), kGridStage);
        host.addPlaced(setup.get(), kSetup);
        host.addPlaced(expr.get(), kExpr);
        host.addPlaced(delivery.get(), kDelivery);

        applySurface();
    }

    void setGridMode(bool grid) {
        if (gridMode == grid && initialised)
            return;
        gridMode = grid;
        applySurface();
    }

    void applySurface() {
        initialised = true;
        topBar.setGridMode(gridMode);
        ribbon.setVisible(!gridMode);
        play.setVisible(!gridMode);
        macros->setVisible(gridMode);
        stage->setVisible(gridMode);
        setup->setVisible(gridMode);
        expr->setVisible(gridMode);
        delivery->setVisible(gridMode);
        host.window().redrawAll();
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    PulseRibbon ribbon;
    PlayPanel play;
    std::unique_ptr<Panel> macros, stage, setup, expr, delivery;
    bool gridMode = true;
    bool initialised = false;
};

Editor::Editor(Model& model, std::function<void()> showAbout)
    : impl_(std::make_unique<Impl>(model, std::move(showAbout))) {}

Editor::~Editor() { detach(); }
void Editor::attach(void* parentView, int width, int height) { impl_->host.attach(parentView, width, height); }
void Editor::detach() { impl_->host.detach(); }
visage::Frame& Editor::rootFrame() { return impl_->host.window(); }

void Editor::setSize(int width, int height) { impl_->host.setSize(width, height); }
void Editor::setGridMode(bool grid) { impl_->setGridMode(grid); }
void Editor::setFillView(bool fill) {
    impl_->context.fillView = fill;
    impl_->host.window().redrawAll();
}

#if defined(__linux__)
int Editor::eventFd() const { return impl_->host.eventFd(); }
void Editor::dispatchEvents() { impl_->host.dispatchEvents(); }
#endif

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->host.renderToPng(path, width, height, false);
}

} // namespace cl::ui
