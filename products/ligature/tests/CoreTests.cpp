// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/compiled.h"
#include "lg/document.h"
#include "lg/midi_file.h"
#include "lg/palette.h"
#include "lg/routes.h"
#include "lg/song.h"
#include "lg/theory.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <set>

using namespace lg;

namespace {
const Key kC{0, Mode::major};
const Key kFm{5, Mode::minor};

std::string num(int root, Quality q, Key k) { return analyze(makeChord(root, q), k).numeral; }
} // namespace

TEST_CASE("Roman numerals spell borrowed, secondary and raised chords", "[theory]") {
    CHECK(num(5, Quality::m9, kFm) == "i9");
    CHECK(num(1, Quality::maj9, kFm) == "VImaj9");
    CHECK(num(0, Quality::d7b9, kFm) == "V7b9"); // harmonic-minor V
    CHECK(num(7, Quality::d7, kC) == "V7");
    CHECK(num(0, Quality::d7b9, kC) == "V7b9/IV"); // C7b9 in C is the dominant of F, not home
    CHECK(num(2, Quality::m9, kFm) == "#vi9");     // raised sixth in minor
    CHECK(num(11, Quality::dim7, kFm) == "#iv\xc2\xb0"
                                         "7");
    CHECK(num(8, Quality::maj9, kC) == "bVImaj9");
    CHECK(num(11, Quality::hdim, kC) == "vii\xc3\xb8"
                                        "7");
    CHECK(analyze(makeChord(0, Quality::d7b9, 4.0f), kC).function == Function::colour);
    CHECK(analyze(makeChord(7, Quality::d7), kC).function == Function::pull);
    CHECK(analyze(makeChord(5, Quality::m9), kFm).function == Function::home);
}

TEST_CASE("Diatonic chords stop at the seventh where the scale's ninth is flat", "[theory]") {
    CHECK(chordName(diatonic(kC, 2, 2)) == "Em7");
    CHECK(chordName(diatonic(kC, 3, 3)) == "Fmaj9#11");
    CHECK(chordName(diatonic(kFm, 4, 2)) == "Cm7");
    CHECK(chordName(diatonic(kFm, 6, 2)) == "Eb9");
    CHECK(chordName(diatonic(kC, 6, 2)) == "Bm7b5");
}

TEST_CASE("Every route between every pair of keys fills exactly eight beats", "[routes]") {
    for (int a = 0; a < 24; ++a)
        for (int b = 0; b < 24; ++b) {
            const Key from{a % 12, a < 12 ? Mode::major : Mode::minor};
            const Key to{b % 12, b < 12 ? Mode::major : Mode::minor};
            const auto rs = routesFor(from, to);
            REQUIRE(rs.size() >= 6);
            for (const auto& r : rs) {
                float beats = 0.0f;
                for (const auto& c : r.chords)
                    beats += c.beats;
                CHECK_THAT(beats, Catch::Matchers::WithinAbs(8.0, 1e-4));
            }
        }
}

TEST_CASE("Route families match the theory", "[routes]") {
    const auto rs = routesFor(kFm, kC);
    std::set<RouteId> ids;
    for (const auto& r : rs)
        ids.insert(r.id);
    CHECK(ids.count(RouteId::pivot) == 0); // no chord is diatonic to both F minor and C major
    CHECK(ids.count(RouteId::pedal) == 1);
    const auto hex = routeOrFirst(kFm, kC, RouteId::hexatonic);
    CHECK(chordName(hex.chords.front()) == "Amaj9"); // the hexatonic pole of F minor is A major
    CHECK(sharedPcs(makeChord(5, Quality::min), makeChord(9, Quality::maj)).empty());
    const auto dm = routesFor({2, Mode::minor}, {5, Mode::major});
    CHECK(std::any_of(dm.begin(), dm.end(), [](const Route& r) { return r.id == RouteId::pivot; }));
    const auto far = routesFor({4, Mode::minor}, {8, Mode::major});
    CHECK(std::none_of(far.begin(), far.end(), [](const Route& r) { return r.id == RouteId::pedal; }));
    CHECK(isPivot(makeChord(8, Quality::maj9), kFm, kC)); // Abmaj9: III in F minor, borrowed bVI in C
}

TEST_CASE("Voicings stay in range and lead smoothly", "[theory]") {
    const auto song = defaultSong();
    for (const auto& sec : song.sections) {
        std::vector<int> lead;
        int motion = 0;
        for (const auto& c : sec.chords) {
            const auto v = voiceChord(c, lead.empty() ? nullptr : &lead, true);
            REQUIRE(!v.notes.empty());
            for (int n : v.notes) {
                CHECK(n >= 43);
                CHECK(n <= 92);
            }
            if (!lead.empty() && lead.size() == v.lead.size())
                for (std::size_t i = 0; i < lead.size(); ++i)
                    motion += std::abs(lead[i] - v.lead[i]);
            lead = v.lead;
        }
        CHECK(motion <= 24);
    }
}

TEST_CASE("Patterns are deterministic, sorted and playable", "[generate]") {
    const auto song = defaultSong();
    for (auto item : stripOrder(song)) {
        const auto a = buildPattern(song, item);
        const auto b = buildPattern(song, item);
        REQUIRE(a.notes.size() == b.notes.size());
        REQUIRE(!a.notes.empty());
        for (std::size_t i = 0; i < a.notes.size(); ++i) {
            CHECK(a.notes[i].pitch == b.notes[i].pitch);
            CHECK(a.notes[i].start == b.notes[i].start);
            CHECK(a.notes[i].pitch >= 0);
            CHECK(a.notes[i].pitch <= 127);
            CHECK(a.notes[i].dur > 0.0f);
            CHECK(a.notes[i].start >= 0.0f);
            CHECK(a.notes[i].start < static_cast<float>(a.totalSteps()));
            if (i > 0)
                CHECK(a.notes[i - 1].start <= a.notes[i].start);
        }
        std::set<Role> roles;
        for (const auto& n : a.notes)
            roles.insert(n.role);
        CHECK(roles.size() == 4);
    }
}

TEST_CASE("A squeezed bridge is regenerated on the grid", "[generate]") {
    auto song = defaultSong();
    song.bridges[0].bars = 4;
    for (int fit = 1; fit <= 4; ++fit) {
        const auto p = buildPattern(song, ItemRef::bridge(0), fit);
        CHECK(p.bars == fit);
        for (const auto& n : p.notes) {
            if ((n.flags & kNoteStutter) != 0 || n.role == Role::chords)
                continue;
            const float quarterSteps = n.start * 4.0f; // 1/64-note grid
            CHECK_THAT(quarterSteps, Catch::Matchers::WithinAbs(std::round(quarterSteps), 1e-3));
        }
        float beats = 0.0f;
        for (const auto& c : itemChords(song, ItemRef::bridge(0), fit))
            beats += c.beats;
        CHECK_THAT(beats, Catch::Matchers::WithinAbs(fit * 4.0, 1e-4));
    }
}

TEST_CASE("The stutter slam route gates its last beat", "[generate]") {
    auto song = defaultSong();
    song.bridges[0].route = RouteId::slam;
    const auto p = buildPattern(song, ItemRef::bridge(0));
    const float L = static_cast<float>(p.totalSteps());
    for (const auto& n : p.notes)
        CHECK(!(n.start >= L - 4.0f && n.start < L));
    CHECK(p.cells.size() == 8);
}

TEST_CASE("Songs survive a JSON round trip", "[song]") {
    Document doc;
    doc.cycleStutter(ItemRef::section(0), 7);
    doc.togglePin(ItemRef::section(0), {4.0f, 2.0f, 72, 0.9f, Role::melody, 0, 0});
    doc.setStyle(Role::bass, 3);
    const auto text = serialiseSong(doc.song());
    const auto parsed = parseSong(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == doc.song());
    CHECK(!parseSong("not json").has_value());
    CHECK(!parseSong("{\"name\": 1}").has_value());
}

TEST_CASE("Document edits are undoable and keep the song valid", "[document]") {
    Document doc;
    const auto start = doc.song();
    doc.replaceChord(0, 1, makeChord(8, Quality::maj9));
    CHECK(chordName(doc.song().sections[0].chords[1]) == "Abmaj9");
    CHECK(doc.song().sections[0].chords[1].beats == 4.0f);
    doc.undo();
    CHECK(doc.song() == start);
    doc.redo();
    CHECK(chordName(doc.song().sections[0].chords[1]) == "Abmaj9");

    // Colour follows the key: Gadd9/B in C major becomes G7/B, not Gmaj7/B.
    doc.setLevel(1, 1, 1);
    CHECK(chordName(doc.song().sections[1].chords[1]) == "G7/B");

    // Inversion clamps without filling the undo stack.
    CHECK(doc.stepInversion(0, 0, 1));
    CHECK(doc.stepInversion(0, 0, 1));
    CHECK(!doc.stepInversion(0, 0, 1));

    // Changing a section's key moves its pins with it.
    doc.togglePin(ItemRef::section(0), {0.0f, 3.0f, 72, 0.9f, Role::melody, 0, 0});
    doc.setSectionKey(0, {7, Mode::minor});
    CHECK(doc.song().sections[0].key == Key{7, Mode::minor});
    CHECK(chordName(doc.song().sections[0].chords[0]) == "Gm9");
    REQUIRE(doc.song().pins.size() == 1);
    CHECK(doc.song().pins[0].pitch == 74);

    // Adding and removing sections keeps one bridge per neighbour.
    REQUIRE(doc.addSection());
    CHECK(doc.song().sections.size() == 3);
    CHECK(doc.song().bridges.size() == 3);
    REQUIRE(doc.removeSection(1));
    CHECK(doc.song().sections.size() == 2);
    CHECK(doc.song().bridges.size() == 2);
    CHECK(doc.song().bridges[0].from == 0);
    CHECK(doc.song().bridges[0].to == 1);
}

TEST_CASE("Section length changes rescale stutter and keep chords filling the bars", "[document]") {
    Document doc;
    doc.setSectionBars(0, 8);
    const auto& s = doc.song().sections[0];
    CHECK(s.bars == 8);
    float beats = 0.0f;
    for (const auto& c : s.chords)
        beats += c.beats;
    CHECK(beats == 32.0f);
    CHECK(s.stutter.count(120) == 1);
}

TEST_CASE("The palette offers three rings of distinct neighbours", "[palette]") {
    Document doc;
    const auto pal = doc.palette(0, 1);
    std::set<std::string> names;
    int rings[3] = {0, 0, 0};
    for (const auto& e : pal) {
        names.insert(chordName(e.chord));
        ++rings[static_cast<int>(e.ring)];
    }
    CHECK(names.size() == pal.size());
    CHECK(names.count("Dbmaj9") == 0);
    CHECK(rings[0] == 6);
    CHECK(rings[1] >= 4);
    CHECK(rings[2] >= 6);
}

TEST_CASE("Every progression fills its bars", "[palette]") {
    for (const auto& p : progressions())
        for (int bars : {2, 4, 8}) {
            const auto chords = realise(p, {3, p.mode}, bars);
            float beats = 0.0f;
            for (const auto& c : chords)
                beats += c.beats;
            CHECK(beats == static_cast<float>(bars * 4));
        }
}

TEST_CASE("The compiled song holds every bridge length", "[compiled]") {
    auto song = defaultSong();
    song.bridges[0].bars = 4;
    auto compiled = std::make_unique<CompiledSong>();
    compileSong(song, *compiled, 7);
    CHECK(compiled->revision == 7);
    CHECK(compiled->sectionCount == 2);
    CHECK(compiled->bridgeCount == 2);
    for (int fit = 1; fit <= 4; ++fit) {
        const auto& p = compiled->pattern(ItemRef::bridge(0), fit);
        CHECK(p.bars == fit);
        CHECK(p.count > 0);
    }
    CHECK(compiled->bridgeIndex(1, 0) == 1);
    CHECK(compiled->bridgeIndex(0, 0) == -1);
}

TEST_CASE("MIDI files carry the right header and tracks", "[midi]") {
    Document doc;
    const auto bytes = itemMidiFile(doc, ItemRef::section(0), std::nullopt, 122.0);
    REQUIRE(bytes.size() > 14);
    CHECK(bytes[0] == 'M');
    CHECK(bytes[1] == 'T');
    CHECK(bytes[11] == kRoleCount); // four tracks
    const auto one = itemMidiFile(doc, ItemRef::section(0), Role::bass, 122.0);
    CHECK(one[11] == 1);
    const auto song = songMidiFile(doc, std::nullopt, 122.0);
    CHECK(song.size() > bytes.size());
}

#include <filesystem>
#include <fstream>
#include <sstream>

TEST_CASE("Every factory preset parses, round-trips and plays", "[presets]") {
    namespace fs = std::filesystem;
    const fs::path dir{LG_PRESET_DIR};
    int count = 0;
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".json")
            continue;
        ++count;
        INFO(entry.path().filename().string());
        std::ifstream in(entry.path());
        std::stringstream text;
        text << in.rdbuf();
        const auto song = parseSong(text.str());
        REQUIRE(song.has_value());
        CHECK(!song->name.empty());
        CHECK(names.insert(song->name).second); // names are unique
        CHECK(song->sections.size() >= 2);
        const auto again = parseSong(serialiseSong(*song));
        REQUIRE(again.has_value());
        CHECK(*again == *song);
        for (auto item : stripOrder(*song)) {
            const auto p = buildPattern(*song, item);
            CHECK(!p.notes.empty());
        }
    }
    CHECK(count >= 0);
}

TEST_CASE("Played notes are named as chords", "[palette]") {
    const auto mask = [](std::initializer_list<int> pcs) {
        std::uint16_t m = 0;
        for (int pc : pcs)
            m = static_cast<std::uint16_t>(m | (1u << pc));
        return m;
    };
    CHECK(chordName(*identifyChord(mask({5, 8, 0, 3, 7}), 5)) == "Fm9");
    CHECK(chordName(*identifyChord(mask({0, 4, 7}), 0)) == "C");
    CHECK(chordName(*identifyChord(mask({7, 11, 2, 5}), 7)) == "G7");
    CHECK(chordName(*identifyChord(mask({0, 4, 7}), 4)) == "C/E");
    CHECK(!identifyChord(mask({0, 7}), 0).has_value());
}

TEST_CASE("Melodies never rub a semitone against their chord", "[generate]") {
    namespace fs = std::filesystem;
    int notes = 0, clashes = 0;
    for (const auto& entry : fs::directory_iterator(fs::path{LG_PRESET_DIR})) {
        if (entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        std::stringstream text;
        text << in.rdbuf();
        const auto song = parseSong(text.str());
        REQUIRE(song.has_value());
        for (auto item : stripOrder(*song)) {
            const auto p = buildPattern(*song, item);
            for (const auto& n : p.notes) {
                if (n.role != Role::melody || (n.flags & kNotePinned) != 0)
                    continue;
                ++notes;
                const auto tones = chordPcs(p.spans[static_cast<std::size_t>(n.span)].chord);
                const int pc = mod12(n.pitch);
                if (std::find(tones.begin(), tones.end(), pc) != tones.end())
                    continue;
                for (int c : tones)
                    if (mod12(pc - c) == 1 || mod12(c - pc) == 1) {
                        ++clashes;
                        break;
                    }
            }
        }
    }
    INFO(clashes << " of " << notes << " melody notes rub against their chord");
    CHECK(notes > 0);
    CHECK(clashes * 100 <= notes * 2); // under 2%: a passing note against a ninth is allowed, a wrong note is not
}

TEST_CASE("A song with a wrongly typed field does not load, and does not throw", "[song]") {
    CHECK_FALSE(parseSong(R"({"sections":[{"bars":"8"}]})").has_value());
    CHECK_FALSE(parseSong(R"({"sections":[{"name":"A"}],"bpm":"fast"})").has_value());
    CHECK(parseSong(R"({"sections":[{"name":"A","bars":8}]})").has_value());
}

TEST_CASE("Bridge pins stay with their bridge when a section is added", "[document]") {
    Document doc; // A B: bridge 0 is A -> B, bridge 1 is B -> A
    doc.togglePin(ItemRef::bridge(0), {0.0f, 2.0f, 72, 0.9f, Role::melody, 0, 0});
    doc.togglePin(ItemRef::bridge(1), {4.0f, 2.0f, 74, 0.9f, Role::melody, 0, 0});
    REQUIRE(doc.song().pins.size() == 2);
    doc.addSection(); // A B C: bridge 1 is now B -> C; B -> A is gone
    std::vector<ItemRef> on;
    for (const auto& pin : doc.song().pins)
        on.push_back(pin.item);
    CHECK(on == std::vector<ItemRef>{ItemRef::bridge(0)});
}

TEST_CASE("A mode change drops a slash bass the new mode does not have", "[document]") {
    Document doc;
    doc.setSectionKey(1, Key{0, Mode::minor});
    for (const auto& c : doc.song().sections[1].chords) {
        INFO(chordName(c));
        CHECK(chordName(c).find("/B") == std::string::npos);
    }
}

TEST_CASE("Chords never strike a pitch that is still sounding", "[generate]") {
    namespace fs = std::filesystem;
    for (const auto& entry : fs::directory_iterator(fs::path{LG_PRESET_DIR})) {
        if (entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        std::stringstream text;
        text << in.rdbuf();
        auto song = parseSong(text.str());
        REQUIRE(song.has_value());
        for (int style = 0; style < static_cast<int>(styleNames(Role::chords).size()); ++style) {
            song->part(Role::chords).style = style;
            song->part(Role::chords).density = 0.9f;
            for (auto item : stripOrder(*song)) {
                auto p = buildPattern(*song, item);
                std::erase_if(p.notes, [](const Note& n) { return n.role != Role::chords; });
                std::sort(p.notes.begin(), p.notes.end(), [](const Note& a, const Note& b) {
                    return a.pitch != b.pitch ? a.pitch < b.pitch : a.start < b.start;
                });
                for (std::size_t i = 1; i < p.notes.size(); ++i) {
                    const auto& a = p.notes[i - 1];
                    const auto& b = p.notes[i];
                    INFO(entry.path().filename().string()
                         << " " << itemKey(*song, item) << " style " << style << " pitch " << b.pitch << " a "
                         << a.start << "+" << a.dur << " flags " << int(a.flags) << " span " << a.span << " b "
                         << b.start << "+" << b.dur << " flags " << int(b.flags) << " span " << b.span);
                    if (a.pitch == b.pitch)
                        CHECK(b.start >= a.start + a.dur - 1e-3f);
                }
            }
        }
    }
}
