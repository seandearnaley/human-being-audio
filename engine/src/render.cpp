#include "tf/render.h"

#include "tf/engine.h"

#include <algorithm>
#include <cmath>

namespace tf {

std::vector<float> renderOffline(const RenderRequest& request) { return renderOffline(request, nullptr); }

std::vector<float> renderOffline(const RenderRequest& request, RenderStats* stats) {
    constexpr int kBlock = 64;  // small, so the gain-reduction statistics are fine-grained
    const double sampleRate = request.sampleRate > 0.0 ? request.sampleRate : 48000.0;
    const double bpm = std::clamp(request.bpm, 20.0, 999.0);

    Engine engine;
    engine.prepare(sampleRate, kBlock, request.oversampling);
    engine.setLaneCurves(request.curves);
    const int latency = engine.latencySamples();

    const double heldSeconds = std::max(0.0, request.bars) * 4.0 * 60.0 / bpm;
    const auto held = static_cast<long long>(std::llround(heldSeconds * sampleRate));
    const auto tail = static_cast<long long>(std::llround(std::max(0.0, request.tailSeconds) * sampleRate));
    const long long total = held + tail;  // frames delivered
    const long long rendered = total + latency;

    std::vector<float> out(static_cast<std::size_t>(total) * 2, 0.0f);
    std::vector<float> left(kBlock), right(kBlock);

    Transport transport;
    transport.bpm = bpm;
    transport.playing = true;

    engine.noteOn(request.note);
    bool released = false;
    double grSum = 0.0, grMax = 0.0, clipSum = 0.0, clipMax = 0.0;
    long long blocks = 0;
    for (long long pos = 0; pos < rendered;) {
        // The note-off lands exactly on frame `held` of the input timeline.
        long long end = std::min(rendered, pos + kBlock);
        if (!released && pos < held && end > held) end = held;
        if (!released && pos >= held) {
            engine.noteOff(request.note);
            released = true;
        }
        const int n = static_cast<int>(end - pos);
        transport.ppq = static_cast<double>(pos) * bpm / 60.0 / sampleRate;
        engine.process(request.params, transport, left.data(), right.data(), n);
        const double gr = engine.gainReductionDb();
        grSum += gr;
        grMax = std::max(grMax, gr);
        ++blocks;
        clipMax = std::max(clipMax, static_cast<double>(engine.growlClipDb()));
        if (!released) clipSum += static_cast<double>(engine.growlClipMeanDb()) * n;
        for (int i = 0; i < n; ++i) {
            const long long frame = pos + i - latency;
            if (frame < 0 || frame >= total) continue;
            out[static_cast<std::size_t>(frame) * 2] = left[static_cast<std::size_t>(i)];
            out[static_cast<std::size_t>(frame) * 2 + 1] = right[static_cast<std::size_t>(i)];
        }
        pos = end;
    }

    if (stats != nullptr) {
        stats->latencySamples = latency;
        stats->oversampling = engine.oversamplingFactor();
        stats->heldSamples = static_cast<int>(held);
        stats->maxGainReductionDb = static_cast<float>(grMax);
        stats->meanGainReductionDb = blocks > 0 ? static_cast<float>(grSum / static_cast<double>(blocks)) : 0.0f;
        stats->maxGrowlClipDb = static_cast<float>(clipMax);
        stats->meanGrowlClipDb = held > 0 ? static_cast<float>(clipSum / static_cast<double>(held)) : 0.0f;
    }
    return out;
}

}
