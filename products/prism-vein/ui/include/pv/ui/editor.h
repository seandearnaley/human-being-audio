// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "pv/ui/model.h"

#include <functional>
#include <memory>
#include <string>

namespace pv::ui {

inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 762;
inline constexpr int kMinWidth = 960;
inline constexpr int kMinHeight = 572;

class Editor {
  public:
    enum class View { color, lattice };

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

} // namespace pv::ui
