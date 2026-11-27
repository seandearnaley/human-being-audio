// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/editor.h"
#include "cl/ui/fake_model.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        std::fprintf(stderr,
                     "usage: cl-ui-snapshot <out.png> [width height] [grid|grid1|grid4|live|fill|slicer|play|0..3]\n");
        return 2;
    }
    int width = cl::ui::kDefaultWidth;
    int height = cl::ui::kDefaultHeight;
    const char* modeArg = nullptr;

    if (argc == 3) {
        modeArg = argv[2];
    } else if (argc >= 4) {
        width = std::atoi(argv[2]);
        height = std::atoi(argv[3]);
        if (argc == 5)
            modeArg = argv[4];
    }

    cl::ui::FakeModel model;
    bool showGrid = true;
    bool showFill = false;
    const std::string mode = modeArg != nullptr ? modeArg : "grid";
    if (mode == "grid1") {
        model.showBars(1);
    } else if (mode == "grid4") {
        model.showBars(4);
    } else if (mode == "live") {
        model.showLive();
    } else if (mode == "fill") {
        showFill = true;
    } else if (mode == "slicer") {
        model.showSlicer();
    } else if (mode != "grid") {
        showGrid = false; // PLAY: "play" keeps the model's program, a number selects one
        if (mode != "play")
            model.set(cl::ParamId::padProgram, static_cast<float>(std::atoi(mode.c_str())));
    }

    cl::ui::Editor editor(model);
    editor.setGridMode(showGrid);
    editor.setFillView(showFill);
    editor.renderToPng(argv[1], width, height);
    std::printf("%s %dx%d (%s)\n", argv[1], width, height, mode.c_str());
    return 0;
}
