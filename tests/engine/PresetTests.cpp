// The factory bank: every file in presets/ loads, is complete and named, and
// plays within the instrument's promises (the sub does not move, the output
// stays under the ceiling). TF_PRESETS_DIR is set by CMake.
#include "helpers.h"

#include "tf/preset.h"
#include "tf/render.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace tf;
using namespace tf::test;

namespace {

struct File {
    std::string path;
    std::string text;
};

std::vector<File> presetFiles() {
    std::vector<File> files;
    for (const auto& entry : std::filesystem::directory_iterator(TF_PRESETS_DIR)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        files.push_back({ entry.path().filename().string(), text.str() });
    }
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.path < b.path; });
    return files;
}

constexpr std::array<std::string_view, 5> kCategories { "Reese", "Neuro", "Talk", "Sub", "Motion" };

}

TEST_CASE("every factory preset parses, is named and complete") {
    const auto files = presetFiles();
    REQUIRE(files.size() >= 28);

    std::set<std::string> names;
    for (const auto& file : files) {
        CAPTURE(file.path);
        std::string error;
        const auto preset = parsePreset(file.text, &error);
        INFO(error);
        REQUIRE(preset.has_value());
        CHECK(!preset->name.empty());
        CHECK(names.insert(preset->name).second);  // unique
        CHECK(std::find(kCategories.begin(), kCategories.end(), preset->category) != kCategories.end());

        // Every parameter is written out except quality, so a preset does not
        // shift when a default changes.
        for (const auto& spec : kParams) {
            CAPTURE(spec.key);
            const bool written = file.text.find("\"" + std::string(spec.key) + "\"") != std::string::npos;
            CHECK(written == (spec.key != "quality"));
        }
    }
}

TEST_CASE("every factory preset renders with a steady sub under the ceiling") {
    constexpr double kRate = 48000.0;
    for (const auto& file : presetFiles()) {
        CAPTURE(file.path);
        const auto preset = parsePreset(file.text);
        REQUIRE(preset.has_value());

        RenderRequest request;
        request.params = preset->params;
        request.note = 29;
        request.bars = 2.0;
        request.oversampling = 4;
        request.sampleRate = kRate;
        RenderStats stats;
        const auto audio = analysis::deinterleave(renderOffline(request, &stats));
        REQUIRE(allFinite(audio.left));
        REQUIRE(allFinite(audio.right));

        // The same measurement window as tf-render --metrics.
        analysis::MetricsRequest q;
        q.sampleRate = kRate;
        q.subFreq = preset->params[ParamId::subFreq];
        q.xover = preset->params[ParamId::xover];
        q.steadyBegin = static_cast<std::size_t>(frames(0.25, kRate));
        q.steadyEnd = static_cast<std::size_t>(stats.heldSamples);
        const auto m = analysis::computeMetrics(audio, q);
        CAPTURE(m.subRmsRangeDb, m.truePeakDb, m.rmsDb);
        CHECK(m.subRmsRangeDb <= 0.5);
        CHECK(m.truePeakDb <= preset->params[ParamId::ceiling] + 0.1);
    }
}
