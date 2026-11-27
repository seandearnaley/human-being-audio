// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hba::host {

inline juce::String externalDragPath(const juce::File& file) {
#if JUCE_LINUX
    // JUCE puts an existing file URI directly into its text/uri-list selection.
    return juce::URL(file).toString(false);
#else
    return file.getFullPathName();
#endif
}

inline bool dragExternalFile(const juce::File& file, juce::Component* source) {
    return juce::DragAndDropContainer::performExternalDragDropOfFiles({externalDragPath(file)}, false, source);
}

} // namespace hba::host
