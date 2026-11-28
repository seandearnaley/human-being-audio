// SPDX-License-Identifier: AGPL-3.0-or-later
// The inspector: whatever is selected. A chord (its palette constellation, voicing, inversion, colour), a part
// (style, density, register, rate, mutate, pins, other takes), a bridge (routes and length) or a section (key, mode,
// length and the progression library).
#pragma once

#include "control.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace lg::ui {

class Inspector final : public Panel {
  public:
    explicit Inspector(Context& context);
    ~Inspector() override;

    void draw(visage::Canvas& canvas) override;

    class Slider;
    class Library;

  private:
    enum class View : std::uint8_t { chord, part, bridge, section };

    void sync() override;
    void layoutChildren() override;
    void live() override;
    void syncChord();
    void syncPart();
    void syncBridge();
    void syncSection();
    void drawChord(const Paint& p);
    void drawPart(const Paint& p);
    void drawBridge(const Paint& p);
    void drawSection(const Paint& p);
    void drawHeader(const Paint& p, const std::string& eyebrow, const std::string& title, const std::string& note,
                    unsigned swatch = 0, float reserveRight = 0.0f) const;
    void rowLabel(const Paint& p, const std::string& label, float y) const;

    View view_ = View::chord;
    int hover_ = -1; // palette chip under the mouse

    // Chord
    std::vector<PaletteEntry> palette_;
    std::vector<int> chipEntry_;                // chip -> palette entry
    std::vector<std::pair<int, int>> chipSlot_; // chip -> (ring, slot)
    ControlPool chips_;
    Segmented voicing_;
    Control inversionDown_;
    Control inversionUp_;
    Segmented level_;
    Control voiceLead_;
    Control played_;         // "Use Cm9": the chord held on the MIDI input
    std::string playedName_; // empty when nothing is held
    Chord playedChord_;

    // Part
    ControlPool styles_;
    std::unique_ptr<Slider> density_;
    Segmented register_;
    Segmented rate_;
    Segmented amount_;
    Control clearPins_;
    Control mutate_;
    ControlPool takes_;
    std::vector<Pattern> takePatterns_;
    std::vector<std::array<float, 3>> styleRects_; // x, y, width of each style chip
    float stylesBottom_ = 0.0f;

    // Bridge
    ControlPool routes_;
    std::vector<Route> routeList_;
    Segmented length_;
    std::vector<std::string> why_;

    // Section
    ControlPool keys_;
    Segmented keyMode_;
    Segmented bars_;
    std::unique_ptr<Library> library_;
};

} // namespace lg::ui
