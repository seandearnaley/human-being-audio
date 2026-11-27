// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/engine.h"
#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cl::ui {

// A factory pattern or fill the grid's menus can load into the edited scene.
struct LibraryPattern {
    std::string name;
    std::string category;
    Pattern pattern;
};

struct LibraryFill {
    std::string name;
    std::string category;
    Fill fill;
};

struct PresetInfo {
    std::string name;
    std::string category;
};

// The most note-ons one UI refresh takes from the engine (7,680 a second at 60 Hz).
inline constexpr int kMaxSentPerRefresh = 128;

struct Visuals {
    // Absolute bar, bar in pattern, step in bar, fill live, Repeat window, and the Density and Dynamics the drums
    // resolve with: the grid and ribbon resolve with these, so they show what plays.
    PlayPosition position{};
    // The note-ons the engine sent since the last refresh, oldest first. Their sequence numbers only grow, so a
    // reader that sees the same notes twice can tell.
    std::array<SentNote, kMaxSentPerRefresh> sent{};
    int sentCount = 0;
    SlicerPlan slicerPlan{}; // the slicer keeps its plan snapshot
    HeldNotes heldNotes{};
    int activeScene = 0;
    int pendingScene = -1;
    int activePadProgram = 0;
    int pendingPadProgram = -1;
    bool padEngaged = false;
    std::uint8_t midiPadMask = 0;
    int capturedClipCount = 0;
    double bpm = 172.0;
    double ppq = 0.0;
    std::array<float, kLaneCount> lanePhase{};
    std::array<float, kLaneCount> laneValue{};
    std::array<float, kDrumLaneCount> laneFlash{};
    std::uint64_t activeNotesLow = 0;
    std::uint64_t activeNotesHigh = 0;
    int lastBlockEvents = 0;
    std::uint8_t activityMask = 0;
    bool playing = false;
};

struct Model {
    virtual ~Model() = default;

    virtual float get(ParamId id) const = 0;
    virtual void begin(ParamId id) = 0;
    virtual void set(ParamId id, float real) = 0;
    virtual void end(ParamId id) = 0;

    virtual void refreshVisuals() {}
    virtual const Visuals& visuals() = 0;

    virtual Curve laneCurve(int lane) const = 0;
    virtual void setLaneCurve(int lane, const Curve& curve) = 0;

    // The bank and the density maps the engine plays from. The grid and ribbon resolve with these.
    virtual const GrooveState& groove() const = 0;
    // Outside a gesture, every call is its own undo step and is written to the plugin state at once. Between
    // beginGrooveEdit() and endGrooveEdit() (one press in the grid), the first call takes the one undo snapshot and
    // each call only publishes the bank to the engine; endGrooveEdit() writes the state once.
    virtual void setGrooveBank(const GrooveBank& bank) = 0;
    virtual void beginGrooveEdit() {}
    virtual void endGrooveEdit() {}

    virtual void cloneActiveSceneToNext() = 0;
    virtual void clearActiveScene() = 0;

    // The factory pattern and fill library, in menu order (grouped by category).
    virtual const std::vector<LibraryPattern>& patternLibrary() const {
        static const std::vector<LibraryPattern> none;
        return none;
    }
    virtual const std::vector<LibraryFill>& fillLibrary() const {
        static const std::vector<LibraryFill> none;
        return none;
    }

    virtual void dragMidiPhrase() = 0;
    virtual std::string exportStatus() const = 0;

    virtual std::string presetName() const = 0;
    virtual std::vector<PresetInfo> presets() const = 0;
    virtual void loadPreset(int index) = 0;
    virtual void stepPreset(int delta) = 0;
    virtual void savePreset() = 0;

    virtual bool canUndo() const { return false; }
    virtual bool canRedo() const { return false; }
    virtual void undo() {}
    virtual void redo() {}
    virtual bool isSlotB() const { return false; }
    virtual void toggleAB() {}
    virtual void copyAB() {}
};

} // namespace cl::ui
