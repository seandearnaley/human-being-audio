// tf-ui-snapshot <out.png> [width height]
// Renders the editor without a window, so layout can be checked from the command line.
#include "tf/ui/editor.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr, "usage: tf-ui-snapshot <out.png> [width height]\n");
        return 2;
    }
    const int width = argc == 4 ? std::atoi(argv[2]) : tf::ui::kDefaultWidth;
    const int height = argc == 4 ? std::atoi(argv[3]) : tf::ui::kDefaultHeight;

    tf::ui::Editor editor;
    editor.renderToPng(argv[1], width, height);
    std::printf("%s %dx%d\n", argv[1], width, height);
    return 0;
}
