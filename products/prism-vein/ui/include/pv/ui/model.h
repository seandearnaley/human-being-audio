// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <hba/ui/accessible_controls.h>

#include "pv/engine.h"
#include "pv/params.h"
#include "pv/preset.h"

#include <hba/dsp/curve.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pv::ui {

struct PresetEntry {
    std::string category;
    std::string name;
};

class Model : public hba::ui::PresetMenuHost {
  public:
    virtual ~Model() = default;

    virtual float get(ParamId id) const = 0;
    virtual void begin(ParamId id) = 0;
    virtual void set(ParamId id, float real) = 0;
    virtual void end(ParamId id) = 0;

    virtual std::uint16_t chordMask() const = 0;
    virtual void setChordMask(std::uint16_t mask) = 0;

    virtual hba::dsp::Curve laneCurve(int lane) const = 0;
    virtual void setLaneCurve(int lane, const hba::dsp::Curve& curve) = 0;

    virtual const TelemetrySnapshot& visuals() = 0;
    virtual void refreshVisuals() {}

    virtual std::string presetName() const = 0;
    virtual std::vector<PresetEntry> presets() const = 0;
    virtual void loadPreset(int index) = 0;
    virtual void stepPreset(int delta) = 0;
    virtual void savePreset() = 0;

    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual bool isSlotB() const = 0;
    virtual void toggleAB() = 0;
    virtual void copyAB() = 0;
};

} // namespace pv::ui
