#include "pf/preset.h"
#include "pf/ui/editor.h"
#include "pf/ui/fake_model.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pf-ui-snapshot <out.png> [width height] [--preset <file.json>]\n");
        return 2;
    }
    const char* outPath = argv[1];
    int width = pf::ui::kDefaultWidth;
    int height = pf::ui::kDefaultHeight;
    const char* presetPath = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--preset" && i + 1 < argc) {
            presetPath = argv[++i];
        } else if (i + 1 < argc && width == pf::ui::kDefaultWidth && height == pf::ui::kDefaultHeight) {
            width = std::atoi(argv[i]);
            height = std::atoi(argv[++i]);
        }
    }

    pf::ui::FakeModel model;
    if (presetPath != nullptr) {
        std::ifstream in(presetPath);
        std::ostringstream buf;
        buf << in.rdbuf();
        std::string err;
        const auto preset = pf::parsePreset(buf.str(), &err);
        if (!preset) {
            std::fprintf(stderr, "failed to parse preset %s: %s\n", presetPath, err.c_str());
            return 1;
        }
        for (int i = 0; i < pf::kParamCount; ++i) {
            const auto id = static_cast<pf::ParamId>(i);
            model.set(id, preset->params[id]);
        }
        for (int l = 0; l < pf::kLaneCount; ++l)
            model.setLaneCurve(l, preset->curves[static_cast<std::size_t>(l)]);
    }

    pf::ui::Editor editor(model);
    editor.renderToPng(outPath, width, height);
    std::printf("%s %dx%d\n", outPath, width, height);
    return 0;
}
