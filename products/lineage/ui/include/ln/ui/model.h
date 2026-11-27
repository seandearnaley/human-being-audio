// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <hba/ui/accessible_controls.h>

#include "ln/engine.h"
#include "ln/lanes.h"
#include "ln/params.h"

#include <array>
#include <string>
#include <vector>

namespace ln::ui {

struct PresetInfo {
    std::string name;
    std::string category;
};

enum class PrintState : std::uint8_t { idle, printing, ready };
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

    virtual Curve laneCurve(int lane) const = 0;
    virtual void setLaneCurve(int lane, const Curve& curve) = 0;

    // Non-destructive Gen-2 / Gen-N resampling controls
    virtual const std::array<float, PrintBuffer::kWaveformPoints>& printPeaks() const = 0;
    virtual std::string printLabel() const = 0;
    virtual int printGeneration() const = 0;
    virtual PrintState printState() const = 0;
    virtual void triggerPrint(bool stackGeneration) = 0;
    virtual bool canStepBackPrint() const { return false; }
    virtual void stepBackPrint() {}
    virtual void browseLoadWav() = 0;
    virtual void clearPrint() = 0;

    // 16x offline WAV drag-to-DAW export
    virtual ExportState exportState() const { return ExportState::idle; }
    virtual void startWavExport() {}
    virtual void dragExportedWav() {}

    // Preset & history management
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

} // namespace ln::ui
