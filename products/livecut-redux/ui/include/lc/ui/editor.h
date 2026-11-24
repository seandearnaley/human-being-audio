// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lc/ui/model.h"

#include <functional>
#include <memory>
#include <string>

namespace lc::ui {

class Editor {
  public:
    Editor(Model& model, std::function<void()> showAbout = {});
    ~Editor();

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    void attach(void* parentView, int width, int height);
    void detach();
    void setSize(int width, int height);

    void selectClassicTab(int tabIndex);

#if defined(__linux__)
    int eventFd() const;
    void dispatchEvents();
#endif

    void renderToPng(const std::string& path, int width = kDefaultWidth * 2, int height = kDefaultHeight * 2);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lc::ui
