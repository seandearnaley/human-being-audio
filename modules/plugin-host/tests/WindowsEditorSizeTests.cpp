// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

TEST_CASE("The embedded Windows editor follows the native host client area", "[windows-editor-size]") {
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    juce::Component host;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
    REQUIRE(editor != nullptr);
    editor->setScaleFactor(0.75f);
    host.setBounds(-4000, -4000, editor->getWidth(), editor->getHeight());
    host.addAndMakeVisible(*editor);
    host.addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
    host.setVisible(true);

    auto* surface = dynamic_cast<juce::HWNDComponent*>(editor->findChildWithID("instrumentSurface"));
    REQUIRE(surface != nullptr);
    const auto parent = static_cast<HWND>(surface->getHWND());
    REQUIRE(parent != nullptr);
    const auto renderer = GetWindow(parent, GW_CHILD);
    REQUIRE(renderer != nullptr);
    const auto client = [](HWND window) {
        RECT bounds{};
        REQUIRE(GetClientRect(window, &bounds));
        return juce::Point<int>{static_cast<int>(bounds.right), static_cast<int>(bounds.bottom)};
    };
    CHECK(client(renderer) == client(parent));
    REQUIRE(SetWindowPos(parent, nullptr, 0, 0, 857, 503, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER));
    CHECK(client(renderer) == client(parent));
}
