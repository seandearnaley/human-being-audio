// SPDX-License-Identifier: GPL-3.0-or-later
// Shared TopBar helpers: accessible SVG wordmark button, categorized preset menu, and A/B copy menu.
#pragma once

#include <hba/ui/theme.h>
#include <visage/ui.h>

#include <cmath>
#include <functional>
#include <string>

namespace hba::ui {

template <typename DrawFn>
inline void setupWordmarkButton(visage::Button& wordmark, std::function<void()> showAbout, DrawFn&& onDraw) {
    wordmark.onDraw() += std::forward<DrawFn>(onDraw);
    wordmark.onToggle() = [open = std::move(showAbout)](visage::Button*, bool) {
        if (open)
            open();
    };
    wordmark.setName("About");
    wordmark.setAcceptsKeystrokes(true);
    wordmark.onKeyPress() = [&wordmark](const visage::KeyEvent& key) {
        if (key.keyCode() != visage::KeyCode::Return && key.keyCode() != visage::KeyCode::Space)
            return false;
        wordmark.notify(false);
        return true;
    };
}

template <typename EmbeddedSvgT>
inline void drawBrandWordmark(visage::Canvas& canvas, const visage::Button& wordmark, const EmbeddedSvgT& svgAsset,
                              float aspect, float designHeight, float designX, float scale,
                              unsigned focusRingArgb = 0u) {
    const float h = designHeight * scale;
    const float w = h * aspect;
    visage::Svg svg(svgAsset);
    svg.setDimensions(static_cast<int>(std::ceil(w)), static_cast<int>(std::ceil(h)), canvas.dpiScale());
    canvas.svg(svg, designX * scale, (wordmark.height() - h) * 0.5f, w, h);
    if (focusRingArgb != 0u && wordmark.hasKeyboardFocus()) {
        canvas.setColor(focusRingArgb);
        canvas.roundedRectangleBorder(0.0f, 0.0f, wordmark.width(), wordmark.height(), 4.0f * scale, 1.0f * scale);
    }
}

template <typename Model> inline void showStudioPresetMenu(Model& model, visage::Frame* anchor) {
    if (model.openHostPresetMenu) {
        model.openHostPresetMenu(*anchor);
        return;
    }
    const auto presets = model.presets();
    constexpr int kSaveId = 10000;
    visage::PopupMenu menu;
    std::size_t i = 0;
    while (i < presets.size()) {
        const std::string& category = presets[i].category;
        visage::PopupMenu group(category);
        for (; i < presets.size() && presets[i].category == category; ++i)
            group.addOption(static_cast<int>(i), presets[i].name);
        menu.addSubMenu(std::move(group));
    }
    menu.addBreak();
    menu.addOption(kSaveId, "Save User Preset...");
    menu.addOption(kSaveId + 1,
                   model.isSlotB() ? "Copy B to A (Shift+Enter on A/B)" : "Copy A to B (Shift+Enter on A/B)");
    menu.onSelection() = [&model](int index) {
        if (index == kSaveId)
            model.savePreset();
        else if (index == kSaveId + 1)
            model.copyAB();
        else if (index >= 0)
            model.loadPreset(index);
    };
    menu.show(anchor);
}

template <typename CopyFn>
inline void showCopyABMenu(bool isSlotB, visage::Frame* anchor, CopyFn&& onCopy, bool useUnicodeArrow = true) {
    visage::PopupMenu menu;
    const char* label =
        useUnicodeArrow ? (isSlotB ? "Copy B \u2192 A" : "Copy A \u2192 B") : (isSlotB ? "Copy B -> A" : "Copy A -> B");
    menu.addOption(1, label);
    menu.onSelection() = [copy = std::forward<CopyFn>(onCopy)](int opt) {
        if (opt == 1)
            copy();
    };
    menu.show(anchor);
}

template <typename ButtonT>
inline void syncHistoryButtons(ButtonT& undo, ButtonT& redo, ButtonT& ab, bool canUndo, bool canRedo, bool isSlotB,
                               unsigned slotAAccent, unsigned slotBAccent, unsigned enabledAccent = colour::textDim,
                               unsigned disabledAccent = colour::textFaint) {
    undo.setEnabledState(canUndo);
    redo.setEnabledState(canRedo);
    undo.setAccent(canUndo ? enabledAccent : disabledAccent);
    redo.setAccent(canRedo ? enabledAccent : disabledAccent);
    ab.setLabel(isSlotB ? "B" : "A");
    ab.setAccent(isSlotB ? slotBAccent : slotAAccent);
}

} // namespace hba::ui
