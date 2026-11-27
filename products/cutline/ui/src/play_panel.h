// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 3 PLAY: six program tabs (played from C3 to F3 while Pad MIDI is on, in drum mode), the surface that
// draws the notes the engine sent with the X/Y puck and the program's zones over it, and the performance dock.
#pragma once

#include "controls.h"
#include "pad_art.h"

#include <string>
#include <vector>

namespace cl::ui {

class PlayPanel final : public visage::Frame, public Live {
  public:
    explicit PlayPanel(Context& context);
    ~PlayPanel() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;

    void mouseMove(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent& e) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;
    bool keyPress(const visage::KeyEvent& key) override;

  private:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    };

    // One zone of a pad axis: from..to of the axis (0..1) and its name.
    struct Zone {
        float from = 0.0f;
        float to = 1.0f;
        const char* label = "";
    };
    struct Axis {
        const char* title = ""; // or, when the axis is not used, what the program does instead
        std::vector<Zone> zones;
        bool used = true;        // false: this program has no such axis
        bool continuous = false; // the exact value matters: drawn as a bar along the strip
    };

    int playingProgram() const;
    bool isDrum() const;
    bool programDoesNothing(int program) const; // Density and Fill in MIDI Slicer mode
    Axis xAxis(int program) const;
    Axis yAxis(int program) const;
    Rect tabRect(int index) const;
    Rect sheetRect() const;
    Rect artRect() const;
    Rect xStripRect() const;
    Rect yStripRect() const;
    int tabAt(float x, float y) const;
    std::string statusBadge(int prog, float x, float y, bool engaged, bool holdOn) const;
    std::string axisValue(int prog, bool isX, float v) const;
    bool showKeyHints() const;

    void drawTabs(visage::Canvas& canvas);
    void drawSurface(visage::Canvas& canvas, const Rect& art, unsigned accent, bool engaged);
    void drawLegend(visage::Canvas& canvas, const Rect& art, unsigned sheetCol);
    void drawStrip(visage::Canvas& canvas, const Rect& r, const Axis& axis, float value, bool vertical,
                   unsigned sheetCol, bool engaged);
    void drawPuck(visage::Canvas& canvas, const Rect& art, unsigned sheetCol, bool engaged);
    void drawDock(visage::Canvas& canvas, unsigned accent, bool engaged, bool holdOn);

    void updatePointer(float x, float y, bool shiftDown);
    void endPadTouch();
    void toggleHold();
    void togglePadMidi();
    bool padMidiOn() const;
    void selectProgram(int index);

    Context& context_;

    // Bottom Play Dock: Hold, Pad MIDI, XY -> CC, Audition, Drag .mid
    Button holdBtn_;
    Button padMidiBtn_;
    Chip ccChip_;
    Button auditionBtn_;
    Button dragMidBtn_;
    std::string shownExport_;

    // Live animation and touch state
    int program_ = 0;
    int programBefore_ = 0;
    float change_ = 1.0f;
    float ring_ = 1.0f;
    int hoveredTab_ = -1;
    bool draggingPad_ = false;
    bool touching_ = false;
    bool fineDragging_ = false;
    float dragOriginX_ = 0.0f;
    float dragOriginY_ = 0.0f;
    float dragStartX_ = 0.5f;
    float dragStartY_ = 0.5f;
    float engage_ = 0.0f;  // 0..1, eased
    float impulse_ = 0.0f; // the loudest lane flash, decaying

    // The surface: what was sent, on the host clock
    SentHistory history_;
    double nowPpq_ = 0.0;        // the host beat now, between telemetry updates
    double telemetryPpq_ = -1e9; // the host beat the last telemetry reported
    std::vector<int> pitchRows_; // MIDI Slicer rows, top to bottom
};

} // namespace cl::ui
