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

TEST_CASE("All 12 cubes x 8 corners x 3 topologies have bounded passband auto-makeup", "[core]") {
    constexpr double fs = 192000.0; // 4x oversampled 48 kHz
    for (int c = 0; c < pf::kNumCubes; ++c) {
        for (int topo = 0; topo < 3; ++topo) {
            for (float z : {0.0f, 1.0f}) {
                for (float y : {0.0f, 1.0f}) {
                    for (float x : {0.0f, 1.0f}) {
                        const auto lat =
                            pf::resolveLattice(c, x, y, z, 0.0f, 0.55f, 0.0f, 0.0f, nullptr, pf::kNumStages, topo);
                        double linPow = 0.0;
                        float maxDb = -200.0f;
                        constexpr int kProbes = 25;
                        for (int i = 0; i < kProbes; ++i) {
                            const double t = static_cast<double>(i) / static_cast<double>(kProbes - 1);
                            const double hz = 75.0 * std::pow(12500.0 / 75.0, t);
                            const float db = pf::evaluateMagnitudeDb(lat, pf::kNumStages, topo, hz, fs);
                            REQUIRE(std::isfinite(db));
                            maxDb = std::max(maxDb, db);
                            const double lin = std::pow(10.0, static_cast<double>(db) / 20.0);
                            linPow += lin * lin;
                        }
                        const double rmsDb = 10.0 * std::log10(std::max(linPow / kProbes, 1e-12));
                        INFO("cube=" << c << " topo=" << topo << " (" << x << "," << y << "," << z
                                     << ") rmsDb=" << rmsDb << " maxDb=" << maxDb);
                        REQUIRE(rmsDb > -10.0);
                        REQUIRE(rmsDb < 10.0);
                        REQUIRE(maxDb < 28.0f);
                    }
                }
            }
        }
    }
}

TEST_CASE("Rebuilt Burg LPC handles DC offset, 192 kHz decimation, and pure sine without duplicate stacking",
          "[core]") {
    // 1) Heavy DC offset + 192 kHz sample rate still recovers 1200 Hz and 2800 Hz formants.
    {
        constexpr double fs = 192000.0;
        constexpr int n = 16384;
        std::vector<float> audio(n, 0.0f);
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / fs;
            audio[static_cast<std::size_t>(i)] =
                static_cast<float>(4.5 + 0.6 * std::sin(2.0 * 3.141592653589793 * 1200.0 * t) +
                                   0.45 * std::sin(2.0 * 3.141592653589793 * 2800.0 * t));
        }
        const auto corner = pf::imprintFromAudio(audio, fs);
        bool found1200 = false, found2800 = false;
        for (const auto& st : corner.stages) {
            REQUIRE(std::isfinite(st.poleHz));
            if (std::abs(st.poleHz - 1200.0f) < 120.0f)
                found1200 = true;
            if (std::abs(st.poleHz - 2800.0f) < 220.0f)
                found2800 = true;
        }
        REQUIRE(found1200);
        REQUIRE(found2800);
    }

    // 2) Pure single sine produces 7 distinct sorted stages (no duplicate-pole stacking).
    {
        constexpr double fs = 48000.0;
        constexpr int n = 4096;
        std::vector<float> sine(n, 0.0f);
        for (int i = 0; i < n; ++i)
            sine[static_cast<std::size_t>(i)] =
                static_cast<float>(0.8 * std::sin(2.0 * 3.141592653589793 * 640.0 * static_cast<double>(i) / fs));
        const auto corner = pf::imprintFromAudio(sine, fs);
        for (std::size_t i = 1; i < pf::kNumStages; ++i) {
            REQUIRE(corner.stages[i].poleHz > corner.stages[i - 1].poleHz * 1.05f);
        }
    }
}

TEST_CASE("Multi-corner 4-slot Imprint JSON round-trips cleanly", "[core]") {
    pf::Preset p;
    p.name = "MultiSlot";
    p.category = "Talk";
    p.imprint.setSlot(0, pf::cubeAt(1).corners[0]);
    p.imprint.setSlot(2, pf::cubeAt(3).corners[1]);

    const auto text = pf::serialisePreset(p);
    std::string err;
    const auto loaded = pf::parsePreset(text, &err);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->imprint.slotEnabled(0));
    REQUIRE_FALSE(loaded->imprint.slotEnabled(1));
    REQUIRE(loaded->imprint.slotEnabled(2));
    REQUIRE_FALSE(loaded->imprint.slotEnabled(3));
    REQUIRE(loaded->imprint.active);
    REQUIRE(loaded->imprint.slotCorner(2).stages[0].poleHz == Catch::Approx(pf::cubeAt(3).corners[1].stages[0].poleHz));
}
