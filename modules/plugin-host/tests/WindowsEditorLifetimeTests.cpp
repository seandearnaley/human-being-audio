// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

namespace {
struct WindowDestruction {
    HWND first = nullptr;
    int count = 0;

    static LRESULT CALLBACK observe(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                    DWORD_PTR context) {
        auto& result = *reinterpret_cast<WindowDestruction*>(context);
        if (message == WM_DESTROY) {
            if (result.count == 0)
                result.first = window;
            ++result.count;
        }
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(window, observe, id);
        return DefSubclassProc(window, message, wParam, lParam);
    }
};
} // namespace

TEST_CASE("The editor releases its renderer before the hosting window", "[windows-editor-lifetime]") {
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    WindowDestruction destroyed;
    juce::Component host;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
    REQUIRE(editor != nullptr);
    host.setBounds(-4000, -4000, editor->getWidth(), editor->getHeight());
    host.addAndMakeVisible(*editor);
    host.addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
    host.setVisible(true);

    auto* surface = dynamic_cast<juce::HWNDComponent*>(editor->findChildWithID("instrumentSurface"));
    REQUIRE(surface != nullptr);
    const auto holder = static_cast<HWND>(surface->getHWND());
    REQUIRE(holder != nullptr);
    const auto renderer = GetWindow(holder, GW_CHILD);
    REQUIRE(renderer != nullptr);
    for (const auto window : {holder, renderer})
        REQUIRE(SetWindowSubclass(window, WindowDestruction::observe, 1, reinterpret_cast<DWORD_PTR>(&destroyed)));

    editor.reset();
    CHECK(destroyed.count == 2);
    CHECK(destroyed.first == renderer);
}
