// The whole interface behind one small, Visage-free header. The plugin shell
// attaches it to a native view; tests render it headlessly to a PNG.
#pragma once

#include "tf/ui/model.h"

#include <memory>
#include <string>

namespace tf::ui {

inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 720;
inline constexpr int kMinWidth = 960;
inline constexpr int kMinHeight = 540;

class Editor {
public:
    explicit Editor(Model& model);
    ~Editor();

    // `parentView` is the host's native view (an NSView* on macOS).
    void attach(void* parentView, int width, int height);
    void detach();
    void setSize(int width, int height);

    // Draws one frame without a window and saves it. For snapshot tests.
    void renderToPng(const std::string& path, int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
