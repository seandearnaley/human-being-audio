// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/ui/model.h"

#include <functional>
#include <memory>
#include <string>

namespace tg::ui {

inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 760;
inline constexpr int kMinWidth = 960;
inline constexpr int kMinHeight = 570;

class Editor {
  public:
    enum class View { play, studio };

    Editor(Model& model, std::function<void()> showAbout = {});
    ~Editor();

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    void attach(void* parentView, int width, int height);
    void detach();
    void setSize(int width, int height);
    void setView(View view);

#if defined(__linux__)
    int eventFd() const;
    void dispatchEvents();
#endif

    void renderToPng(const std::string& path, int width = kDefaultWidth, int height = kDefaultHeight);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tg::ui
