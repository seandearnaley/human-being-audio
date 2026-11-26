// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>

namespace cl::ui {

namespace {
constexpr float kMargin = metric::margin;
constexpr float kContentWidth = kDefaultWidth - 2.0f * kMargin;
constexpr hba::ui::DesignRect kTopBar{kMargin, 12.0f, kContentWidth, 64.0f};
constexpr hba::ui::DesignRect kMainDeck{kMargin, 88.0f, kContentWidth, 432.0f};
constexpr float kDeliveryWidth = 316.0f;
constexpr hba::ui::DesignRect kMovement{kMargin, 532.0f, kContentWidth - kDeliveryWidth - kMargin, 176.0f};
constexpr hba::ui::DesignRect kDelivery{kDefaultWidth - kMargin - kDeliveryWidth, 532.0f, kDeliveryWidth, 176.0f};
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)), topBar(context, std::move(showAbout)),
          phrase(makePhrasePanel(context)), movement(makeMovementPanel(context)), delivery(makeDeliveryPanel(context)) {
        host.addPlaced(&topBar, kTopBar);
        host.addPlaced(phrase.get(), kMainDeck);
        host.addPlaced(movement.get(), kMovement);
        host.addPlaced(delivery.get(), kDelivery);
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    std::unique_ptr<Panel> phrase, movement, delivery;
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
    impl_->host.renderToPng(path, width, height, false);
}

} // namespace cl::ui
