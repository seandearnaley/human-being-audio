#pragma once

#include "pf/cubes.h"
#include "pf/lanes.h"
#include "pf/params.h"

#include <array>
#include <string>
#include <vector>

namespace pf::ui {

struct Visuals {
    static constexpr int kGonioPoints = 256;
    static constexpr int kSpectrumBands = 160;

    std::array<float, kGonioPoints> gonioLeft {};
    std::array<float, kGonioPoints> gonioRight {};
    float correlation = 0.0f;

    std::array<float, kSpectrumBands> spectrumDb {};

    float peakDbLeft = -100.0f;
    float peakDbRight = -100.0f;
    float gainReductionDb = 0.0f;

    std::array<float, kLaneCount> lanePhase {};
    std::array<float, kLaneCount> laneValue {};
    double ppq = 0.0;
    double sampleRate = 48000.0;

    // Effective modulated Z-plane values from the audio engine.
    float effMorphX = 0.36f;
    float effMorphY = 0.28f;
    float effMorphZ = 0.18f;
    float effFreq = 0.0f;
    float effRes = 0.62f;
    float effTransform = 0.0f;
    float effSpread = 0.22f;
    float midiShiftSt = 0.0f;
    int activeNote = -1;
};

struct PresetInfo {
    std::string name;
    std::string category;
};

class Model {
public:
    virtual ~Model() = default;

    virtual float get(ParamId id) const = 0;
    virtual void begin(ParamId id) = 0;
    virtual void set(ParamId id, float real) = 0;
    virtual void end(ParamId id) = 0;

    virtual const Visuals& visuals() = 0;

    virtual Curve laneCurve(int lane) const = 0;
    virtual void setLaneCurve(int lane, const Curve& curve) = 0;

    virtual ImprintData imprint() const = 0;
    virtual std::string imprintLabel() const = 0;
    virtual void browseImprintAudio() = 0;
    virtual void clearImprint() = 0;

    virtual std::string presetName() const = 0;
    virtual std::vector<PresetInfo> presets() const = 0;
    virtual void loadPreset(int index) = 0;
    virtual void stepPreset(int delta) = 0;
    virtual void savePreset() = 0;
};

}
