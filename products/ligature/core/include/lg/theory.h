// SPDX-License-Identifier: AGPL-3.0-or-later
// Music theory for Ligature: chord qualities, keys, Roman-numeral analysis and voice-led voicings.
// Pure C++20. Names use ASCII accidentals ("Db", "bVI", "#iv") because the studio's UI font has no music glyphs.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

constexpr int mod12(int n) noexcept { return ((n % 12) + 12) % 12; }

enum class Mode : std::uint8_t { major, minor };

struct Key {
    int tonic = 0;
    Mode mode = Mode::major;
    bool operator==(const Key&) const = default;
};

enum class Family : std::uint8_t { maj, min, dom, sus, hdim, dim, aug };

enum class Quality : std::uint8_t {
    maj,
    maj7,
    maj9,
    maj9s11,
    add9,
    six9,
    min,
    m7,
    m9,
    m11,
    m6,
    d7,
    d9,
    d13,
    d7b9,
    sus4,
    hdim,
    dim7,
    aug,
    q4,
    count
};

struct QualityInfo {
    std::string_view id;      // stable JSON id
    std::string_view suffix;  // chord-name suffix: "m9", "7b9"
    std::string_view numeral; // Roman-numeral suffix: "9", "maj9", "7b9"
    Family family;
    std::array<std::int8_t, 6> intervals; // chord tones above the root
    std::uint8_t intervalCount;
    std::array<std::int8_t, 4> voicing; // the upper-structure tones a voicing uses (rootless for 9ths and up)
    std::uint8_t voicingCount;
};

const QualityInfo& qualityInfo(Quality q) noexcept;
std::optional<Quality> qualityFromId(std::string_view id) noexcept;

enum class VoicingStyle : std::uint8_t { close, drop2, spread };

struct Chord {
    int root = 0;
    Quality quality = Quality::maj;
    float beats = 4.0f;
    int bass = -1; // pitch class of a slash bass, or -1
    VoicingStyle style = VoicingStyle::close;
    int inversion = 0;           // -2..2, applied after voice-leading
    Family family = Family::maj; // the family the chord was written in; keeps "7" -> triad -> "7" round trips
    bool operator==(const Chord&) const = default;
};

Chord makeChord(int root, Quality q, float beats = 4.0f, int bass = -1);

std::string pcName(int pc);
std::string chordName(const Chord& c);
std::string keyName(Key k);
std::string keyShort(Key k);
float keyHue(Key k); // oklch hue from the key signature's place on the circle of fifths

std::uint16_t pcMask(const Chord& c);
std::vector<int> chordPcs(const Chord& c);
std::vector<int> sharedPcs(const Chord& a, const Chord& b);
const std::array<int, 7>& scaleIntervals(Mode m) noexcept;
bool inScale(Key k, int pc) noexcept;
Key parallel(Key k) noexcept;

// The key's own chord on `degree` (0-6) at an extension level: 0 triad, 1 seventh, 2 ninth, 3 eleventh/thirteenth.
// A degree whose ninth is flat in the scale (iii in major, v in minor) stops at the seventh.
Chord diatonic(Key k, int degree, int level = 2, float beats = 4.0f);
int levelOf(const Chord& c);
bool hasLadder(Family f) noexcept;
Quality ladder(Family f, int level) noexcept;

enum class Function : std::uint8_t { home, away, pull, colour };
std::string_view functionWord(Function f) noexcept;

struct Analysis {
    std::string numeral;
    Function function = Function::home;
    bool diatonic = false;
    int degree = -1;
};

Analysis analyze(const Chord& c, Key k);
float tension(const Chord& c, const Analysis& a);
// True when the chord fits both keys, counting chords the destination borrows from its parallel mode.
bool isPivot(const Chord& c, Key from, Key to);

struct Voicing {
    std::vector<int> notes; // MIDI notes as played (style and inversion applied)
    std::vector<int> lead;  // the voice-led shape before style and inversion; the next chord leads from this
};

// Picks the voicing nearest `previousLead` within roughly G3-G#5, or a stacked root position when voice-leading is
// off.
Voicing voiceChord(const Chord& c, const std::vector<int>* previousLead, bool voiceLead);
int bassPitch(int pc) noexcept; // C2..B2

} // namespace lg
