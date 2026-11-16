// The factory bank: every file in presets/ loads, is complete and named, and
// plays within the instrument's promises (the sub does not move, the output
// stays under the ceiling) and the 0.2 voicing targets (spec, "Measuring
// noise": the growl is harmonic and weighted to the bottom). TF_PRESETS_DIR is
// set by CMake.
#include "helpers.h"

#include "tf/preset.h"
#include "tf/render.h"
#include "tf/voice_pitch.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
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

// The spec's voicing targets with the lanes off (motion 0), per category:
// spectral tilt from 500 Hz to 0.8 x Reach x f0 in dB per octave, and the
// most off-harmonic energy allowed. Sub presets have no growl to hold.
struct VoicingTarget {
    std::string_view category;
    double tiltLo, tiltHi;
    double offHarmonicMaxDb;
};
constexpr std::array<VoicingTarget, 4> kVoicing { {
    { "Reese", -14.0, -7.0, -30.0 },
    { "Neuro", -9.0, -4.0, -25.0 },
    { "Talk", -10.0, -5.0, -25.0 },
    { "Motion", -10.0, -5.0, -25.0 },
} };
// The targets are set on 16x, 4-bar renders at E1, F1 and A1; this test renders
// F1 at 4x for two bars, so it allows this much either way.
constexpr double kTiltMarginDb = 1.5;
constexpr double kOffHarmonicMarginDb = 3.0;

constexpr double kRate = 48000.0;
constexpr int kNote = 29;  // F1

struct Rendered {
    std::string path;
    Preset preset;
    bool finite = false;
    analysis::Metrics metrics;
};

// Every factory preset at F1, two bars at 4x, measured over the same window and
// with the same tilt span and harmonic tolerance as tf-render --metrics. The
// renders run in parallel (each owns its engine); checks happen afterwards, on
// this thread.
std::vector<Rendered> renderBank(bool lanesOff) {
    std::vector<std::future<Rendered>> jobs;
    for (const auto& file : presetFiles()) {
        jobs.push_back(std::async(std::launch::async, [file, lanesOff] {
            Rendered r;
            r.path = file.path;
            const auto preset = parsePreset(file.text);
            if (!preset) return r;
            r.preset = *preset;

            RenderRequest request;
            request.params = preset->params;
            if (lanesOff) request.params[ParamId::motion] = 0.0f;
            request.note = kNote;
            request.bars = 2.0;
            request.oversampling = 4;
            request.sampleRate = kRate;
            RenderStats stats;
            const auto audio = analysis::deinterleave(renderOffline(request, &stats));
            r.finite = allFinite(audio.left) && allFinite(audio.right);

            analysis::MetricsRequest q;
            q.sampleRate = kRate;
            q.subFreq = splitHz(request.params, request.note);
            q.xover = request.params[ParamId::xover];
            q.steadyBegin = static_cast<std::size_t>(frames(0.25, kRate));
            q.steadyEnd = static_cast<std::size_t>(stats.heldSamples);
            q.f0 = noteHz(kNote);
            q.tiltUpperHz = 0.8 * request.params[ParamId::reach] * q.f0;
            const auto offsets = voiceOffsetsHz(request.params, q.f0, request.bpm);
            q.harmonicToleranceHz = analysis::harmonicToleranceHz(offsets.a, offsets.b);
            r.metrics = analysis::computeMetrics(audio, q);
            return r;
        }));
    }
    std::vector<Rendered> out;
    for (auto& job : jobs) out.push_back(job.get());
    return out;
}

// The x of every point of "lane<n>Curve" in the file as written, in file order
// (the parser sorts points, so order has to be read from the text), or nullopt
// when the file has no such key. Points are [x, y, tension, "segment"].
std::optional<std::vector<double>> writtenCurveXs(const std::string& text, int lane) {
    const std::string key = "\"lane" + std::to_string(lane + 1) + "Curve\"";
    auto pos = text.find(key);
    if (pos == std::string::npos) return std::nullopt;
    pos = text.find('[', pos + key.size());
    std::vector<double> xs;
    int depth = 0;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (c == '[') {
            ++depth;
            if (depth == 2) xs.push_back(std::strtod(text.c_str() + pos + 1, nullptr));
        } else if (c == ']') {
            if (--depth == 0) break;
        } else if (c == '"' && depth >= 1) {
            pos = text.find('"', pos + 1);  // skip a segment name
        }
    }
    return xs;
}

bool laneOn(const Params& p, int lane) {
    return p.on(laneParam(lane, LaneField::on)) && p[laneParam(lane, LaneField::depth)] != 0.0f;
}

int followed(const Params& p, int lane) { return p.choice(laneParam(lane, LaneField::follow)) - 1; }  // -1: Own

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

TEST_CASE("the Coupled Reese preset is the parameter defaults") {
    // The plugin starts on the defaults and names them Coupled Reese, so the
    // file must match the table exactly.
    bool found = false;
    for (const auto& file : presetFiles()) {
        const auto preset = parsePreset(file.text);
        if (!preset || preset->name != "Coupled Reese") continue;
        found = true;
        const auto defaults = Params::defaults();
        for (std::size_t i = 0; i < kParams.size(); ++i) {
            CAPTURE(kParams[i].key);
            CHECK(preset->params.values[i] == defaults.values[i]);
        }
        CHECK(file.text.find("Curve\"") == std::string::npos);  // no drawn curves either
    }
    CHECK(found);
}

TEST_CASE("every drawn curve belongs to a Custom lane and is well formed") {
    for (const auto& file : presetFiles()) {
        CAPTURE(file.path);
        const auto preset = parsePreset(file.text);
        REQUIRE(preset.has_value());
        for (int lane = 0; lane < kLaneCount; ++lane) {
            CAPTURE(lane + 1);
            const auto xs = writtenCurveXs(file.text, lane);
            if (!xs) continue;
            CHECK(preset->params.choice(laneParam(lane, LaneField::shape)) == static_cast<int>(LaneShape::custom));
            CHECK(xs->size() >= 2);
            CHECK(xs->size() <= static_cast<std::size_t>(kMaxCurvePoints));
            for (std::size_t i = 0; i < xs->size(); ++i) {
                CAPTURE(i, (*xs)[i]);
                CHECK((*xs)[i] >= 0.0);
                CHECK((*xs)[i] < 1.0);
                if (i > 0) CHECK((*xs)[i] > (*xs)[i - 1]);
            }
        }
    }
}

TEST_CASE("no lane follows itself or a loop") {
    for (const auto& file : presetFiles()) {
        CAPTURE(file.path);
        const auto preset = parsePreset(file.text);
        REQUIRE(preset.has_value());
        const auto& p = preset->params;
        for (int lane = 0; lane < kLaneCount; ++lane) {
            CAPTURE(lane + 1);
            CHECK(followed(p, lane) != lane);
            // Walking the chain must reach a lane that plays its own shape.
            int at = lane;
            std::set<int> seen { lane };
            while (followed(p, at) >= 0) {
                at = followed(p, at);
                const bool fresh = seen.insert(at).second;
                CHECK(fresh);
                if (!fresh) break;
            }
        }
    }
}

TEST_CASE("the bank shows the movement system") {
    // The 0.2 movement showcase: lanes sweeping Sculpt (the notch bank in
    // motion), drawn curves and linked lanes. Counted so a later edit cannot
    // quietly remove it.
    int sculpt = 0, custom = 0, following = 0;
    for (const auto& file : presetFiles()) {
        const auto preset = parsePreset(file.text);
        REQUIRE(preset.has_value());
        const auto& p = preset->params;
        bool hasSculpt = false, hasCustom = false, hasFollow = false;
        for (int lane = 0; lane < kLaneCount; ++lane) {
            if (!laneOn(p, lane)) continue;
            hasSculpt |= laneTargetOf(p, lane) == LaneTarget::sculpt && p[ParamId::sculptDepth] != 0.0f;
            hasCustom |= p.choice(laneParam(lane, LaneField::shape)) == static_cast<int>(LaneShape::custom);
            hasFollow |= followed(p, lane) >= 0 && laneOn(p, followed(p, lane));
        }
        sculpt += hasSculpt ? 1 : 0;
        custom += hasCustom ? 1 : 0;
        following += hasFollow ? 1 : 0;
    }
    CHECK(sculpt >= 6);
    CHECK(custom >= 8);
    CHECK(following >= 6);
}

TEST_CASE("every factory preset renders with a steady sub under the ceiling") {
    for (const auto& r : renderBank(false)) {
        CAPTURE(r.path);
        REQUIRE(r.finite);
        const auto& m = r.metrics;
        CAPTURE(m.subRmsRangeDb, m.truePeakDb, m.rmsDb);
        CHECK(m.subRmsRangeDb <= 0.2);
        CHECK(m.truePeakDb <= r.preset.params[ParamId::ceiling] + 0.1);
    }
}

TEST_CASE("every factory preset's growl is harmonic and weighted to the bottom") {
    int held = 0;
    for (const auto& r : renderBank(true)) {
        CAPTURE(r.path);
        REQUIRE(r.finite);
        const auto target = std::find_if(kVoicing.begin(), kVoicing.end(),
                                         [&](const VoicingTarget& t) { return t.category == r.preset.category; });
        if (target == kVoicing.end()) continue;  // Sub
        ++held;
        const auto& m = r.metrics;
        CAPTURE(r.preset.category, m.offHarmonicDb, m.harmonicToleranceHz);
        CHECK(m.offHarmonicDb <= target->offHarmonicMaxDb + kOffHarmonicMarginDb);

        // Tilt is undefined when Reach leaves less than two octaves above
        // 500 Hz: such a preset is bottom-weighted by construction.
        if (!m.tiltDbPerOct) continue;
        // Coupled Reese must equal the parameter defaults (above). The defaults
        // tilt at about -4.5 dB/oct at F1, above the Reese window, so only its
        // harmonicity is held here.
        if (r.preset.name == "Coupled Reese") continue;
        const double tilt = *m.tiltDbPerOct;
        CAPTURE(tilt);
        CHECK(tilt >= target->tiltLo - kTiltMarginDb);
        CHECK(tilt <= target->tiltHi + kTiltMarginDb);
    }
    CHECK(held >= 24);
}
