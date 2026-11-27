// SPDX-License-Identifier: GPL-3.0-or-later
#include "lc/ui/editor.h"

#include "classic_panel.h"
#include "pad_panel.h"
#include "phrase_tape.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>

namespace lc::ui {

namespace {
constexpr float kMargin = metric::margin;
constexpr float kContentWidth = kDefaultWidth - 2.0f * kMargin;
constexpr hba::ui::DesignRect kTopBar{kMargin, 12.0f, kContentWidth, 52.0f};
constexpr hba::ui::DesignRect kPhraseTape{kMargin, 72.0f, kContentWidth, 52.0f};
constexpr hba::ui::DesignRect kMainDeck{kMargin, 134.0f, kContentWidth, 574.0f};
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)), topBar(context, std::move(showAbout)),
          tape(context), pad(context), classic(context) {
        host.onBeforeLiveTick = [this] { syncSurface(); };
        host.addPlaced(&topBar, kTopBar);
        host.addPlaced(&tape, kPhraseTape);
        host.addPlaced(&pad, kMainDeck);
        host.addPlaced(&classic, kMainDeck);
        syncSurface();
    }

    void syncSurface() {
        const bool isClassic = context.model.getNormalised("surface") >= 0.5f;
        if (isClassic == shownClassic && initialisedSurface)
            return;
        initialisedSurface = true;
        shownClassic = isClassic;
        pad.setVisible(!isClassic);
        classic.setVisible(isClassic);
        host.window().redrawAll();
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    PhraseTape tape;
    PadPanel pad;
    ClassicPanel classic;
    bool shownClassic = false;
    bool initialisedSurface = false;
};

Editor::Editor(Model& model, std::function<void()> showAbout)
    : impl_(std::make_unique<Impl>(model, std::move(showAbout))) {}

Editor::~Editor() { detach(); }
void Editor::attach(void* parentView, int width, int height) { impl_->host.attach(parentView, width, height); }
void Editor::detach() { impl_->host.detach(); }
visage::Frame& Editor::rootFrame() { return impl_->host.window(); }

void Editor::setSize(int width, int height) { impl_->host.setSize(width, height); }
void Editor::selectClassicTab(int tabIndex) { impl_->classic.selectTab(tabIndex); }

#if defined(__linux__)
int Editor::eventFd() const { return impl_->host.eventFd(); }
void Editor::dispatchEvents() { impl_->host.dispatchEvents(); }
#endif

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->host.renderToPng(path, width, height, false);
}

} // namespace lc::ui
