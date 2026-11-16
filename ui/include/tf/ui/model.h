// Everything the interface knows about the outside world. The plugin implements
// it over JUCE; tests implement it with canned data.
#pragma once

#include "tf/lanes.h"
#include "tf/params.h"

#include <array>
#include <string>
#include <vector>

namespace tf::ui {

// Display-ready data, refreshed by the model once per frame.
struct Visuals {
    static constexpr int kScopePoints = 512;
    static constexpr int kGonioPoints = 256;
    static constexpr int kSpectrumBands = 160;

    // Two periods of the master sine, phase-locked to it. -1..1.
    std::array<float, kScopePoints> scopeMaster {};
    std::array<float, kScopePoints> scopeGrowl {};
    std::array<float, kScopePoints> scopeOut {};

    // Recent output sample pairs for the phase scope. -1..1.
    std::array<float, kGonioPoints> gonioLeft {};
    std::array<float, kGonioPoints> gonioRight {};
    float correlation = 0.0f;

    // Output spectrum in dBFS, log-spaced from 20 Hz to 20 kHz.
    std::array<float, kSpectrumBands> spectrumDb {};

    float peakDbLeft = -100.0f;
    float peakDbRight = -100.0f;
    float gainReductionDb = 0.0f;

    std::array<float, kLaneCount> lanePhase {};  // 0..1
    std::array<float, kLaneCount> laneValue {};  // -1..1 after depth and motion

    // Host position in quarter notes; runs from note-on while the host is stopped.
    double ppq = 0.0;

    float hz = 0.0f;
    int note = -1;  // MIDI note, -1 when silent
    bool gate = false;
};

enum class ResampleState { idle, rendering, ready };

struct PresetInfo {
    std::string name;
    std::string category;
};

class Model {
public:
    virtual ~Model() = default;

    // Real parameter values, as in tf::Params.
    virtual float get(ParamId id) const = 0;
    virtual void begin(ParamId id) = 0;
    virtual void set(ParamId id, float real) = 0;
    virtual void end(ParamId id) = 0;

    virtual const Visuals& visuals() = 0;

    // A lane's drawn curve, played when its shape is Custom.
    virtual Curve laneCurve(int lane) const = 0;
    virtual void setLaneCurve(int lane, const Curve& curve) = 0;

    virtual std::string presetName() const = 0;
    // In menu order: grouped by category.
    virtual std::vector<PresetInfo> presets() const = 0;
    virtual void loadPreset(int index) = 0;
    virtual void stepPreset(int delta) = 0;
    virtual void savePreset() = 0;

    virtual ResampleState resampleState() const = 0;
    virtual void resample() = 0;
    // Starts a drag of the rendered file into the host. Called from a mouse drag
    // on the button once a render is ready.
    virtual void dragResampledFile() = 0;
};

}
