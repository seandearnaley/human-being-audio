// SPDX-License-Identifier: GPL-3.0-or-later
// Unified Human Being Audio web showcase renderer.
// Links the pure C++20 engines (LivecutEngine, tf_engine, pf_engine, cl_engine, tg_engine, sb_engine, pv_engine)
// to render 42 composed, parameter-automated, record-ready musical clips (6 per product)
// at 48 kHz stereo float WAV + catalog metadata JSON.

#include "showcase_common.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    using namespace hba::showcase;
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: studio-showcase <repo-root> <out-wav-dir> [product]\n";
        return 2;
    }
    const fs::path repoRoot = argv[1];
    const fs::path outDir = argv[2];
    const std::string_view onlyProduct = (argc == 4) ? std::string_view(argv[3]) : std::string_view{};
    fs::create_directories(outDir);

    auto renderProduct = [&](std::string_view slug) -> bool {
        if (slug == "livecut-redux") {
            renderLivecutClips(outDir);
            return true;
        }
        if (slug == "tract-form") {
            renderTractFormClips(repoRoot, outDir);
            return true;
        }
        if (slug == "pole-field") {
            renderPoleFieldClips(repoRoot, outDir);
            return true;
        }
        if (slug == "cutline") {
            renderCutlineClips(repoRoot, outDir);
            return true;
        }
        if (slug == "torus-glide") {
            renderTorusGlideClips(repoRoot, outDir);
            return true;
        }
        if (slug == "strike-body") {
            renderStrikeBodyClips(repoRoot, outDir);
            return true;
        }
        if (slug == "prism-vein") {
            renderPrismVeinClips(repoRoot, outDir);
            return true;
        }
        return false;
    };

    try {
        if (onlyProduct.empty()) {
            std::cout << "Rendering Human Being Audio Studio Showcase (42 clips)...\n";
            renderLivecutClips(outDir);
            renderTractFormClips(repoRoot, outDir);
            renderPoleFieldClips(repoRoot, outDir);
            renderCutlineClips(repoRoot, outDir);
            renderTorusGlideClips(repoRoot, outDir);
            renderStrikeBodyClips(repoRoot, outDir);
            renderPrismVeinClips(repoRoot, outDir);
        } else {
            std::cout << "Rendering Human Being Audio Showcase (" << onlyProduct << ")...\n";
            if (!renderProduct(onlyProduct)) {
                std::cerr << "ERROR: unknown showcase product '" << onlyProduct << "'\n";
                return 2;
            }
        }

        nlohmann::ordered_json arr = nlohmann::ordered_json::array();
        for (const auto& c : manifest()) {
            arr.push_back({
                {"product", c.product},
                {"id", c.id},
                {"title", c.title},
                {"tag", c.tag},
                {"bpm", c.bpm},
                {"key", c.key},
                {"note", c.note},
                {"wav", c.wavPath},
            });
        }
        std::ofstream mf(outDir / "raw-manifest.json");
        mf << arr.dump(2) << "\n";
        std::cout << "Rendered " << manifest().size() << " clips -> " << outDir << "\n";
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
