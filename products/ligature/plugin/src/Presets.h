// SPDX-License-Identifier: AGPL-3.0-or-later
// Factory songs (embedded JSON) and user songs in ~/Music/Human Being Audio/Ligature/Presets.
#pragma once

#include "lg/song.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace lg {

class Presets {
  public:
    Presets();

    const std::vector<Song>& all() const { return presets_; }
    int indexOf(const std::string& name) const;
    int step(const std::string& currentName, int delta) const; // -1 when there are none
    bool save(const juce::String& name, const Song& song);
    void rescan();

    static juce::File userDirectory();

  private:
    std::vector<Song> presets_;
};

} // namespace lg
