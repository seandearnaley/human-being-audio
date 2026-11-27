// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hba/ui/accessible_controls.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lc::ui {

inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 720;
inline constexpr int kMinWidth = 960;
inline constexpr int kMinHeight = 540;

struct PresetEntry {
    std::string name;
    std::string category;
    bool factory = true;
};

struct VisualState {
    static constexpr std::size_t kWaveformBins = 128;

    // Transport & phrase state
    bool cutting = false;
    bool bypass = false;
    bool waitingHost = true;
    int statusCode = 0; // matches DisplayState::Status
    double bpm = 120.0;
    double beatsPerBar = 4.0;
    double beat = 0.0;
    int unitsPerBar = 8;
    int phraseBars = 2;
    double blockStart = 0.0;
    double blockEnd = 0.0;
    double positionInPhrase = 0.0;
    int cutCount = 0;
    int activeCut = 0;

    // Pad performance state
    int padActiveProgram = 0;
    int padPendingProgram = -1;
    bool padEngaged = false;
    std::uint8_t midiPadMask = 0;

    // Meters & waveform tape
    std::array<float, kWaveformBins> waveform{};
    std::array<std::uint8_t, kWaveformBins> sliceMarks{};
    float outputPeakL = 0.0f;
    float outputPeakR = 0.0f;
    float limiterGrDb = 0.0f;

    // Diagnostics & status strings
    std::string headline;
    std::string captureStatus;
    bool hasCapture = false;
    std::string holdStatus;
    std::string transformStatus;
    std::string fractureStatus;
    std::string resonatorStatus;
    std::string spectralStatus;
    bool canApplyMode = false;
    bool modePending = false;
    bool activeStudio = false;
    bool activeSpectral = false;
};

class Model : public hba::ui::PresetMenuHost {
  public:
    virtual ~Model() = default;

    // Parameter access (normalised 0..1 plus formatted readout)
    virtual float getNormalised(std::string_view id) const = 0;
    virtual float getReal(std::string_view id) const = 0;
    virtual float defaultNormalised(std::string_view id) const = 0;
    virtual void beginGesture(std::string_view id) = 0;
    virtual void setNormalised(std::string_view id, float normalised) = 0;
    virtual void endGesture(std::string_view id) = 0;
    virtual std::string formatValue(std::string_view id) const = 0;
    virtual std::vector<std::string> choices(std::string_view id) const = 0;
    virtual bool isBool(std::string_view id) const = 0;

    // Presets
    virtual std::vector<PresetEntry> presets() const = 0;
    virtual std::string presetName() const = 0;
    virtual void loadPreset(int index) = 0;
    virtual void stepPreset(int delta) = 0;
    virtual void savePreset() = 0;

    // History & A/B
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual bool isSlotB() const = 0;
    virtual void toggleAB() = 0;
    virtual void copyAB() = 0;

    // Performance & workflow actions
    virtual void rollSeed() = 0;
    virtual void requestCapture() = 0;
    virtual void saveCapture() = 0;
    virtual void beginDragCaptureWav() = 0;
    virtual void applyRequestedMode() = 0;

    // Visual telemetry refresh (called at ~60 Hz on UI thread)
    virtual void refreshVisuals() = 0;
    virtual const VisualState& visuals() const = 0;
};

} // namespace lc::ui
