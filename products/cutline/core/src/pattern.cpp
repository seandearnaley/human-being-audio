// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/pattern.h"

#include <hba/core/note_name.h>

#include <algorithm>

namespace cl {

std::int8_t effectiveLaneMidiNote(NoteMap map, int lane, int overrideNote) noexcept CL_NONBLOCKING {
    if (overrideNote >= 1 && overrideNote <= 127)
        return static_cast<std::int8_t>(overrideNote);
    const auto mIdx = static_cast<std::size_t>(std::clamp(static_cast<int>(map), 0, kNoteMapCount - 1));
    const auto lIdx = static_cast<std::size_t>(std::clamp(lane, 0, kDrumLaneCount - 1));
    return kKitNoteMaps[mIdx][lIdx];
}

std::string midiNoteLabel(int midiNote) { return hba::core::midiNoteName(std::clamp(midiNote, 0, 127)); }

} // namespace cl
