// SPDX-License-Identifier: GPL-3.0-or-later
// Shared factory & user preset bank scanning, sorting, stepping, and APVTS parameter helpers.
#pragma once

#include <hba/host/parameters.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace hba::host {

inline juce::File studioUserDirectory(const juce::String& productFolder, const juce::String& leafFolder,
                                      bool underStudioRoot = true) {
    auto root = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    if (underStudioRoot)
        root = root.getChildFile("Human Being Audio");
    return root.getChildFile(productFolder).getChildFile(leafFolder);
}

inline int wrappedStepIndex(int current, int delta, int count) noexcept {
    if (count <= 0)
        return -1;
    if (current < 0)
        return 0;
    return ((current + delta) % count + count) % count;
}

template <typename ParseFn> inline auto invokeParsePreset(ParseFn& parsePreset, std::string_view json) {
    if constexpr (requires { parsePreset(json); })
        return parsePreset(json);
    else
        return parsePreset(json, nullptr);
}

template <typename PresetT, typename GetResourceFn, typename ParseFn, typename RankFn>
inline std::vector<PresetT> scanFactoryPresets(const char* const* resourceNames, const char* const* originalFilenames,
                                               int count, GetResourceFn&& getResource, ParseFn&& parsePreset,
                                               RankFn&& categoryRank, bool sortByOriginalFilename = false) {
    struct Item {
        std::string origName;
        PresetT preset;
    };
    std::vector<Item> items;
    items.reserve(static_cast<std::size_t>(std::max(0, count)));
    for (int i = 0; i < count; ++i) {
        int size = 0;
        const char* raw = getResource(resourceNames[i], size);
        if (raw == nullptr || size <= 0)
            continue;
        if (auto parsed = invokeParsePreset(parsePreset, std::string_view(raw, static_cast<std::size_t>(size)))) {
            items.push_back(
                {originalFilenames != nullptr ? std::string(originalFilenames[i]) : std::string{}, std::move(*parsed)});
        } else {
            jassertfalse;
        }
    }

    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        const auto ra = categoryRank(a.preset.category);
        const auto rb = categoryRank(b.preset.category);
        if (sortByOriginalFilename)
            return std::tie(ra, a.origName) < std::tie(rb, b.origName);
        return std::make_tuple(ra, std::string_view(a.preset.name)) <
               std::make_tuple(rb, std::string_view(b.preset.name));
    });

    std::vector<PresetT> out;
    out.reserve(items.size());
    for (auto& item : items)
        out.push_back(std::move(item.preset));
    return out;
}

template <typename PresetT, typename ParseFn>
inline void appendUserPresets(std::vector<PresetT>& destination, const juce::File& directory, ParseFn&& parsePreset,
                              const char* userCategory = "User", bool sortByName = true) {
    if (!directory.isDirectory())
        return;
    std::vector<PresetT> user;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.json")) {
        const auto text = file.loadFileAsString().toStdString();
        if (auto parsed = invokeParsePreset(parsePreset, text)) {
            if (parsed->name.empty())
                parsed->name = file.getFileNameWithoutExtension().toStdString();
            parsed->category = userCategory;
            user.push_back(std::move(*parsed));
        }
    }
    if (sortByName)
        std::sort(user.begin(), user.end(), [](const PresetT& a, const PresetT& b) { return a.name < b.name; });
    destination.insert(destination.end(), std::make_move_iterator(user.begin()), std::make_move_iterator(user.end()));
}

} // namespace hba::host
