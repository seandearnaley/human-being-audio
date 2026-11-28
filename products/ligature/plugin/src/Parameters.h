// SPDX-License-Identifier: AGPL-3.0-or-later
// Host parameters: the performance settings and the automatable Scene launcher. The song itself lives in the state.
#pragma once

#include <hba/host/parameters.h>

#include <array>
#include <span>
#include <string_view>

namespace lg {

enum class ParamKind { toggle, choice, real };

struct ParamSpec {
    ParamKind kind = ParamKind::real;
    std::string_view key;
    std::string_view name;
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    std::span<const std::string_view> choices;
    bool automatable = true;
};

enum class ParamId : int { land, audition, level, beat, role, scene, keyChords, voices, count };
inline constexpr int kParamCount = static_cast<int>(ParamId::count);

inline constexpr std::array<std::string_view, 3> kLandChoices{"Next bar", "2 bars", "Phrase"};
inline constexpr std::array<std::string_view, 5> kRoleChoices{"Lead", "Follow chords", "Follow bass", "Follow melody",
                                                              "Follow arp"};
// Automating Scene to a section launches it on the Land grid, through its bridge.
inline constexpr std::array<std::string_view, 5> kSceneChoices{"-", "A", "B", "C", "D"};
// The built-in sound: the quick sketch synth, or the studio's own instruments (Torus Glide, Lineage, Strike Body,
// Datum).
inline constexpr std::array<std::string_view, 2> kVoiceChoices{"Sketch", "Studio"};

inline const std::array<ParamSpec, kParamCount> kParams{{
    {ParamKind::choice, "land", "Land", 0, 2, 2, kLandChoices, true},
    {ParamKind::toggle, "audition", "Sound", 0, 1, 1, {}, true},
    {ParamKind::real, "level", "Sound level", 0, 1, 0.8f, {}, true},
    {ParamKind::toggle, "beat", "Beat", 0, 1, 1, {}, true},
    {ParamKind::choice, "role", "Instance", 0, 4, 0, kRoleChoices, false},
    {ParamKind::choice, "scene", "Scene", 0, 4, 0, kSceneChoices, true},
    {ParamKind::toggle, "keyChords", "One-key chords", 0, 1, 1, {}, true},
    {ParamKind::choice, "voices", "Sound source", 0, 1, 1, kVoiceChoices, true},
}};

struct ParamValues {
    std::array<float, kParamCount> values{};
    float operator[](ParamId id) const noexcept { return values[static_cast<std::size_t>(id)]; }
    int choice(ParamId id) const noexcept { return static_cast<int>(values[static_cast<std::size_t>(id)] + 0.5f); }
    bool on(ParamId id) const noexcept { return values[static_cast<std::size_t>(id)] >= 0.5f; }
};

inline juce::String toJuce(std::string_view utf8) { return hba::host::toJuce(utf8); }
juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

class ParameterValues {
  public:
    explicit ParameterValues(juce::AudioProcessorValueTreeState& state) : snap_(state, kParams) {}
    ParamValues snapshot() const noexcept { return snap_.snapshot(); }

  private:
    hba::host::ParameterSnapshotter<ParamValues, decltype(kParams)> snap_;
};

inline int landBars(int choice) noexcept { return choice <= 0 ? 1 : choice == 1 ? 2 : 4; }
inline int landChoice(int bars) noexcept { return bars <= 1 ? 0 : bars == 2 ? 1 : 2; }

} // namespace lg
