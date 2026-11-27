// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pv/preset.h"
#include "pv/ui/editor.h"
#include "pv/ui/fake_model.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "prism-vein-snapshot.png";
    pv::ui::Editor::View view = pv::ui::Editor::View::color;
    std::string presetPath;
    bool simSc = false;
    bool simMidi = false;
    int width = pv::ui::kDefaultWidth;
    int height = pv::ui::kDefaultHeight;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--color" || arg == "color") {
            view = pv::ui::Editor::View::color;
        } else if (arg == "--lattice" || arg == "lattice") {
            view = pv::ui::Editor::View::lattice;
        } else if (arg == "--sidechain") {
            simSc = true;
        } else if (arg == "--midi") {
            simMidi = true;
        } else if (arg == "--preset" && i + 1 < argc) {
            presetPath = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            width = std::max(640, std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::max(380, std::atoi(argv[++i]));
        }
    }

    pv::ui::FakeModel model;
    if (!presetPath.empty()) {
        std::ifstream f(presetPath);
        std::ostringstream ss;
        ss << f.rdbuf();
        if (auto p = pv::parsePreset(ss.str())) {
            for (std::size_t i = 0; i < pv::kParamCount; ++i)
                model.set(static_cast<pv::ParamId>(i), p->params[static_cast<pv::ParamId>(i)]);
            model.setChordMask(p->chordMask);
            for (int l = 0; l < pv::kLaneCount; ++l)
                model.setLaneCurve(l, p->curves[static_cast<std::size_t>(l)]);
            model.setPresetName(p->name);
        }
    }
    model.warmUpEngine(simSc, simMidi);

    pv::ui::Editor editor(model);
    editor.setView(view);
    editor.renderToPng(out, width, height);
    std::cout << "Saved " << out << " (" << (view == pv::ui::Editor::View::color ? "color" : "lattice") << ", " << width
              << "x" << height << ")\n";
    return 0;
}
