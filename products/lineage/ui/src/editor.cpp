// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ln/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <hba/core/note_name.h>
#include <hba/ui/editor_window.h>
#include <hba/ui/voice_bar.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace ln::ui {

namespace {
constexpr float kM = metric::margin;
constexpr float kW = kDefaultWidth - 2.0f * kM;
constexpr hba::ui::DesignRect kTopBar{kM, 10.0f, kW, 52.0f};

// Hero PLAY surface (default)
constexpr hba::ui::DesignRect kRibbon{kM, 70.0f, kW, 54.0f};
constexpr hba::ui::DesignRect kPlayPad{kM, 132.0f, kW, 616.0f};

// Deep STUDIO surface (1-click toggle)
constexpr hba::ui::DesignRect kStudioStage{kM, 70.0f, kW, 214.0f};
constexpr hba::ui::DesignRect kStudioDeck{kM, 292.0f, kW, 272.0f};
constexpr hba::ui::DesignRect kMovement{kM, 572.0f, kW, 176.0f};

// Persistent studio Voice / Playability / Quality footer (always visible in both PLAY and STUDIO)
constexpr hba::ui::DesignRect kVoiceBar{kM, 756.0f, kW, hba::ui::kVoiceBarHeight};

using VoiceBar = hba::ui::StudioVoiceBar<Context, ParamFrame, ParamId, ParamKind>;
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)),
          topBar(context, std::move(showAbout), [this](bool studio) { applyView(studio); }),
          ribbon(makeStrataRibbon(context)), playPad(makePlayPanel(context)), studioStage(makeStudioStage(context)),
          studioDeck(makeStudioDeck(context)), movement(makeMovementPanel(context)),
          voiceBar(
              context, colour::reese, "VOICE",
              {
                  {ParamId::playMode, "Mode", colour::reese, 118.0f, false, false},
                  {ParamId::glide, "Glide", colour::reese, 136.0f, true, false},
                  {ParamId::glideMode, "Portamento", colour::reese, 134.0f, false, false},
                  {ParamId::bendRange, "Bend", colour::sculpt, 106.0f, false, false},
                  {ParamId::octave, "Octave", colour::sub, 108.0f, false, false},
                  {ParamId::quality, "OS", colour::reese, 74.0f, false, true},
              },
              [this]() -> std::string {
                  const auto& v = context.model.visuals();
                  const int note = v.activeNote >= 0 ? v.activeNote : 29;
                  const bool isPoly =
                      static_cast<int>(std::lround(context.model.get(ParamId::playMode))) == static_cast<int>(PlayMode::poly4);
                  const std::string n = hba::core::midiNoteName(note, false);
                  if (isPoly)
                      return n + " \xc2\xb7 " + std::to_string(std::max(0, v.activeVoices)) + "/4v";
                  return n + (v.sounding ? " \xc2\xb7 1v" : "");
              }) {
        host.onKeyPress = [this](const visage::KeyEvent& key) {
            // Use D (leaving Space free for Ableton Live / DAW transport!)
            if (key.keyCode() == visage::KeyCode::D && !key.isMainModifier()) {
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
        host.addPlaced(studioStage.get(), kStudioStage);
        host.addPlaced(studioDeck.get(), kStudioDeck);
        host.addPlaced(movement.get(), kMovement);
        host.addPlaced(&voiceBar, kVoiceBar);
        applyView(false);
    }

    void applyView(bool studio) {
        studioView = studio;
        ribbon->setVisible(!studio);
        playPad->setVisible(!studio);
        studioStage->setVisible(studio);
        studioDeck->setVisible(studio);
        movement->setVisible(studio);
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    std::unique_ptr<Panel> ribbon, playPad;
    std::unique_ptr<Panel> studioStage, studioDeck, movement;
    VoiceBar voiceBar;
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

} // namespace ln::ui
