// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <hba/ui/accessible_controls.h>

#include "tg/chord_matrix.h"
#include "tg/engine.h"
#include "tg/params.h"

#include <string>
#include <vector>

namespace tg::ui {

enum class ResampleState { idle, rendering, ready, failed };

struct PresetEntry {
    std::string name;
    std::string category;
};

class Model : public hba::ui::PresetMenuHost {
  public:
    virtual ~Model() = default;

    // Parameters
    virtual float get(ParamId id) const = 0;
    virtual void begin(ParamId id) = 0;
    virtual void set(ParamId id, float real) = 0;
    virtual void end(ParamId id) = 0;

    // Modulation curves
    virtual hba::dsp::Curve curve(int lane) const = 0;
    virtual void setCurve(int lane, const hba::dsp::Curve& next) = 0;

    // Programmable 4x5 Copedent Scene Matrix
    virtual SceneMatrix sceneMatrix() const = 0;
    virtual void setSceneMatrix(const SceneMatrix& next) = 0;
    virtual void optimizeVoiceLeading() = 0;
    virtual void resetSceneMatrix() = 0;
    virtual bool captureHeldChord(int sceneIndex) = 0;
    virtual void jumpToScene(int sceneIndex) = 0;

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

    // Audition & Drag Exports
    virtual bool isAuditioning() const = 0;
    virtual void setAudition(bool active) = 0;
    virtual ResampleState resampleState() const = 0;
    virtual void startResample() = 0;
    virtual void dragResampledWav() = 0;
    virtual void dragExportedMidi() = 0;

    // Live audio telemetry
    virtual void refreshVisuals() = 0;
    virtual const TelemetrySnapshot& visuals() const = 0;
};

} // namespace tg::ui
