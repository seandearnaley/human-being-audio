// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/engine.h"
#include "lg/studio_voices.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

using namespace lg;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
constexpr double kBpm = 122.0;

struct Audio {
    std::vector<float> left, right;

    std::size_t size() const { return left.size(); }
    double seconds() const { return static_cast<double>(size()) / kRate; }

    bool finite() const {
        for (std::size_t i = 0; i < size(); ++i)
            if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
                return false;
        return true;
    }
    double peak(std::size_t from = 0, std::size_t to = SIZE_MAX) const {
        double p = 0.0;
        for (std::size_t i = from; i < std::min(to, size()); ++i)
            p = std::max({p, static_cast<double>(std::abs(left[i])), static_cast<double>(std::abs(right[i]))});
        return p;
    }
    double rms(std::size_t from = 0, std::size_t to = SIZE_MAX) const {
        double sum = 0.0;
        std::size_t n = 0;
        for (std::size_t i = from; i < std::min(to, size()); ++i, ++n)
            sum += 0.5 * (static_cast<double>(left[i]) * left[i] + static_cast<double>(right[i]) * right[i]);
        return n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
    }
    std::size_t at(double second) const { return static_cast<std::size_t>(second * kRate); }
};

double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-12)); }

std::unique_ptr<StudioVoices> makeVoices() {
    auto voices = std::make_unique<StudioVoices>();
    voices->prepare(kRate, kBlock);
    return voices;
}

// Drives the voices the way the engine does: blocks with a running clock, each rendered into cleared buffers.
struct Player {
    explicit Player(StudioVoices& v) : voices(v) {}

    StudioVoices& voices;
    Audio audio;
    double beat = 0.0;

    void run(double seconds) {
        std::vector<float> l(kBlock), r(kBlock);
        const auto blocks = static_cast<int>(std::ceil(seconds * kRate / kBlock));
        for (int b = 0; b < blocks; ++b) {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            voices.render(l.data(), r.data(), kBlock, 0.8f, kBpm, beat, true);
            audio.left.insert(audio.left.end(), l.begin(), l.end());
            audio.right.insert(audio.right.end(), r.begin(), r.end());
            beat += kBlock * kBpm / 60.0 / kRate;
        }
    }
};

// Plays `song` through the engine for `bars` bars, through the studio voices when `voices` is set.
Audio renderSong(const Song& song, int bars, StudioVoices* voices, EngineParams params = {}) {
    Engine engine;
    engine.prepare(kRate, kBlock);
    compileSong(song, engine.songBuffer().write(), 1);
    engine.songBuffer().publish();
    engine.setVoiceRenderer(voices);
    params.studioVoices = voices != nullptr;
    MidiOutBuffer out;
    std::vector<float> l(kBlock), r(kBlock);
    Audio audio;
    const double bps = song.bpm / 60.0 / kRate;
    for (double ppq = 0.0; ppq < bars * 4.0; ppq += kBlock * bps) {
        engine.process(params, {song.bpm, ppq, true}, out, l.data(), r.data(), kBlock);
        audio.left.insert(audio.left.end(), l.begin(), l.end());
        audio.right.insert(audio.right.end(), r.begin(), r.end());
    }
    return audio;
}

// The strongest period (in samples) of the left channel over [from, from + 4096), searched between 50 Hz and 2 kHz.
double period(const Audio& a, std::size_t from) {
    constexpr std::size_t kWindow = 4096;
    double bestLag = 0.0, best = -1.0;
    for (int lag = static_cast<int>(kRate / 2000.0); lag <= static_cast<int>(kRate / 50.0); ++lag) {
        double num = 0.0, e0 = 0.0, e1 = 0.0;
        for (std::size_t i = from; i < from + kWindow; ++i) {
            const double x = a.left[i], y = a.left[i + static_cast<std::size_t>(lag)];
            num += x * y;
            e0 += x * x;
            e1 += y * y;
        }
        const double c = num / std::sqrt(std::max(e0 * e1, 1e-30));
        if (c > best + 1e-3) { // the first (shortest) of near-equal peaks: the fundamental, not a multiple
            best = c;
            bestLag = lag;
        }
    }
    return bestLag;
}

} // namespace

TEST_CASE("studio voices: every embedded preset loads", "[voices]") {
    auto voices = makeVoices();
    CHECK(voices->presetsLoaded());
    CHECK(voices->latencySamples() > 0);
    CHECK(voices->latencySamples() < static_cast<int>(kRate * 0.01));
}

TEST_CASE("studio voices: each part sounds, finite and within full scale", "[voices]") {
    struct Part {
        int role;
        std::vector<int> notes;
    };
    // Six chord notes: one more than Torus Glide's voices, so the voicing has to drop one.
    const std::vector<Part> parts{{0, {56, 60, 63, 67, 70, 74}}, {1, {41}}, {2, {72}}, {3, {72, 75, 79, 84}}};
    for (const auto& part : parts) {
        INFO("role " << part.role);
        auto voices = makeVoices();
        Player player(*voices);
        if (part.role == 3) { // the arp plucks 16ths
            for (int step = 0; step < 8; ++step) {
                const int note = part.notes[static_cast<std::size_t>(step) % part.notes.size()];
                voices->noteOn(3, note, 0.8f);
                player.run(60.0 / kBpm / 4.0);
                voices->noteOff(3, note);
            }
        } else {
            for (const int note : part.notes)
                voices->noteOn(part.role, note, 0.8f);
            player.run(0.6);
            for (const int note : part.notes)
                voices->noteOff(part.role, note);
        }
        player.run(0.4);
        const auto& a = player.audio;
        CHECK(a.finite());
        CHECK(a.peak() <= 1.0);
        CHECK(a.rms(0, a.at(0.6)) > 1e-3);
    }
}

TEST_CASE("studio voices: each drum sounds, finite and within full scale", "[voices]") {
    for (int kind = 0; kind < 3; ++kind) {
        INFO("drum " << kind);
        auto voices = makeVoices();
        Player player(*voices);
        voices->drum(kind, 1.0f);
        player.run(0.3);
        CHECK(player.audio.finite());
        CHECK(player.audio.peak() <= 1.0);
        CHECK(player.audio.peak() > 0.01);
    }
}

TEST_CASE("studio voices: releaseAll lets every part die away", "[voices]") {
    auto voices = makeVoices();
    Player player(*voices);
    for (const int note : {56, 60, 63, 67, 70})
        voices->noteOn(0, note, 0.9f);
    voices->noteOn(1, 41, 0.9f);
    voices->noteOn(2, 77, 0.9f);
    voices->noteOn(3, 84, 0.9f);
    for (int kind = 0; kind < 3; ++kind)
        voices->drum(kind, 1.0f);
    player.run(1.0);
    REQUIRE(player.audio.rms() > 1e-2);
    voices->releaseAll();
    player.run(6.0);
    const auto& a = player.audio;
    CHECK(a.finite());
    CHECK(a.peak(a.size() - a.at(0.5)) < 1e-3); // below -60 dBFS
    CHECK(a.peak(a.size() - a.at(0.5)) < a.peak(a.at(1.0), a.at(1.5)) * 1e-2);
}

TEST_CASE("studio voices: a chord change keeps the chord sounding", "[voices]") {
    // The engine sends a change's note-offs and note-ons at the same sample, with no render between them. Torus
    // Glide's five voices share one envelope, so the new chord must take over without the level dipping.
    auto voices = makeVoices();
    Player player(*voices);
    for (const int note : {57, 60, 64, 67})
        voices->noteOn(0, note, 0.8f);
    player.run(0.5);
    for (const int note : {57, 60, 64, 67})
        voices->noteOff(0, note);
    for (const int note : {55, 59, 62, 65, 69})
        voices->noteOn(0, note, 0.8f);
    player.run(0.2);
    const auto& a = player.audio;
    const auto change = a.at(0.5);
    CHECK(a.rms(change, change + a.at(0.03)) > 0.7 * a.rms(change - a.at(0.1), change));
}

TEST_CASE("studio voices: a release tail keeps its pitch", "[voices]") {
    // Torus Glide's idle root sits at C2 + Root Key; the voices keep that equal to the root they hold, so a lead's
    // tail never slides down to it.
    auto voices = makeVoices();
    Player player(*voices);
    voices->noteOn(2, 69, 0.9f); // A4, 440 Hz
    player.run(0.5);
    voices->noteOff(2, 69);
    player.run(0.4);
    const auto& a = player.audio;
    const double held = period(a, a.at(0.3));
    const double tail = period(a, a.at(0.6));
    CHECK(held > 100.0);
    CHECK(held < 118.0);
    CHECK(std::abs(tail - held) <= 0.03 * held);
}

TEST_CASE("studio voices: the default song plays through the engine", "[voices]") {
    auto voices = makeVoices();
    const auto a = renderSong(defaultSong(), 8, voices.get());
    CHECK(a.finite());
    CHECK(a.peak() <= 1.0);
    CHECK(a.rms() > 0.01);
    for (int bar = 0; bar < 8; ++bar) { // every bar sounds
        const double perBar = 4.0 * 60.0 / kBpm;
        CHECK(a.rms(a.at(bar * perBar), a.at((bar + 1) * perBar)) > 0.005);
    }
}

TEST_CASE("studio voices: each part sits near the sketch synth's level", "[voices]") {
    // The same notes through both sources, one part at a time and then the kit alone.
    for (int role = 0; role <= kRoleCount; ++role) {
        Song song = defaultSong();
        EngineParams params;
        params.beat = role == kRoleCount;
        for (int r = 0; r < kRoleCount; ++r)
            song.part(static_cast<Role>(r)).mute = r != role;
        auto voices = makeVoices();
        const auto sketch = renderSong(song, 8, nullptr, params);
        const auto studio = renderSong(song, 8, voices.get(), params);
        const double diff = db(studio.rms() / sketch.rms());
        INFO((role < kRoleCount ? roleName(static_cast<Role>(role)) : "drums")
             << ": sketch " << db(sketch.rms()) << " dB, studio " << db(studio.rms()) << " dB");
        REQUIRE(sketch.rms() > 1e-3);
        CHECK(std::abs(diff) < 3.0);
    }
}

TEST_CASE("studio voices: eight bars render much faster than real time", "[voices][perf]") {
    auto voices = makeVoices();
    const auto started = std::chrono::steady_clock::now();
    const auto a = renderSong(defaultSong(), 8, voices.get());
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double factor = a.seconds() / std::max(took, 1e-9);
    INFO("8 bars (" << a.seconds() << " s) in " << took << " s: " << factor << "x real time");
    // About 10x on an Apple M4 Max; the bound leaves room for slower CI machines while catching a real regression.
#ifdef NDEBUG
    CHECK(factor > 3.0);
#else
    CHECK(factor > 0.5);
#endif
}
