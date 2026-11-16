// How control positions become engine quantities. These few curves decide how
// the synth feels under the hand, so they live in one place and are tuned by ear.
#pragma once

#include <cmath>

namespace tf::feel {

// Phase-modulation index in radians for a voice's PM amount (-1..1) and the
// Coupling macro (0..1).
inline float pmIndex(float pm, float coupling) {
    constexpr float maxRadians = 8.0f;
    return pm * coupling * maxRadians;
}

// A voice's detune in cents for its Detune setting and the Beat macro (0..1).
inline float detuneCents(float detune, float beat) {
    return detune * beat;
}

// Linear gain into the folder for Drive (0..1).
inline float driveGain(float drive) {
    constexpr float maxDb = 30.0f;
    return std::pow(10.0f, drive * maxDb / 20.0f);
}

// Feedback into the voices' phase, in radians, for Feedback (0..1). Squared:
// the loop turns chaotic above about 0.8 rad, and a linear control reached that
// a third of the way up.
inline float feedbackRadians(float amount) {
    constexpr float maxRadians = 6.2831853f;
    return amount * amount * maxRadians;
}

}
