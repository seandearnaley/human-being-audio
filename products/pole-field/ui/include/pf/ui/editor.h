#pragma once

#include "pf/ui/model.h"

#include <functional>
#include <memory>
#include <string>

namespace pf::ui {

inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 762;
inline constexpr int kMinWidth = 960;
inline constexpr int kMinHeight = 572;

class Editor {
public:
    explicit Editor(Model& model, std::function<void()> showAbout = {});
    ~Editor();

    void attach(void* parentView, int width, int height);
    void detach();
    void setSize(int width, int height);

#if defined(__linux__)
    int eventFd() const;
    void dispatchEvents();
#endif

    void renderToPng(const std::string& path, int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
