// SPDX-License-Identifier: GPL-3.0-or-later
#include "lc/ui/editor.h"
#include "lc/ui/fake_model.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string outPath = "livecut-snapshot.png";
    std::string surface = "pad";
    int program = 0;
    int tab = 0;
    int width = lc::ui::kDefaultWidth * 2;
    int height = lc::ui::kDefaultHeight * 2;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc)
            outPath = argv[++i];
        else if (arg == "--surface" && i + 1 < argc)
            surface = argv[++i];
        else if (arg == "--program" && i + 1 < argc)
            program = std::atoi(argv[++i]);
        else if (arg == "--tab" && i + 1 < argc)
            tab = std::atoi(argv[++i]);
        else if (arg == "--width" && i + 1 < argc)
            width = std::atoi(argv[++i]);
        else if (arg == "--height" && i + 1 < argc)
            height = std::atoi(argv[++i]);
        else if (arg[0] != '-')
            outPath = arg;
    }

    lc::ui::FakeModel model;
    model.setReal("surface", surface == "classic" ? 1.0f : 0.0f);
    model.setReal("padProgram", static_cast<float>(program));
    model.populateDemoTelemetry();

    lc::ui::Editor editor(model);
    if (surface == "classic")
        editor.selectClassicTab(tab);
    editor.renderToPng(outPath, width, height);
    std::cout << "Wrote " << outPath << " (" << width << "x" << height << ")\n";
    return 0;
}
