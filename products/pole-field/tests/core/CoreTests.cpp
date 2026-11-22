#include "pf/cubes.h"
#include "pf/lanes.h"
#include "pf/params.h"
#include "pf/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("Parameter table maps and formats cleanly", "[core]") {
    for (int i = 0; i < pf::kParamCount; ++i) {
        const auto id = static_cast<pf::ParamId>(i);
        const auto& s = pf::spec(id);
        REQUIRE(pf::findParam(s.key) == id);
        const float norm = pf::toNormalised(s, s.def);
        REQUIRE(norm >= 0.0f);
        REQUIRE(norm <= 1.0f);
        const float back = pf::fromNormalised(s, norm);
        REQUIRE(back == Catch::Approx(s.def).margin(1e-3f));
        REQUIRE(!pf::formatValue(s, s.def).empty());
    }
}

TEST_CASE("All factory Z-plane cubes resolve strictly inside stability bounds", "[core]") {
    for (int c = 0; c < pf::kNumCubes; ++c) {
        for (float x : { 0.0f, 0.5f, 1.0f }) {
            for (float y : { 0.0f, 0.5f, 1.0f }) {
                for (float z : { 0.0f, 1.0f }) {
                    const auto lat = pf::resolveLattice(c, x, y, z, 0.0f, 1.0f, 0.5f, 2.0f);
                    REQUIRE(std::isfinite(lat.makeupLinear));
                    for (const auto& s : lat.stages) {
                        const auto zp = pf::stageZPoint(s, 48000.0);
                        REQUIRE(zp.poleR > 0.0f);
                        REQUIRE(zp.poleR < 1.0f);
                        REQUIRE(std::isfinite(zp.poleTheta));
                    }
                    const float db = pf::evaluateMagnitudeDb(lat, pf::kNumStages, 0, 1000.0, 48000.0);
                    REQUIRE(std::isfinite(db));
                }
            }
        }
    }
}

TEST_CASE("Burg LPC audio imprint recovers synthetic formant frequency", "[core]") {
    constexpr double fs = 48000.0;
    constexpr int n = 4096;
    std::vector<float> audio(n, 0.0f);
    // Synthesise a crisp multi-formant signal centred strongly at 1200 Hz + 2800 Hz.
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double env = std::exp(-t * 4.0);
        audio[static_cast<std::size_t>(i)] = static_cast<float>(
            env * (0.7 * std::sin(2.0 * 3.141592653589793 * 1200.0 * t)
                 + 0.45 * std::sin(2.0 * 3.141592653589793 * 2800.0 * t)
                 + 0.25 * std::sin(2.0 * 3.141592653589793 * 480.0 * t)));
    }
    const auto corner = pf::imprintFromAudio(audio, fs);
    bool found1200 = false;
    bool found2800 = false;
    for (const auto& st : corner.stages) {
        REQUIRE(std::isfinite(st.poleHz));
        REQUIRE(st.poleHz >= 55.0f);
        REQUIRE(st.poleQ >= 1.2f);
        if (std::abs(st.poleHz - 1200.0f) < 90.0f) found1200 = true;
        if (std::abs(st.poleHz - 2800.0f) < 180.0f) found2800 = true;
    }
    REQUIRE(found1200);
    REQUIRE(found2800);
}

TEST_CASE("Preset JSON round-trips parameters, custom curves, and imprint", "[core]") {
    pf::Preset p;
    p.name = "Roundtrip";
    p.category = "Neuro";
    p.params[pf::ParamId::morphX] = 0.77f;
    p.params[pf::ParamId::cube] = 7.0f;
    p.params[pf::ParamId::lane1Shape] = static_cast<float>(pf::LaneShape::custom);
    p.curves[0] = pf::curves::triangle();
    p.imprint.active = true;
    p.imprint.corner = pf::cubeAt(0).corners[0];

    const auto text = pf::serialisePreset(p);
    std::string err;
    const auto loaded = pf::parsePreset(text, &err);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->name == "Roundtrip");
    REQUIRE(loaded->params[pf::ParamId::morphX] == Catch::Approx(0.77f).margin(1e-4f));
    REQUIRE(loaded->curves[0] == p.curves[0]);
    REQUIRE(loaded->imprint.active);
    REQUIRE(loaded->imprint.corner.stages[0].poleHz == Catch::Approx(p.imprint.corner.stages[0].poleHz));
}
