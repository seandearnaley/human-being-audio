// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/engine.h"
#include "sb/params.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sb::ui {

enum class BounceState { idle, rendering, ready };
enum class Archetype : std::uint8_t { snare = 0, kick = 1, clap = 2, metal = 3, tom = 4, perc = 5 };

struct PresetEntry {
    std::string name;
    std::string category;
};

class Model {
  public:
    virtual ~Model() = default;

    // Parameters
    virtual float get(ParamId id) const = 0;
    virtual void begin(ParamId id) = 0;
    virtual void set(ParamId id, float real) = 0;
    virtual void end(ParamId id) = 0;

    // Live strike audition, phase-align solver, archetype quick-starts & smart mutate
    virtual void triggerAudition(Articulation art, float velocity = 0.96f) = 0;
    virtual void runAutoAlign() = 0;
    virtual void loadArchetype(Archetype kind) = 0;
    virtual void mutateSound() = 0;

    // Presets
    virtual std::string presetName() const = 0;
    virtual std::vector<PresetEntry> presets() const = 0;
    virtual void loadPreset(int index) = 0;
    virtual void stepPreset(int delta) = 0;
    virtual void savePreset() = 0;

    // Studio History & A/B Compare
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual bool isSlotB() const = 0;
    virtual void toggleAB() = 0;
    virtual void copyAB() = 0;

    // One-click key-tagged WAV bounce & drag
    virtual BounceState bounceState() const = 0;
    virtual void startBounce() = 0;
    virtual void dragBouncedWav() = 0;

    // Live telemetry
    virtual void refreshVisuals() = 0;
    virtual const TelemetrySnapshot& visuals() const = 0;
};

} // namespace sb::ui
