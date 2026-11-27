// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_json.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
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
    sanitise(p);

    std::string error;
    const auto back = parsePattern(serialisePattern(p, "Round Trip"), &error);
    REQUIRE(back.has_value());
    CHECK(*back == p);

    GrooveBank bank;
    bank.scenes[2] = p;
    bank.slicerLength = 12;
    bank.slicerSteps[3] = hit(96, Layer::groove);
    bank.slicerSteps[3].ratchet = 2;
    sanitise(bank);
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

TEST_CASE("Edits reach main, fill and slicer steps", "[groove][json]") {
    std::string error;
    const auto p = parsePattern(R"({
        "format": 3,
        "lanes": { "hat": { "groove": "x.x............." } },
        "fill": { "beats": 1, "lanes": { "snare": "............x.x." } },
        "edits": [
            { "lane": "hat", "step": 2, "velocity": 71, "ratchet": 3, "nudge": -6, "flam": true },
            { "lane": "snare", "step": 14, "fill": true, "ratchet": 2 }
        ]
    })",
                                &error);
    INFO(error);
    REQUIRE(p.has_value());
    CHECK(p->lanes[4].steps[2].velocity == 71);
    CHECK(p->lanes[4].steps[2].ratchet == 3);
    CHECK(p->lanes[4].steps[2].nudge == -6);
    CHECK(p->lanes[4].steps[2].flam);
    CHECK(p->fill.steps[1][14].ratchet == 2);

    const auto bank = parseGrooveBank(
        R"({"scenes":[],"slicer":{"length":8,"steps":"x..x....","edits":[{"step":3,"velocity":40,"flam":true}]}})",
        &error);
    INFO(error);
    REQUIRE(bank.has_value());
    CHECK(bank->slicerLength == 8);
    CHECK(bank->slicerSteps[3].velocity == 40);
    CHECK(bank->slicerSteps[3].flam);
    CHECK_FALSE(bank->slicerSteps[6].on()); // the file's strip replaces the default gate
}

TEST_CASE("A bank without a slicer object keeps the default gate", "[groove][json]") {
    const auto bank = parseGrooveBank(R"({"scenes":[]})");
    REQUIRE(bank.has_value());
    CHECK(bank->slicerSteps[3].on());
    CHECK(bank->slicerLength == 16);
}

TEST_CASE("Edits are strict about where they point", "[groove][json]") {
    std::string error;
    const char* head = R"({"format":3,"lanes":{"hat":{"groove":"x..............."}},"edits":[)";

    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"hat","step":16,"ratchet":2}]})", &error));
    CHECK(error.find("step") != std::string::npos);
    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"hat","step":-1,"ratchet":2}]})", &error));
    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"hat","ratchet":2}]})", &error));
    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"cowbell","step":0}]})", &error));
    CHECK_FALSE(parsePattern(std::string(head) + R"(7]})", &error));

    // An edit may not switch on a step that has no character.
    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"hat","step":4,"velocity":90}]})", &error));
    CHECK(error.find("off") != std::string::npos);

    // A fill edit before the fill window is ignored, as the spec says.
    CHECK(parsePattern(std::string(head) + R"({"lane":"hat","step":2,"fill":true,"ratchet":2}]})", &error));

    CHECK_FALSE(
        parseGrooveBank(R"({"slicer":{"length":8,"steps":"x.......","edits":[{"step":8,"ratchet":2}]}})", &error));
    CHECK_FALSE(
        parseGrooveBank(R"({"slicer":{"length":8,"steps":"x.......","edits":[{"step":1,"ratchet":2}]}})", &error));
    CHECK_FALSE(parseGrooveBank(R"({"slicer":{"length":8,"steps":"x.......","edits":[{"ratchet":2}]}})", &error));
    CHECK_FALSE(parseGrooveBank(R"({"slicer":{"length":8,"steps":"x.......","edits":[3]}})", &error));
}

TEST_CASE("Any sanitised bank survives a round trip unchanged", "[groove][json]") {
    std::uint32_t state = 20261126u;
    const auto next = [&state](int lo, int hi) {
        state = state * 1664525u + 1013904223u;
        return lo + static_cast<int>((state >> 8) % static_cast<std::uint32_t>(hi - lo + 1));
    };
    const auto randomStep = [&next](Step& s) {
        if (next(0, 3) != 0)
            return;
        s.velocity = static_cast<std::uint8_t>(next(1, 127));
        s.layer = static_cast<Layer>(next(0, 2));
        s.ratchet = static_cast<std::uint8_t>(next(1, 4));
        s.nudge = static_cast<std::int8_t>(next(-24, 24));
        s.flam = next(0, 4) == 0;
    };
    for (int round = 0; round < 20; ++round) {
        GrooveBank bank;
        for (auto& p : bank.scenes) {
            p.bars = static_cast<std::uint8_t>(1 << next(0, 2));
            p.fill.beats = static_cast<std::uint8_t>(1 << next(0, 2));
            for (auto& lane : p.lanes) {
                for (auto& s : lane.steps)
                    randomStep(s);
                lane.length = static_cast<std::uint8_t>(next(0, 64));
                lane.midiNote = static_cast<std::uint8_t>(next(0, 127));
                lane.offsetMs = static_cast<std::int8_t>(next(-20, 20));
                lane.muted = next(0, 5) == 0;
                lane.solo = next(0, 9) == 0;
            }
            for (auto& row : p.fill.steps)
                for (auto& s : row)
                    randomStep(s);
        }
        bank.slicerLength = static_cast<std::uint8_t>(next(1, 32));
        for (auto& s : bank.slicerSteps)
            randomStep(s);
        sanitise(bank);

        std::string error;
        const auto back = parseGrooveBank(serialiseGrooveBank(bank), &error);
        INFO("round " << round << ": " << error);
        REQUIRE(back.has_value());
        CHECK(*back == bank);
    }
}
