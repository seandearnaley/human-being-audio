// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/ui/model.h"

#include <functional>
#include <memory>
#include <string>

namespace sb::ui {

inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 820;
inline constexpr int kMinWidth = 960;
inline constexpr int kMinHeight = 615;

class Editor {
  public:
    Editor(Model& model, std::function<void()> showAbout = {});
    ~Editor();

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    void attach(void* parentView, int width, int height);
    void detach();
    void setSize(int width, int height);

#if defined(__linux__)
    int eventFd() const;
    void dispatchEvents();
#endif

    void renderToPng(const std::string& path, int width = kDefaultWidth, int height = kDefaultHeight);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sb::ui
