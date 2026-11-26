// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/theory.h"
#include "panels.h"

#include <array>
#include <string>

namespace cl::ui {

unsigned roleColour(Role role);
const char* roleName(Role role);
std::string spanDisplayLabel(Role role, const SpanOverride& sp);
bool isEditableNote(const CompiledNote& n) noexcept;
NoteOverride* ensureNoteOverride(PhraseOverrides& ov, NoteKey key, int slotIndex) noexcept;
void pruneNoteOverride(PhraseOverrides& ov, NoteKey key, int slotIndex, const NoteOverride* no) noexcept;

class PhraseStage final : public visage::Frame, public Live {
  public:
    explicit PhraseStage(Context& context);
    ~PhraseStage() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void mouseMove(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;

  private:
    enum class DragMode { none, fillDivider, turnDivider, chordBar, notePitch, noteLength, noteNudge, velBrush };

    struct Layout {
        float padL, padR, plotW;
        double totalBeats;
        float rulerY, tapeY, tapeH;
        float chordY, chordH;
        float rollY, rollH;
        float velY, velH;
        float footerY;
        float fillDivX, turnDivX;
    };

    struct NoteBox {
        float x, y, w, h;
    };

    struct VelBrushItem {
        NoteKey key{};
        int slotIndex = -1;
        int planIndex = -1;
        float stemX = 0.0f;
        int baseVel = 100;
    };

    Layout computeLayout(const PhrasePlan& plan, OutputMode mode, float w, float h) const;
    static void computePitchRange(const PhrasePlan& plan, OutputMode mode, int& minNote, int& maxNote);
    NoteBox noteBoxOf(const CompiledNote& n, const Layout& lay, OutputMode mode, int minNote, int maxNote) const;
    void captureVelBrushSnapshot(const PhrasePlan& plan, const Layout& lay);
    void applyVelBrush(float xA, float xB, float my, const Layout& lay);
    void updateHover(float mx, float my);
    void showChordMenu(int barIdx);
    void showSpanMenu(int spanIdx);
    void showNoteMenu(NoteKey key, int slotIndex, bool currentlyMuted);

    Context& context_;
    std::array<float, 5> activityHold_{};
    int hoveredSpan_ = -1;
    int hoveredChordBar_ = -1;
    int hoveredDivider_ = 0;
    int hoveredNote_ = -1;
    bool hoveredNoteEdge_ = false;
    bool hoveredVelLane_ = false;
    int hoveredVelNote_ = -1;
    bool hoveredReset_ = false;
    float resetBtnX_ = -1.0f;
    float resetBtnY_ = 0.0f;
    float resetBtnW_ = 0.0f;
    float resetBtnH_ = 0.0f;

    DragMode dragMode_ = DragMode::none;
    int dragNoteIdx_ = -1;
    NoteKey dragNoteKey_{};
    int dragSlotIndex_ = -1;
    int dragChordBar_ = -1;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;
    int dragBasePitchDelta_ = 0;
    int dragBaseLength_ = 0;
    int dragBaseNudge_ = 0;
    int dragBaseChordDeg_ = 0;
    int dragOriginalMidi_ = 41;

    std::array<VelBrushItem, kMaxPhraseNotes> velBrushItems_{};
    int velBrushCount_ = 0;
    float velBrushLastX_ = 0.0f;
};

} // namespace cl::ui
