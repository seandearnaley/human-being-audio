// Offline rendering: one held note through the engine with the transport playing.
#pragma once

#include "tf/params.h"

#include <vector>

namespace tf {

struct RenderRequest {
    Params params = Params::defaults();
    int note = 29;            // F1
    double bpm = 174.0;
    double bars = 4.0;        // 4/4; note held for the whole length
    double sampleRate = 48000.0;
    int oversampling = 16;
    double tailSeconds = 0.25;
};

// Facts about a render that the audio alone does not show.
struct RenderStats {
    int latencySamples = 0;        // removed from the start of the output
    int oversampling = 0;          // factor in use
    int heldSamples = 0;           // frames while the note is held; the tail follows
    float maxGainReductionDb = 0;  // limiter, over the whole render
    float meanGainReductionDb = 0;
    float maxGrowlClipDb = 0;      // growl clipper gain reduction, whole render
    float meanGrowlClipDb = 0;     // per sample, while the note is held
};

// Interleaved stereo. Transport plays from ppq 0. The engine latency is removed,
// so frame 0 is the note-on.
std::vector<float> renderOffline(const RenderRequest& request);
std::vector<float> renderOffline(const RenderRequest& request, RenderStats* stats);

}
