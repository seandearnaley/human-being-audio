// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/editor.h"
#include "cl/ui/fake_model.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr, "usage: cl-ui-snapshot <out.png> [width height]\n");
        return 2;
    }
    const int width = argc == 4 ? std::atoi(argv[2]) : cl::ui::kDefaultWidth;
    const int height = argc == 4 ? std::atoi(argv[3]) : cl::ui::kDefaultHeight;

    cl::ui::FakeModel model;
    cl::ui::Editor editor(model);
    editor.renderToPng(argv[1], width, height);
    std::printf("%s %dx%d\n", argv[1], width, height);
    return 0;
}
