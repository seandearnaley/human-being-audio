// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/editor.h"
#include "cl/ui/fake_model.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        std::fprintf(stderr, "usage: cl-ui-snapshot <out.png> [width height] [grid|0..7]\n");
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
    bool showGrid = false;
    if (modeArg != nullptr) {
        if (std::strcmp(modeArg, "grid") == 0) {
            showGrid = true;
        } else {
            const int prog = std::atoi(modeArg);
            model.set(cl::ParamId::padProgram, static_cast<float>(prog));
        }
    }

    cl::ui::Editor editor(model);
    editor.setGridMode(showGrid);
    editor.renderToPng(argv[1], width, height);
    std::printf("%s %dx%d (%s)\n", argv[1], width, height, showGrid ? "grid" : "play");
    return 0;
}
