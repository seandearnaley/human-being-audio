// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_state.h"
#include "cl/hits.h"
#include "cl/resolve.h"
#include "cl/smf.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace cl;

namespace {

struct SmfNote {
    std::uint32_t tick = 0;
    int note = 0;
    int velocity = 0;
};

struct Smf {
    int ppq = 0;
    std::uint32_t endTick = 0;
    std::vector<SmfNote> noteOns;
    int noteOffs = 0;
    int pitchBends = 0;
};

// Reads the one track that encodeSmfType0 writes: full status bytes, a tempo meta event, end of track.
Smf parse(const std::vector<std::uint8_t>& bytes) {
    Smf out;
    REQUIRE(bytes.size() > 22);
    REQUIRE(bytes[0] == 'M');
    REQUIRE(bytes[1] == 'T');
    REQUIRE(bytes[2] == 'h');
    REQUIRE(bytes[3] == 'd');
    out.ppq = (static_cast<int>(bytes[12]) << 8) | static_cast<int>(bytes[13]);
    std::size_t pos = 22;
    std::uint32_t tick = 0;
    while (pos < bytes.size()) {
        std::uint32_t delta = 0;
        while (pos < bytes.size()) {
            const std::uint8_t b = bytes[pos++];
            delta = (delta << 7u) | (b & 0x7Fu);
            if ((b & 0x80u) == 0u)
                break;
        }
        tick += delta;
        REQUIRE(pos < bytes.size());
        const std::uint8_t status = bytes[pos];
        if (status == 0xFFu) {
            const std::uint8_t type = bytes[pos + 1];
            const std::uint8_t length = bytes[pos + 2];
            if (type == 0x2Fu) {
                out.endTick = tick;
                break;
            }
            pos += 3u + length;
            continue;
        }
        const std::uint8_t hi = status & 0xF0u;
        if (hi == 0x90u && bytes[pos + 2] > 0)
            out.noteOns.push_back({tick, bytes[pos + 1], bytes[pos + 2]});
        else if (hi == 0x80u || hi == 0x90u)
            ++out.noteOffs;
        else if (hi == 0xE0u)
            ++out.pitchBends;
        pos += (hi == 0xC0u || hi == 0xD0u) ? 2u : 3u;
    }
    return out;
}

int count(const Smf& smf, int note, std::uint32_t fromTick, std::uint32_t toTick) {
    int n = 0;
    for (const auto& on : smf.noteOns)
        n += (on.note == note && on.tick >= fromTick && on.tick < toTick) ? 1 : 0;
    return n;
}

// Two bars. Kick on 1 and 3, hat on the 8ths, one Detail ghost, and a two-beat snare fill.
GrooveBank exportBank() {
    GrooveBank bank;
    Pattern& p = bank.scenes[0];
    setPatternBars(p, 2);
    p.lanes[2].steps[7].velocity = 56;
    p.lanes[2].steps[7].layer = Layer::detail;
    p.lanes[4].steps[22].ratchet = 2;
    p.fill.beats = 2;
    p.fill.steps[1][8].velocity = 96;
    p.fill.steps[1][10].velocity = 96;
    p.fill.steps[1][12].velocity = 118;
    return bank;
}

} // namespace

TEST_CASE("Export writes the pattern once through at the current Rate", "[smf]") {
    const GrooveBank bank; // default beat, one bar, 12 note-ons
    for (int rate = 0; rate < 3; ++rate) {
        Params p;
        p[ParamId::seqRate] = static_cast<float>(rate);
        const Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 172.0, 0, bank));
        INFO("rate " << rate);
        CHECK(smf.ppq == 960);
        CHECK(smf.endTick == (rate == 0 ? 7680u : (rate == 1 ? 3840u : 1920u)));
        CHECK(smf.noteOns.size() == 12);
        CHECK(smf.noteOffs == 12);
        CHECK(smf.pitchBends == 0);
        for (const auto& on : smf.noteOns)
            CHECK(on.tick < smf.endTick);
    }
}

TEST_CASE("Export plays what resolveBar reports, with Density and Dynamics applied", "[smf]") {
    const GrooveBank bank = exportBank();
    const GrooveState groove(bank);
    for (const float density : {0.0f, 0.5f, 1.0f}) {
        Params p;
        p[ParamId::density] = density;
        p[ParamId::dynamics] = 0.8f;
        p[ParamId::noteMap] = static_cast<float>(NoteMap::chromatic);
        const Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 140.0, 0, bank));

        int expected = 0;
        for (int bar = 0; bar < 2; ++bar) {
            ResolvedBar resolved;
            resolveBar(groove.bank.scenes[0], bar, {density, 0.8f, false}, resolved);
            for (const auto& lane : resolved.lanes)
                for (const auto& s : lane)
                    expected += s.velocity > 0 ? static_cast<int>(s.ratchet) + (s.flam ? 1 : 0) : 0;
        }
        INFO("density " << density);
        CHECK(static_cast<int>(smf.noteOns.size()) == expected);
        CHECK(count(smf, 38, 0, smf.endTick) == (density >= 1.0f ? 1 : 0)); // the Detail ghost, chromatic lane 2
        for (const auto& on : smf.noteOns) {
            if (on.note == 36)
                CHECK(on.velocity == applyDynamics(96, 0.8f));
        }
    }
}

TEST_CASE("The last bar of an export has the fill when Fill is not Off", "[smf]") {
    const GrooveBank bank = exportBank();
    Params p;
    p[ParamId::noteMap] = static_cast<float>(NoteMap::chromatic); // snare is note 37
    Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 140.0, 0, bank));
    CHECK(smf.endTick == 7680u);
    CHECK(count(smf, 37, 3840u, 7680u) == 2); // the main pattern's backbeats in bar 2

    p[ParamId::fillEvery] = 2.0f;
    smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 140.0, 0, bank));
    CHECK(count(smf, 37, 0u, 3840u) == 2);            // bar 1 is untouched
    CHECK(count(smf, 37, 3840u, 3840u + 1920u) == 1); // beat 2 of bar 2, before the fill window
    CHECK(count(smf, 37, 3840u + 1920u, 7680u) == 3); // the drawn fill replaces the snare lane's last two beats
    CHECK(count(smf, 36, 3840u + 1920u, 7680u) == 1); // the kick draws nothing in the fill and plays through
}

TEST_CASE("A hit before bar 1 or after the last bar wraps to the other end of the clip", "[smf]") {
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    bank.scenes[0].lanes[0].steps[0].velocity = 96;
    bank.scenes[0].lanes[0].steps[0].layer = Layer::core;
    bank.scenes[0].lanes[0].steps[0].flam = true; // grace hit 20 ms before beat 0
    bank.scenes[0].lanes[1].steps[15].velocity = 96;
    bank.scenes[0].lanes[1].steps[15].layer = Layer::core;
    bank.scenes[0].lanes[1].steps[15].nudge = 24; // lands on the next bar's downbeat
    Params p;
    const Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 120.0, 0, bank));
    REQUIRE(smf.noteOns.size() == 3);
    CHECK(count(smf, 36, 0u, 1u) == 1);       // the kick itself
    CHECK(count(smf, 36, 3780u, 3840u) == 1); // its grace hit: 0.04 beats before the loop point
    CHECK(count(smf, 38, 0u, 20u) == 1);      // the late snare, wrapped to the start
    CHECK(smf.noteOffs == 3);
}

TEST_CASE("Two lanes that share a note on the same step export one note, the louder one", "[smf]") {
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    bank.scenes[0].lanes[0].steps[0].velocity = 96; // kick
    bank.scenes[0].lanes[0].steps[0].layer = Layer::core;
    bank.scenes[0].lanes[1].steps[0].velocity = 118; // snare: the Strike Body map plays both as note 36
    bank.scenes[0].lanes[1].steps[0].layer = Layer::core;
    Params p;
    p[ParamId::noteMap] = static_cast<float>(NoteMap::strikeBody);
    const Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 120.0, 0, bank));
    REQUIRE(smf.noteOns.size() == 1);
    CHECK(smf.noteOns[0].note == 36);
    CHECK(smf.noteOns[0].velocity == 118);
    CHECK(smf.noteOffs == 1);
}

TEST_CASE("Slicer export writes the compiled phrase, and Clip Slice writes no notes", "[smf]") {
    const GrooveBank bank;
    HeldNotes held;
    held.push(48, 100);
    held.push(55, 100);
    Params p;
    p[ParamId::engineMode] = static_cast<float>(EngineMode::slicer);
    Smf smf = parse(exportSmf(p, defaultLaneCurves(), held, 120.0, 0, bank));
    CHECK(smf.endTick == 3840u);
    CHECK(smf.noteOns.size() == 10); // five lit gate steps, two notes each
    for (const auto& on : smf.noteOns)
        CHECK((on.note == 48 || on.note == 55));

    p[ParamId::slicerStyle] = static_cast<float>(SlicerStyle::clipSlice);
    smf = parse(exportSmf(p, defaultLaneCurves(), held, 120.0, 0, bank));
    CHECK(smf.noteOns.empty());
    CHECK(smf.endTick == 3840u);
}
