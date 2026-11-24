// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "engine/Cut.h"
#include "engine/AudioHistory.h"

#include <array>
#include <span>
#include <optional>

namespace livecut::engine
{

// Livecut's cut envelope at sample `t` of a cut `length` samples long: an
// exponential attack and release over `fade` samples.
float fadeEnvelope(int t, int fade, int length) noexcept HBA_NONBLOCKING;

// How many samples of a pitched cut can sound before its read head overtakes
// the recording. The cut starts once `recorded` samples of its block have been
// recorded, and at time t it reads samples floor(t * rate) and floor(t * rate) + 1.
int playableSamples(int length, double rate, int recorded, int capacity) noexcept HBA_NONBLOCKING;

// Plays one block of cuts on a live stereo input. The first cut plays the
// input as it arrives and each later cut replays the block from its start,
// so cutting adds no latency.
class LivePlayer
{
public:
    struct PlayingCut
    {
        int start = 0;    // samples after the block starts
        int size = 0;     // samples until the next cut starts
        int playable = 0; // samples that sound before the gate closes
        int soundingFrames = 0; // physical musical gate, independent of classic availability
        bool transformDecided = false;
        double units = 0.0;
        double logicalStart = 0.0;
        double gate = 1.0;
        bool pitched = false;
        double rate = 1.0;
        float leftToLeft = 1.0f, leftToRight = 0.0f, rightToLeft = 0.0f, rightToRight = 1.0f;
        Cut requested;
    };

    // Declares the classic source extent; retains eight such extents plus two guards.
    void prepare (int capacity, std::size_t retainedFrames = 0);

    void setFadeSamples(int samples) noexcept HBA_NONBLOCKING;

    // Starts a block. Sizes are rounded cumulatively, so they add up to the
    // block's length in samples exactly. The first cut plays the input live,
    // so it honours only a pitch below the original.
    void startBlock(std::span<const Cut> cuts, double samplesPerUnit) noexcept HBA_NONBLOCKING;
    void startBlock(std::span<const Cut> cuts, double samplesPerUnit, SourceRegion source) noexcept HBA_NONBLOCKING;

    // Raw ingestion and rendering are separate: a future delayed renderer can
    // address an explicit older frame without ingesting that audio twice.
    SourceRegion sourceAtFrame(std::int64_t frame) const noexcept HBA_NONBLOCKING;
    SourceRegion sourceForNextFrame() const noexcept HBA_NONBLOCKING;
    bool ingestFrame(float left, float right) noexcept HBA_NONBLOCKING;
    void renderFrame(float& left, float& right, bool advanceBySamples = true) noexcept HBA_NONBLOCKING;
    void advanceFrame(bool advanceBySamples = true) noexcept HBA_NONBLOCKING;
    int currentCutAge() const noexcept HBA_NONBLOCKING { return position; }
    void resetSource() noexcept HBA_NONBLOCKING;
    void resetRendering() noexcept HBA_NONBLOCKING;
    std::optional<StereoFrame> readSourceFrame(double relativeFrame) const noexcept HBA_NONBLOCKING;
    std::optional<StereoFrame> readSourceFrame(SourceRegion region,
                                               double relativeFrame) const noexcept HBA_NONBLOCKING;
    std::optional<AudioHistory::Window> readSourceWindow(SourceRegion region, std::int64_t offset,
                                                         std::size_t count) const noexcept HBA_NONBLOCKING {
        return history.readWindow(region, offset, count);
    }
    std::optional<StereoFrame> rawFrame(std::int64_t frame) const noexcept HBA_NONBLOCKING {
        return history.read ({ history.retainedRegion().epoch, frame, 1 }, 0);
    }
    bool hasSourceError() const noexcept HBA_NONBLOCKING { return sourceError; }
    // Activation only: use the physical gate, not classic live-read availability.
    int currentSoundingFrames() const noexcept HBA_NONBLOCKING;
    void markCurrentTransformDecided() noexcept HBA_NONBLOCKING;
    // Only at the first sample of an undecided cut. The complete fixed source
    // must cover its entire planned physical gate and fit the classic extent.
    // Success freezes source/direction/rate AND the original output-frame budget.
    bool setCurrentTransform(SourceRegion completeFixedSpan, bool reverse) noexcept HBA_NONBLOCKING;
    bool sourceAvailable(SourceRegion completeFixedSpan) const noexcept HBA_NONBLOCKING;
    SourceRegion retainedSource() const noexcept HBA_NONBLOCKING;
    void advanceToUnits(double logicalUnits) noexcept HBA_NONBLOCKING;
    void retimeMusicalCut(int samplesToEnd, int samplesToGate,
                          std::int64_t renderedExclusiveEnd) noexcept HBA_NONBLOCKING;

    // Records one input frame and replaces it with the output frame.
    void process(float& left, float& right) noexcept HBA_NONBLOCKING;

    // Re-times the rest of the block for a new tempo: what has already played
    // stays, and everything after it is laid out again at the new length of a unit.
    void setSamplesPerUnit(double samplesPerUnit) noexcept HBA_NONBLOCKING;
    // For this block only: count the current cut's elapsed units across re-times,
    // each stretch at the tempo it played at, rather than all of it at the last
    // tempo. The pad's Tape asks for it so a stop ends on its grid point through
    // tempo automation; every other block keeps the original count.
    void setExactRetime(bool exact) noexcept HBA_NONBLOCKING { exactRetime = exact; }

    std::span<const PlayingCut> cuts() const noexcept HBA_NONBLOCKING { return {playing.data(), playingCount}; }

    // The playing cut from its next frame on, as plain data, so a restart can let it
    // play on under what follows. It carries the cut's read and stereo matrix, not its
    // fade; the pad's Rush, which takes it, plays its cuts unfaded.
    struct Tail
    {
        SourceRegion source {};
        double position = 0.0, rate = 1.0;
        float ll = 1, lr = 0, rl = 0, rr = 1;
        // Renders the next frame; false once its source is gone.
        bool next(const LivePlayer&, float& left, float& right) noexcept HBA_NONBLOCKING;
    };
    // Empty once the block has run out, its cut has gone silent, or under a transform.
    std::optional<Tail> tail() const noexcept HBA_NONBLOCKING;
    // Cut `index` of this block from its frame `position` on, which may be its end:
    // a cut that played to its end runs on as it would have. Empty if it fell
    // silent before `position`.
    std::optional<Tail> tail(std::size_t index, int position) const noexcept HBA_NONBLOCKING;

    // Index of the cut playing next; cuts().size() once the block has run out.
    std::size_t currentCut() const noexcept HBA_NONBLOCKING { return current; }

  private:
    int playableFor(const PlayingCut& cut, int soundingLength) const noexcept HBA_NONBLOCKING;
    void skipFinishedCuts() noexcept HBA_NONBLOCKING;

    struct Transform
    {
        SourceRegion source;
        double rate;
        int outputFrames;
        bool reverse;
    };
    std::optional<Transform> transform;

    AudioHistory history;
    SourceRegion source {};
    bool sourceError = false;
    std::array<PlayingCut, maxCutsPerBlock> playing{};
    std::size_t playingCount = 0;
    int capacity = 0;
    std::size_t current = 0;
    int position = 0; // samples into the current cut
    bool exactRetime = false;
    double retimedUnits = 0.0; // the current cut's units up to its last re-time, when exact
    int retimedPosition = 0;
    int fade = 1;
    double samplesPerUnit = 1.0;
};

} // namespace livecut::engine
