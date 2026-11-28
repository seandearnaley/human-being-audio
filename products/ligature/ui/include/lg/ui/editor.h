// SPDX-License-Identifier: AGPL-3.0-or-later
// The Ligature editor: one window with the song strip, the timeline (harmony lane, four part rolls, stutter lane),
// the inspector, the keyboard and the theory lens. Visage; no JUCE.
#pragma once

#include "lg/ui/model.h"

#include <functional>
#include <memory>
#include <string>

namespace visage {
class Frame;
}

namespace lg::ui {

inline constexpr int kDefaultWidth = 1240;
inline constexpr int kDefaultHeight = 760;
inline constexpr int kMinWidth = 992;
inline constexpr int kMinHeight = 608;

class Editor {
  public:
    explicit Editor(Model& model, std::function<void()> showAbout = {});
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    void attach(void* parentView, int width, int height);
    void detach();
    visage::Frame& rootFrame();
    void setSize(int width, int height);

#if defined(__linux__)
    int eventFd() const;
    void dispatchEvents();
#endif

    // Snapshot support: what the editor shows before rendering.
    void focusItem(ItemRef item);
    void selectChord(ItemRef item, int span);
    void selectPart(Role role);
    void setFollow(bool follow); // focus follows the playing item (on by default; a manual focus turns it off)
    void setLens(bool on);       // numerals, function words, tension and the lens card
    void renderToPng(const std::string& path, int width = kDefaultWidth, int height = kDefaultHeight);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lg::ui
