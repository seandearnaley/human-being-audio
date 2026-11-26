// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_json.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cl;

namespace {

Step hit(int velocity, Layer layer) {
    Step s;
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = layer;
    return s;
}

} // namespace

TEST_CASE("A pattern file reads step strings into layers and velocities", "[groove][json]") {
    const std::string json = R"({
        "format": 3, "name": "Two-Step", "bars": 1,
        "lanes": {
            "kick":  { "core": "X.........x.....", "groove": "..x.............", "detail": ".......o........" },
            "snare": { "core": "....X.......X..." },
            "hat":   { "core": "x.x.x.x.x.x.x.x.", "offsetMs": -1 }
        },
        "fill": { "beats": 2, "lanes": { "snare": "........x.x.x.xX" } },
        "edits": [ { "lane": "hat", "step": 14, "ratchet": 2 } ]
    })";
    std::string error;
    std::string name;
    const auto p = parsePattern(json, &error, &name);
    REQUIRE(p.has_value());
    CHECK(name == "Two-Step");
    CHECK(p->lanes[0].steps[0].velocity == kVelocityAccent);
    CHECK(p->lanes[0].steps[0].layer == Layer::core);
    CHECK(p->lanes[0].steps[2].layer == Layer::groove);
    CHECK(p->lanes[0].steps[7].velocity == kVelocitySoft);
    CHECK(p->lanes[0].steps[7].layer == Layer::detail);
    CHECK(p->lanes[4].offsetMs == -1);
    CHECK(p->lanes[4].steps[14].ratchet == 2);
    CHECK(p->fill.beats == 2);
    CHECK(p->fill.steps[1][15].velocity == kVelocityAccent);
    CHECK_FALSE(p->fill.steps[1][0].on());
}

TEST_CASE("Patterns and banks round-trip with every field set", "[groove][json]") {
    Pattern p = defaultBeat();
    p.bars = 2;
    p.lanes[4].steps[14].ratchet = 3;
    p.lanes[1].steps[20] = hit(73, Layer::detail);
    p.lanes[1].steps[20].nudge = -6;
    p.lanes[6].length = 7;
    p.lanes[6].offsetMs = -3;
    p.lanes[2].midiNote = 40;
    p.lanes[3].muted = true;
    p.lanes[5].solo = true;
    p.fill.beats = 1;
    p.fill.steps[1][15] = hit(118, Layer::groove);
    p.fill.steps[1][15].flam = true;

    std::string error;
    const auto back = parsePattern(serialisePattern(p, "Round Trip"), &error);
    REQUIRE(back.has_value());
    CHECK(*back == p);

    GrooveBank bank;
    bank.scenes[2] = p;
    bank.slicerLength = 12;
    bank.slicerSteps[3] = hit(96, Layer::groove);
    bank.slicerSteps[3].ratchet = 2;
    const auto bankBack = parseGrooveBank(serialiseGrooveBank(bank), &error);
    REQUIRE(bankBack.has_value());
    CHECK(*bankBack == bank);
}

TEST_CASE("Bad pattern files are rejected with a reason", "[groove][json]") {
    std::string error;

    CHECK_FALSE(parsePattern(R"({"bars":1})", &error));
    CHECK(error == kOldFormatError);
    CHECK_FALSE(parsePattern(R"({"format":2,"bars":1})", &error));
    CHECK(error == kOldFormatError);

    CHECK_FALSE(parsePattern(R"({"format":3,"lanes":{"kick":{"core":"x...............","groove":"x..............."}}})",
                             &error));
    CHECK(error.find("more than one layer") != std::string::npos);

    CHECK_FALSE(parsePattern(R"({"format":3,"lanes":{"kick":{"core":"x..."}}})", &error));
    CHECK(error.find("expected 16") != std::string::npos);

    CHECK_FALSE(parsePattern(R"({"format":3,"bars":3})", &error));
    CHECK_FALSE(parsePattern(R"({"format":3,"lanes":{"cowbell":{"core":"x..............."}}})", &error));
    CHECK_FALSE(parsePattern(R"({"format":3,"lanes":{"kick":{"core":"x......?........"}}})", &error));
    CHECK_FALSE(parsePattern("not json", &error));
}

TEST_CASE("Fill steps before the fill window are ignored on read and not written", "[groove][json]") {
    std::string error;
    const auto p = parsePattern(R"({
        "format": 3,
        "fill": { "beats": 1, "lanes": { "snare": "x.......x.....xX" } },
        "edits": [ { "lane": "snare", "step": 2, "fill": true, "velocity": 100 } ]
    })",
                                &error);
    REQUIRE(p.has_value());
    for (std::size_t step = 0; step < 12; ++step)
        CHECK_FALSE(p->fill.steps[1][step].on());
    CHECK(p->fill.steps[1][14].on());
    CHECK(p->fill.steps[1][15].velocity == kVelocityAccent);

    Pattern drawn{};
    drawn.fill.beats = 2;
    drawn.fill.steps[0][4] = hit(96, Layer::groove); // before the 2-beat window
    drawn.fill.steps[0][8] = hit(96, Layer::groove); // inside it
    const auto back = parsePattern(serialisePattern(drawn), &error);
    REQUIRE(back.has_value());
    CHECK_FALSE(back->fill.steps[0][4].on());
    CHECK(back->fill.steps[0][8].on());
}
