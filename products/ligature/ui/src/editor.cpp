// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/ui/editor.h"

#include "inspector.h"
#include "keyboard_lens.h"
#include "song_strip.h"
#include "timeline.h"
#include "top_bar.h"

#include <hba/ui/editor_window.h>

namespace lg::ui {

namespace {
// The window at its design size: top bar, song strip, timeline beside the inspector, keyboard beside the lens.
constexpr hba::ui::DesignRect kTopBar{0.0f, 0.0f, 1240.0f, 48.0f};
constexpr hba::ui::DesignRect kStrip{10.0f, 56.0f, 1220.0f, 90.0f};
constexpr hba::ui::DesignRect kTimeline{10.0f, 154.0f, 912.0f, 504.0f};
constexpr hba::ui::DesignRect kInspector{930.0f, 154.0f, 300.0f, 504.0f};
constexpr hba::ui::DesignRect kKeyboard{10.0f, 666.0f, 912.0f, 84.0f};
constexpr hba::ui::DesignRect kLens{930.0f, 666.0f, 300.0f, 84.0f};
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)), topBar(context, std::move(showAbout)),
          strip(context), timeline(context), inspector(context), keyboard(context), lens(context) {
        host.addPlaced(&topBar, kTopBar);
        host.addPlaced(&strip, kStrip);
        host.addPlaced(&timeline, kTimeline);
        host.addPlaced(&inspector, kInspector);
        host.addPlaced(&keyboard, kKeyboard);
        host.addPlaced(&lens, kLens);
        // Before the panels tick: Follow moves the focus to whatever plays, then the selection is kept valid.
        host.onBeforeLiveTick = [this] {
            if (context.doc().loadCount() != context.loadsSeen)
                context.songLoaded();
            const auto& t = context.model.telemetry();
            if (t.playing)
                context.followTo(t.performance.item);
            context.validate();
        };
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    SongStrip strip;
    Timeline timeline;
    Inspector inspector;
    KeyboardPanel keyboard;
    LensCard lens;
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

void Editor::focusItem(ItemRef item) { impl_->context.focusOn(item); }

void Editor::selectChord(ItemRef item, int span) { impl_->context.selectChord(item, span); }

void Editor::selectPart(Role role) { impl_->context.selectPart(role); }

void Editor::setFollow(bool follow) {
    impl_->context.follow = follow;
    impl_->context.changed();
}

void Editor::setLens(bool on) {
    impl_->context.lens = on;
    impl_->context.changed();
}

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->host.renderToPng(path, width, height, true);
}

} // namespace lg::ui
