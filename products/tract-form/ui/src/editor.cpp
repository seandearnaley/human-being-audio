#include "tf/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <hba/core/note_name.h>
#include <hba/ui/editor_window.h>
#include <hba/ui/voice_bar.h>

#include <string>

namespace tf::ui {

namespace {
constexpr float kMargin = metric::margin;
constexpr float kContentWidth = kDefaultWidth - 2.0f * kMargin;
constexpr hba::ui::DesignRect kTopBar{kMargin, 12.0f, kContentWidth, 64.0f};
constexpr hba::ui::DesignRect kGraph{kMargin, 84.0f, kContentWidth, 292.0f};
constexpr hba::ui::DesignRect kAnalysis{kMargin, 388.0f, kContentWidth, 132.0f};
constexpr float kOutputWidth = 304.0f;
constexpr hba::ui::DesignRect kMovement{kMargin, 532.0f, kContentWidth - kOutputWidth - kMargin, 176.0f};
constexpr hba::ui::DesignRect kOutput{kDefaultWidth - kMargin - kOutputWidth, 532.0f, kOutputWidth, 176.0f};
constexpr hba::ui::DesignRect kVoiceBar{kMargin, 716.0f, kContentWidth, hba::ui::kVoiceBarHeight};

using VoiceBar = hba::ui::StudioVoiceBar<Context, ParamFrame, ParamId, ParamKind>;
} // namespace

struct Editor::Impl final {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), host(context, static_cast<float>(kDefaultWidth)), topBar(context, std::move(showAbout)),
          graph(makeGraphPanel(context)), analysis(makeAnalysisPanel(context)), movement(makeMovementPanel(context)),
          output(makeOutputPanel(context)),
          voiceBar(
              context, colour::master, "VOICE",
              {
                  {ParamId::playMode, "Mode", colour::master, 114.0f, false, false},
                  {ParamId::glide, "Glide", colour::master, 136.0f, true, false},
                  {ParamId::glideMode, "Portamento", colour::master, 134.0f, false, false},
                  {ParamId::bendRange, "Bend", colour::growl, 106.0f, false, false},
                  {ParamId::octave, "Octave", colour::text, 108.0f, false, false},
                  {ParamId::quality, "OS", colour::master, 74.0f, false, true},
              },
              [this]() -> std::string {
                  const auto& v = context.model.visuals();
                  if (v.note >= 0)
                      return hba::core::midiNoteName(v.note, false) + (v.gate ? " \xc2\xb7 1v" : "");
                  return "Mono";
              }) {
        host.addPlaced(&topBar, kTopBar);
        host.addPlaced(graph.get(), kGraph);
        host.addPlaced(analysis.get(), kAnalysis);
        host.addPlaced(movement.get(), kMovement);
        host.addPlaced(output.get(), kOutput);
        host.addPlaced(&voiceBar, kVoiceBar);
    }

    Context context;
    hba::ui::StudioEditorWindow<Context> host;
    TopBar topBar;
    std::unique_ptr<Panel> graph, analysis, movement, output;
    VoiceBar voiceBar;
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

} // namespace tf::ui
