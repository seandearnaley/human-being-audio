// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Presets.h"

#include <LigaturePresetsData.h>
#include <hba/host/preset_bank.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace lg {

namespace {

constexpr const char* kUserCategory = "User";

std::ptrdiff_t categoryRank(const std::string& category) {
    static constexpr std::array<std::string_view, 14> kOrder{"Pop",    "Garage",   "House",    "Deep House", "DnB",
                                                             "Liquid", "Trance",   "Neo-soul", "Afro House", "Drill",
                                                             "Lo-fi",  "City Pop", "Ambient",  "Techno"};
    return std::find(kOrder.begin(), kOrder.end(), category) - kOrder.begin();
}

std::optional<Song> parsePreset(std::string_view json) { return parseSong(json); }

} // namespace

juce::File Presets::userDirectory() { return hba::host::studioUserDirectory("Ligature", "Presets"); }

Presets::Presets() { rescan(); }

void Presets::rescan() {
    presets_ = hba::host::scanFactoryPresets<Song>(
        LigaturePresetsData::namedResourceList, LigaturePresetsData::originalFilenames,
        LigaturePresetsData::namedResourceListSize, LigaturePresetsData::getNamedResource, parsePreset, categoryRank,
        true);
    hba::host::appendUserPresets(presets_, userDirectory(), parsePreset, kUserCategory, true);
    if (presets_.empty())
        presets_.push_back(defaultSong());
}

int Presets::indexOf(const std::string& name) const {
    const auto it = std::find_if(presets_.begin(), presets_.end(), [&](const Song& s) { return s.name == name; });
    return it == presets_.end() ? -1 : static_cast<int>(it - presets_.begin());
}

int Presets::step(const std::string& currentName, int delta) const {
    return hba::host::wrappedStepIndex(indexOf(currentName), delta, static_cast<int>(presets_.size()));
}

bool Presets::save(const juce::String& name, const Song& song) {
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return false;
    Song copy = song;
    copy.name = trimmed.toStdString();
    copy.category = kUserCategory;
    const auto dir = userDirectory();
    if (dir.createDirectory().failed())
        return false;
    const auto file = dir.getChildFile(juce::File::createLegalFileName(trimmed) + ".json");
    if (!file.replaceWithText(juce::String::fromUTF8(serialiseSong(copy).c_str())))
        return false;
    rescan();
    return true;
}

} // namespace lg
