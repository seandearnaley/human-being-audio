// SPDX-License-Identifier: GPL-3.0-or-later
// Cross-platform native surface bridging a JUCE AudioProcessorEditor to Visage.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hba::host {

#if JUCE_MAC
struct VisageSurface : juce::NSViewComponent {
    VisageSurface();
    void useHostFileDropTarget();
    void* nativeHandle(juce::ComponentPeer* /*peer*/) { return getView(); }
};
#elif JUCE_LINUX
struct VisageSurface : juce::XEmbedComponent {
    VisageSurface() : juce::XEmbedComponent(juce::XEmbedComponentOptions{}) {}
    void* nativeHandle(juce::ComponentPeer* /*peer*/) { return reinterpret_cast<void*>(getHostWindowID()); }
};
#elif JUCE_WINDOWS
struct VisageSurface : juce::HWNDComponent {
    ~VisageSurface() override {
        if (owned_ != nullptr) {
            setHWND(nullptr);
            DestroyWindow(owned_);
            owned_ = nullptr;
        }
    }
    void* nativeHandle(juce::ComponentPeer* peer) {
        if (getHWND() == nullptr && peer != nullptr) {
            auto window = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0,
                                          0, static_cast<HWND>(peer->getNativeHandle()), nullptr, nullptr, nullptr);
            if (window == nullptr)
                return nullptr;
            owned_ = window;
            setHWND(window);
        }
        return getHWND();
    }

  private:
    HWND owned_ = nullptr;
};
#endif

} // namespace hba::host
