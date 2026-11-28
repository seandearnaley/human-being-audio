// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/document.h"
#include "lg/engine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

using namespace lg;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Rig {
    Engine engine;
    Song song = defaultSong();
    EngineParams params;
    MidiOutBuffer out;
    std::vector<float> left = std::vector<float>(kBlock), right = std::vector<float>(kBlock);
    double ppq = 0.0;
    double bpm = 120.0;
    // Every note event seen, with the absolute beat it happened at.
    struct Ev {
        double beat;
        int status, channel, pitch, velocity;
    };
    std::vector<Ev> events;
    std::vector<std::pair<double, ItemRef>> items; // the item playing at each block start

    Rig() {
        engine.prepare(kRate, kBlock);
        publish();
    }

    void publish() {
        compileSong(song, engine.songBuffer().write(), 1);
        engine.songBuffer().publish();
    }

    void block(bool playing = true) {
        Transport t{bpm, ppq, playing};
        engine.process(params, t, out, left.data(), right.data(), kBlock);
        const double bps = bpm / 60.0 / kRate;
        for (int i = 0; i < out.count; ++i) {
            const auto& e = out.events[static_cast<std::size_t>(i)];
            events.push_back({ppq + e.sampleOffset * bps, e.data[0] & 0xF0, e.data[0] & 0x0F, e.data[1], e.data[2]});
        }
        items.push_back({ppq, engine.telemetry().performance.item});
        if (playing)
            ppq += kBlock * bps;
    }

    void runBeats(double beats) {
        const double end = ppq + beats;
        while (ppq < end - 1e-9)
            block();
    }

    ItemRef itemAt(double beat) const {
        ItemRef r = items.front().second;
        for (const auto& [b, item] : items)
            if (b <= beat + 1e-9)
                r = item;
        return r;
    }

    // Every note-on has a matching note-off, in order, per channel and pitch.
    bool balanced() const {
        std::map<std::pair<int, int>, int> open;
        for (const auto& e : events) {
            auto& n = open[{e.channel, e.pitch}];
            if (e.status == 0x90 && e.velocity > 0) {
                if (n != 0)
                    return false;
                n = 1;
            } else if (e.status == 0x80) {
                n = 0;
            }
        }
        return true;
    }
};

} // namespace

TEST_CASE("The engine follows the host and plays all four parts on channels 1-4", "[engine]") {
    Rig rig;
    rig.runBeats(16.0);
    std::set<int> channels;
    for (const auto& e : rig.events)
        if (e.status == 0x90)
            channels.insert(e.channel);
    CHECK(channels == std::set<int>{0, 1, 2, 3});
    CHECK(rig.balanced());
    CHECK(rig.engine.telemetry().hostPlaying);
}

TEST_CASE("The same host position always plays the same notes", "[engine]") {
    Rig a, b;
    a.runBeats(8.0);
    b.ppq = 8.0; // start the host at bar 3 of the 4-bar verse
    b.runBeats(4.0);
    std::vector<int> fromA, fromB;
    for (const auto& e : a.events)
        if (e.status == 0x90 && e.beat >= 8.0 - 1e-6)
            fromA.push_back(e.pitch);
    for (const auto& e : b.events)
        if (e.status == 0x90 && e.beat < 12.0 - 1e-6)
            fromB.push_back(e.pitch);
    a.runBeats(4.0);
    fromA.clear();
    for (const auto& e : a.events)
        if (e.status == 0x90 && e.beat >= 8.0 - 1e-6 && e.beat < 12.0 - 1e-6)
            fromA.push_back(e.pitch);
    CHECK(!fromB.empty());
    CHECK(fromA == fromB);
}

TEST_CASE("Launching early plays the whole bridge and lands on the phrase", "[engine]") {
    Rig rig;
    rig.runBeats(2.0); // inside bar 1
    rig.engine.command({CommandKind::launch, 1});
    rig.runBeats(22.0);
    CHECK(rig.itemAt(3.9) == ItemRef::section(0));
    CHECK(rig.itemAt(8.5) == ItemRef::bridge(0));   // bars 3-4: the 2-bar bridge
    CHECK(rig.itemAt(16.5) == ItemRef::section(1)); // bar 5: the chorus lands on the phrase
    CHECK(rig.balanced());
}

TEST_CASE("Launching late squeezes the bridge into the bar that is left", "[engine]") {
    Rig rig;
    rig.runBeats(9.0); // inside bar 3
    rig.engine.command({CommandKind::launch, 1});
    rig.block();
    const auto t = rig.engine.telemetry();
    CHECK(t.barsToLand == 2);
    rig.runBeats(12.0);
    CHECK(rig.itemAt(12.5) == ItemRef::bridge(0)); // bar 4 only
    CHECK(rig.itemAt(16.5) == ItemRef::section(1));
}

TEST_CASE("Holding a bridge loops its first bar until release, then lands", "[engine]") {
    Rig rig;
    rig.runBeats(2.0);
    rig.engine.command({CommandKind::hold, 0});
    rig.runBeats(14.0); // bridge starts at bar 2 (beat 4) and holds through beat 16
    CHECK(rig.itemAt(5.0) == ItemRef::bridge(0));
    CHECK(rig.itemAt(13.0) == ItemRef::bridge(0));
    CHECK(rig.engine.telemetry().performance.hold);
    rig.engine.command({CommandKind::release, 0});
    rig.runBeats(12.0);
    CHECK(rig.engine.telemetry().performance.item == ItemRef::section(1));
    CHECK(rig.balanced());
}

TEST_CASE("A host jump releases every note and lands a bridge in flight", "[engine]") {
    Rig rig;
    rig.runBeats(2.0);
    rig.engine.command({CommandKind::launch, 1});
    rig.runBeats(8.0); // now inside the bridge
    CHECK(rig.engine.telemetry().performance.item.isBridge());
    rig.ppq = 0.0; // loop brace back to the start
    rig.runBeats(1.0);
    CHECK(rig.engine.telemetry().performance.item == ItemRef::section(1));
    CHECK(!rig.engine.telemetry().performance.planActive);
    CHECK(rig.balanced());
}

TEST_CASE("Stopping the host ends every note", "[engine]") {
    Rig rig;
    rig.runBeats(3.0);
    rig.block(false);
    std::map<std::pair<int, int>, int> open;
    for (const auto& e : rig.events)
        open[{e.channel, e.pitch}] = e.status == 0x90 ? 1 : 0;
    for (const auto& [k, v] : open)
        CHECK(v == 0);
}

TEST_CASE("A follower outputs one part on channel 1, in step with its leader", "[engine]") {
    Rig leader, follower;
    follower.params.outputRole = static_cast<int>(Role::bass);
    leader.runBeats(2.0);
    follower.runBeats(2.0);
    leader.engine.command({CommandKind::launch, 1});
    for (int i = 0; i < 400; ++i) {
        leader.block();
        follower.engine.follow(leader.engine.performance());
        follower.block();
    }
    std::vector<int> bassLeader, bassFollower;
    for (const auto& e : leader.events)
        if (e.status == 0x90 && e.channel == 1)
            bassLeader.push_back(e.pitch);
    for (const auto& e : follower.events)
        if (e.status == 0x90) {
            CHECK(e.channel == 0);
            bassFollower.push_back(e.pitch);
        }
    CHECK(!bassFollower.empty());
    CHECK(bassLeader == bassFollower);
}

TEST_CASE("A direct launch skips the bridge", "[engine]") {
    Rig rig;
    rig.runBeats(2.0);
    rig.engine.command({CommandKind::launchDirect, 1});
    rig.runBeats(18.0);
    for (const auto& [beat, item] : rig.items)
        CHECK(!item.isBridge());
    CHECK(rig.itemAt(16.5) == ItemRef::section(1));
}

TEST_CASE("Preview plays from the chosen item while the host is stopped", "[engine]") {
    Rig rig;
    rig.engine.command({CommandKind::previewPlay, 1});
    for (int i = 0; i < 50; ++i)
        rig.block(false);
    const auto t = rig.engine.telemetry();
    CHECK(t.previewPlaying);
    CHECK(t.performance.item == ItemRef::section(1));
    int ons = 0;
    for (const auto& e : rig.events)
        ons += e.status == 0x90 ? 1 : 0;
    CHECK(ons > 0);
    rig.engine.command({CommandKind::previewStop, 0});
    rig.block(false);
    CHECK(!rig.engine.telemetry().playing);
    CHECK(rig.balanced());
}

TEST_CASE("A new song is picked up without losing the timeline", "[engine]") {
    Rig rig;
    rig.runBeats(5.0);
    rig.song.part(Role::melody).mute = true;
    rig.publish();
    const auto before = rig.events.size();
    rig.runBeats(8.0);
    for (std::size_t i = before; i < rig.events.size(); ++i)
        if (rig.events[i].status == 0x90)
            CHECK(rig.events[i].channel != 2);
    CHECK(rig.engine.telemetry().performance.item == ItemRef::section(0));
}

TEST_CASE("The sketch synth makes sound and stays bounded", "[engine]") {
    Rig rig;
    float peak = 0.0f;
    for (int i = 0; i < 200; ++i) {
        rig.block();
        for (float s : rig.left)
            peak = std::max(peak, std::abs(s));
    }
    CHECK(peak > 0.01f);
    CHECK(peak <= 1.0f);
}

TEST_CASE("White keys play the section key's own chords; release ends them", "[engine]") {
    Rig rig;
    rig.block(false);
    rig.engine.inputNote(60, 100, true, 10); // C3: degree 1 of F minor -> Fm9 with F in the bass
    rig.block(false);
    std::set<int> pitches;
    int bassCount = 0;
    for (const auto& e : rig.events)
        if (e.status == 0x90) {
            pitches.insert(e.pitch % 12);
            bassCount += e.channel == 1 ? 1 : 0;
        }
    CHECK(bassCount == 1);
    CHECK(pitches.count(5) == 1); // F
    CHECK(pitches.count(8) == 1); // Ab
    rig.engine.inputNote(60, 0, false, 0);
    rig.block(false);
    CHECK(rig.balanced());
    std::map<std::pair<int, int>, int> open;
    for (const auto& e : rig.events)
        open[{e.channel, e.pitch}] = e.status == 0x90 ? 1 : 0;
    for (const auto& [k, v] : open)
        CHECK(v == 0);
}

TEST_CASE("Auditioning a chord while stopped sounds it, then lets go", "[engine]") {
    Rig rig;
    rig.engine.command({CommandKind::audition, 0, 1}); // Dbmaj9 in the verse
    rig.block(false);
    int ons = 0;
    for (const auto& e : rig.events)
        ons += e.status == 0x90 ? 1 : 0;
    CHECK(ons >= 4);
    for (int i = 0; i < 120; ++i)
        rig.block(false);
    CHECK(rig.balanced());
    int offs = 0;
    for (const auto& e : rig.events)
        offs += e.status == 0x80 ? 1 : 0;
    CHECK(offs == ons);
}

TEST_CASE("Pressing Play during an audition ends the auditioned chord at once", "[engine]") {
    Rig rig;
    rig.engine.command({CommandKind::audition, 1, 2});
    rig.block(false);
    std::set<std::pair<int, int>> auditioned;
    for (const auto& e : rig.events)
        if (e.status == 0x90)
            auditioned.insert({e.channel, e.pitch});
    REQUIRE(!auditioned.empty());
    const auto before = rig.events.size();
    rig.runBeats(4.0);
    for (const auto& [channel, pitch] : auditioned) {
        INFO("channel " << channel << " pitch " << pitch);
        const auto off = std::find_if(
            rig.events.begin() + static_cast<std::ptrdiff_t>(before), rig.events.end(),
            [&](const Rig::Ev& e) { return e.status == 0x80 && e.channel == channel && e.pitch == pitch; });
        REQUIRE(off != rig.events.end());
        CHECK(off->beat < 1e-6); // in the first block that plays
    }
}

TEST_CASE("Removing an earlier section keeps playback on the section that plays", "[engine]") {
    Rig rig;
    Document doc(rig.song);
    doc.addSection(); // A B C
    rig.song = doc.song();
    rig.publish();
    rig.engine.command({CommandKind::launchDirect, 1, 0});
    rig.runBeats(20.0);
    REQUIRE(rig.engine.telemetry().performance.item == ItemRef::section(1));
    const double anchor = rig.engine.telemetry().performance.anchor;
    doc.removeSection(0); // B is now section 0
    rig.song = doc.song();
    rig.publish();
    rig.runBeats(2.0);
    CHECK(rig.engine.telemetry().performance.item == ItemRef::section(0));
    CHECK(rig.engine.telemetry().performance.anchor == anchor); // in time: no restart, no drift
    CHECK(rig.balanced());
}

namespace {
// Notes as (channel, pitch, on beat, off beat), rendered at a given block size.
std::vector<std::array<double, 4>> renderNotes(int blockSize, double beats) {
    Engine engine;
    engine.prepare(kRate, blockSize);
    compileSong(defaultSong(), engine.songBuffer().write(), 1);
    engine.songBuffer().publish();
    MidiOutBuffer out;
    std::vector<float> l(static_cast<std::size_t>(blockSize)), r(static_cast<std::size_t>(blockSize));
    const double bps = 120.0 / 60.0 / kRate;
    std::map<std::pair<int, int>, double> open;
    std::vector<std::array<double, 4>> notes;
    for (double ppq = 0.0; ppq < beats; ppq += blockSize * bps) {
        engine.process({}, {120.0, ppq, true}, out, l.data(), r.data(), blockSize);
        for (int i = 0; i < out.count; ++i) {
            const auto& e = out.events[static_cast<std::size_t>(i)];
            const std::pair<int, int> key{e.data[0] & 0x0F, e.data[1]};
            const double beat = ppq + e.sampleOffset * bps;
            if ((e.data[0] & 0xF0) == 0x80 && open.contains(key)) {
                notes.push_back({double(key.first), double(key.second), open[key], beat});
                open.erase(key);
            } else if ((e.data[0] & 0xF0) == 0x90) {
                open[key] = beat;
            }
        }
    }
    std::sort(notes.begin(), notes.end());
    return notes;
}
} // namespace

TEST_CASE("Repeated notes keep their gaps at any buffer size", "[engine]") {
    const double beats = 24.0;
    const auto small = renderNotes(64, beats);
    const auto large = renderNotes(4096, beats);
    const double sample = 120.0 / 60.0 / kRate;
    // Only notes that start and end well inside both renders.
    const auto inside = [&](const std::array<double, 4>& n) { return n[3] < beats - 4.0; };
    std::vector<std::array<double, 4>> a, b;
    std::copy_if(small.begin(), small.end(), std::back_inserter(a), inside);
    std::copy_if(large.begin(), large.end(), std::back_inserter(b), inside);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        INFO("channel " << a[i][0] << " pitch " << a[i][1] << " on " << a[i][2]);
        CHECK(std::abs(a[i][2] - b[i][2]) <= 2.0 * sample);
        CHECK(std::abs(a[i][3] - b[i][3]) <= 2.0 * sample);
    }
}

TEST_CASE("A legato one-key chord keeps the notes it shares with the chord before", "[engine]") {
    Rig rig;
    rig.block(false);
    rig.engine.inputNote(60, 100, true, 0); // Fm9
    rig.block(false);
    rig.engine.inputNote(62, 100, true, 0); // the next chord, pressed before the first lets go
    const auto pressed = rig.events.size();
    rig.block(false);
    std::set<std::pair<int, int>> second;
    for (auto i = pressed; i < rig.events.size(); ++i)
        if (rig.events[i].status == 0x90)
            second.insert({rig.events[i].channel, rig.events[i].pitch});
    rig.engine.inputNote(60, 0, false, 0);
    rig.block(false);
    std::map<std::pair<int, int>, int> open;
    for (const auto& e : rig.events)
        open[{e.channel, e.pitch}] = e.status == 0x90 ? 1 : 0;
    std::set<std::pair<int, int>> sounding;
    for (const auto& [k, v] : open)
        if (v != 0)
            sounding.insert(k);
    CHECK(sounding == second);
}

TEST_CASE("Preparing again mid-play ends the notes the instrument still holds", "[engine]") {
    Rig rig;
    rig.runBeats(3.0);
    rig.engine.prepare(kRate, kBlock); // a host changing its buffer size while playing
    rig.runBeats(1.0);
    rig.block(false); // stop: everything still on ends
    std::map<std::pair<int, int>, int> after;
    for (const auto& e : rig.events)
        after[{e.channel, e.pitch}] += e.status == 0x90 ? 1 : (e.status == 0x80 ? -1 : 0);
    for (const auto& [note, n] : after)
        CHECK(n == 0);
}

TEST_CASE("A direct launch in the last bar lands on the upcoming phrase line", "[engine]") {
    Rig rig; // Land: Phrase (4 bars)
    while (rig.ppq < 13.0)
        rig.block();
    rig.engine.command({CommandKind::launchDirect, 1, 0});
    rig.runBeats(4.0);
    CHECK(rig.itemAt(16.5) == ItemRef::section(1));
}

TEST_CASE("Followers preview with the leader while the host is stopped", "[engine]") {
    Rig leader, follower;
    follower.params.outputRole = static_cast<int>(Role::bass);
    leader.engine.command({CommandKind::previewPlay, 0, 0});
    for (int i = 0; i < 200; ++i) {
        leader.block(false);
        follower.engine.follow(leader.engine.performance());
        follower.block(false);
    }
    CHECK(follower.engine.telemetry().previewPlaying);
    int ons = 0;
    for (const auto& e : follower.events)
        ons += e.status == 0x90 && e.channel == 0 ? 1 : 0;
    CHECK(ons > 0);
    leader.engine.command({CommandKind::previewStop, 0, 0});
    leader.block(false);
    follower.engine.follow(leader.engine.performance());
    follower.block(false);
    CHECK_FALSE(follower.engine.telemetry().previewPlaying);
    CHECK(follower.balanced());
}
