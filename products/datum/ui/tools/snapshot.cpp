// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dt/preset.h"
#include "dt/ui/editor.h"
#include "dt/ui/fake_model.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "datum-snapshot.png";
    dt::ui::Editor::View view = dt::ui::Editor::View::sculpt;
    std::string presetPath;
    bool demoSidechain = false;
    int width = dt::ui::kDefaultWidth;
    int height = dt::ui::kDefaultHeight;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sculpt" || arg == "sculpt") {
            view = dt::ui::Editor::View::sculpt;
        } else if (arg == "--anatomy" || arg == "anatomy") {
            view = dt::ui::Editor::View::anatomy;
        } else if (arg == "--compose" || arg == "compose") {
            view = dt::ui::Editor::View::compose;
        } else if (arg == "--sidechain") {
            demoSidechain = true;
        } else if (arg == "--preset" && i + 1 < argc) {
            presetPath = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            width = std::max(640, std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::max(380, std::atoi(argv[++i]));
        }
    }

    dt::ui::FakeModel model;
    if (!presetPath.empty()) {
        std::ifstream f(presetPath);
        std::ostringstream ss;
        ss << f.rdbuf();
        if (auto p = dt::parsePreset(ss.str())) {
            for (std::size_t i = 0; i < dt::kParamCount; ++i)
                model.set(static_cast<dt::ParamId>(i), p->params[static_cast<dt::ParamId>(i)]);
            model.setPitchCurve(p->pitchCurve);
            model.setAmpCurve(p->ampCurve);
            model.setPattern(p->pattern);
            model.setPresetName(p->name);
        }
    }
    if (demoSidechain)
        model.setDemoSidechain(true);

    dt::ui::Editor editor(model);
    editor.setView(view);
    editor.renderToPng(out, width, height);
    const char* name = view == dt::ui::Editor::View::sculpt
                           ? "sculpt"
                           : (view == dt::ui::Editor::View::anatomy ? "anatomy" : "compose");
    std::cout << "Saved " << out << " (" << name << ", " << width << "x" << height << ")\n";
    return 0;
}
