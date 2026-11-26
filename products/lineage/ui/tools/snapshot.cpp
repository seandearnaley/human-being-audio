// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ln/preset.h"
#include "ln/ui/editor.h"
#include "ln/ui/fake_model.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "lineage-snapshot.png";
    bool studio = false;
    int world = 0;
    std::string presetPath;
    int width = ln::ui::kDefaultWidth;
    int height = ln::ui::kDefaultHeight;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--studio" || arg == "studio") {
            studio = true;
        } else if (arg == "--play" || arg == "play") {
            studio = false;
        } else if (arg == "--world" && i + 1 < argc) {
            world = std::clamp(std::atoi(argv[++i]), 0, ln::kPadProgramCount - 1);
        } else if (arg == "--preset" && i + 1 < argc) {
            presetPath = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            width = std::max(640, std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::max(380, std::atoi(argv[++i]));
        }
    }

    ln::ui::FakeModel model;
    if (!presetPath.empty()) {
        std::ifstream f(presetPath);
        std::ostringstream ss;
        ss << f.rdbuf();
        if (auto p = ln::parsePreset(ss.str())) {
            for (int i = 0; i < ln::kParamCount; ++i)
                model.set(static_cast<ln::ParamId>(i), p->params[static_cast<ln::ParamId>(i)]);
            for (int lane = 0; lane < ln::kLaneCount; ++lane)
                model.setLaneCurve(lane, p->curves[static_cast<std::size_t>(lane)]);
            model.setPresetName(p->name);
        }
    }
    model.set(ln::ParamId::padProgram, static_cast<float>(world));

    ln::ui::Editor editor(model);
    if (studio)
        editor.setView(ln::ui::Editor::View::studio);
    editor.renderToPng(out, width, height);
    std::cout << "Saved " << out << " (" << (studio ? "studio" : "play") << ", world=" << world << ", " << width << "x"
              << height << ")\n";
    return 0;
}
