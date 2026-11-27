// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/ui/model.h"

#include <functional>
#include <memory>
#include <string>

namespace ln::ui {

inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 802;
inline constexpr int kMinWidth = 960;
inline constexpr int kMinHeight = 602;

class Editor {
  public:
    enum class View { play, studio };

    explicit Editor(Model& model, std::function<void()> showAbout = {});
    ~Editor();

    void attach(void* parentView, int width, int height);
    void detach();
    visage::Frame& rootFrame();
    void setSize(int width, int height);
    void setView(View view);

#if defined(__linux__)
    int eventFd() const;
    void dispatchEvents();
#endif

    void renderToPng(const std::string& path, int width, int height);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ln::ui
