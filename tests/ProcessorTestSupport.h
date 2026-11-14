// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

inline void useClassicSurface (LivecutAudioProcessor& processor)
{
    processor.getParameterState ().getParameter (livecut::ids::surface)->setValueNotifyingHost (1.0f);
}

// The component with this ID, anywhere beneath `component`.
inline juce::Component* findById (juce::Component& component, const juce::String& id)
{
    if (component.getComponentID() == id)
        return &component;

    for (auto* child : component.getChildren())
        if (auto* found = findById (*child, id))
            return found;

    return nullptr;
}

// The first component of this type at or beneath `component`.
template <typename Control>
Control* findControl (juce::Component& component)
{
    if (auto* found = dynamic_cast<Control*> (&component))
        return found;
    for (auto* child : component.getChildren())
        if (auto* found = findControl<Control> (*child))
            return found;
    return nullptr;
}

// Whether `component` and everything between it and `root` is visible.
inline bool isShowing (juce::Component& component, juce::Component& root)
{
    for (auto* c = &component; c != nullptr && c != &root; c = c->getParentComponent())
        if (! c->isVisible())
            return false;

    return true;
}

// Where the hidden [.screenshot] cases write their PNGs. Without the variable
// they skip, so a tag filter that reaches them leaves nothing in the repo root.
inline juce::File screenshotDirectory()
{
    const auto* path = std::getenv ("LIVECUT_SCREENSHOT_DIR");
    if (path == nullptr || *path == '\0')
        SKIP ("Set LIVECUT_SCREENSHOT_DIR to write editor screenshots");

    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (path));
    REQUIRE (directory.createDirectory().wasOk());
    return directory;
}

inline void saveScreenshot (juce::Component& editor, const juce::File& directory, const char* name)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    juce::FileOutputStream stream (directory.getChildFile (name));
    REQUIRE (stream.openedOk());
    stream.setPosition (0);
    stream.truncate();
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
