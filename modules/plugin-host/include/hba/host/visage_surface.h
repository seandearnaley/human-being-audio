// SPDX-License-Identifier: GPL-3.0-or-later
// Cross-platform native surface bridging a JUCE AudioProcessorEditor to Visage.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <ole2.h>

#include <functional>
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
    std::function<void(int, int)> onNativeResize;

    void* nativeHandle(juce::ComponentPeer* peer) {
        if (getHWND() == nullptr && peer != nullptr) {
            auto window = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0,
                                          0, static_cast<HWND>(peer->getNativeHandle()), nullptr, nullptr, nullptr);
            if (window == nullptr)
                return nullptr;
            if (!SetWindowSubclass(window, parentWindowProc, 0, reinterpret_cast<DWORD_PTR>(this))) {
                DestroyWindow(window);
                return nullptr;
            }
            setHWND(window); // HWNDComponent owns and destroys the window.
        }
        return getHWND();
    }

    void useHostFileDropTarget() {
        // OLE searches ancestor HWNDs after the embedded renderer releases its target.
        if (auto* child = GetWindow(static_cast<HWND>(getHWND()), GW_CHILD))
            RevokeDragDrop(child);
    }

    juce::Rectangle<int> clientBounds() const {
        RECT bounds{};
        GetClientRect(static_cast<HWND>(getHWND()), &bounds);
        return {0, 0, static_cast<int>(bounds.right), static_cast<int>(bounds.bottom)};
    }

    ~VisageSurface() override {
        if (auto* window = static_cast<HWND>(getHWND()))
            RemoveWindowSubclass(window, parentWindowProc, 0);
    }

  private:
    static LRESULT CALLBACK parentWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                             DWORD_PTR context) {
        auto& surface = *reinterpret_cast<VisageSurface*>(context);
        if (message == WM_SIZE && surface.getHWND() == window && surface.onNativeResize)
            surface.onNativeResize(LOWORD(lParam), HIWORD(lParam));
        return DefSubclassProc(window, message, wParam, lParam);
    }
};
#endif

} // namespace hba::host
