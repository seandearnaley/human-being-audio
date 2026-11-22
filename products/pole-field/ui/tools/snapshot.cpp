#include "pf/ui/editor.h"
#include "pf/ui/fake_model.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr, "usage: pf-ui-snapshot <out.png> [width height]\n");
        return 2;
    }
    const int width = argc == 4 ? std::atoi(argv[2]) : pf::ui::kDefaultWidth;
    const int height = argc == 4 ? std::atoi(argv[3]) : pf::ui::kDefaultHeight;

    pf::ui::FakeModel model;
    pf::ui::Editor editor(model);
    editor.renderToPng(argv[1], width, height);
    std::printf("%s %dx%d\n", argv[1], width, height);
    return 0;
}
