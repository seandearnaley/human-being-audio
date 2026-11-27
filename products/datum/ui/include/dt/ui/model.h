// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <hba/ui/accessible_controls.h>

#include "dt/engine.h"
#include "dt/params.h"
#include "dt/pattern.h"

#include <hba/dsp/envelope_curve.h>

#include <cstdint>
#include <string>
#include <vector>

namespace dt::ui {

struct PresetInfo {
    std::string name;
    std::string category;
};

enum class ExportState : std::uint8_t { idle, rendering, ready, failed };

class Model : public hba::ui::PresetMenuHost {
  public:
    virtual ~Model() = default;

    virtual float get(ParamId id) const = 0;
    virtual void begin(ParamId id) = 0;
    virtual void set(ParamId id, float real) = 0;
    virtual void end(ParamId id) = 0;

    virtual void refreshVisuals() {}
    virtual const TelemetrySnapshot& visuals() = 0;
    virtual const PreviewSnapshot& preview() = 0;

    // Analytical pitch & amp envelopes
    virtual hba::dsp::EnvelopeCurve pitchCurve() const = 0;
    virtual hba::dsp::EnvelopeCurve ampCurve() const = 0;
    virtual void setPitchCurve(const hba::dsp::EnvelopeCurve& curve) = 0;
    virtual void setAmpCurve(const hba::dsp::EnvelopeCurve& curve) = 0;

    // 4-slot Kit + 16/32-step Context Sequencer
    virtual ComposePattern pattern() const = 0;
    virtual void setPattern(const ComposePattern& pattern) = 0;

    // Audition & Archetypes
    virtual void triggerAudition(int slotIndex = -1, float velocity = 0.96f) = 0;
    virtual void applyArchetype(int archetypeIndex) = 0;
    virtual void mutateKick() = 0;

    // Optional drop-in click WAV
    virtual std::string clickSampleName() const = 0;
    virtual bool hasClickSample() const = 0;
    virtual void browseClickSample() = 0;
    virtual void alignClickSample() = 0;
    virtual void clearClickSample() = 0;

    // Sub-Link live sidechain alignment solvers
    virtual void matchSubLinkPhase() = 0;
    virtual void tuneToSubLinkRoot() = 0;

    // Sample-0 16x WAV export & 960-PPQ MIDI drag
    virtual ExportState wavExportState() const { return ExportState::idle; }
    virtual void startWavExport(bool patternLoop) { (void)patternLoop; }
    virtual void dragExportedWav() {}
    virtual void dragPatternMidi() {}

    // Presets & Undo/Redo/A-B history
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

} // namespace dt::ui
