// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/ui/model.h"

#include <functional>
#include <memory>
#include <string>

namespace cl::ui {

inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 720;
inline constexpr int kMinWidth = 960;
inline constexpr int kMinHeight = 540;

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
    void setGridMode(bool grid);
    void setFillView(bool fill);

#if defined(__linux__)
    int eventFd() const;
    void dispatchEvents();
#endif

    void renderToPng(const std::string& path, int width = kDefaultWidth, int height = kDefaultHeight);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cl::ui
