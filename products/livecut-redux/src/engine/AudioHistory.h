// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "hba/dsp/nonblocking.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace livecut::engine
{

struct SourceRegion
{
    std::uint64_t epoch;
    std::int64_t firstFrame;
    std::int64_t frameCount;
};

struct StereoFrame
{
    float left;
    float right;
};

// Single-threaded stereo source storage, shared by the producer and renderer on
// the audio thread. Regions identify this instance's audio, never ring slots.
// A region must only be passed back to the instance that created it; epochs do
// not identify different AudioHistory instances.
// Availability does not pin a region: a renderer must retain its source for the
// entire voice lifetime, or use an immutable captured snapshot outside this class.
class AudioHistory
{
public:
    AudioHistory() = default;
    AudioHistory (const AudioHistory&) = delete;
    AudioHistory& operator= (const AudioHistory&) = delete;
    AudioHistory (AudioHistory&&) = delete;
    AudioHistory& operator= (AudioHistory&&) = delete;

    // The only allocating operation. Invalid capacities leave existing audio
    // intact; allocation failures propagate to the non-real-time caller.
    // Successful preparation starts a new epoch, even at the same capacity.
    bool prepare (std::size_t capacity)
    {
        if (capacity == 0 || std::cmp_greater (capacity, maximumFrame)
            || capacity > frames.max_size() || epoch == maximumEpoch)
            return false;

        frames.resize (capacity);
        return reset();
    }

    // Never wrap an epoch back onto an old source identity. Exhausting uint64_t
    // invalidates all audio and requires a new instance; false reports this.
    bool reset() noexcept HBA_NONBLOCKING {
        nextWritten = 0;
        if (epoch == maximumEpoch)
        {
            active = false;
            return false;
        }

        ++epoch;
        active = ! frames.empty();
        return true;
    }

    // Absolute frame positions never wrap. Once INT64_MAX frames have arrived,
    // appends fail without changing audio; the caller must explicitly reset.
    bool append(StereoFrame frame) noexcept HBA_NONBLOCKING {
        if (! active || nextWritten == maximumFrame)
            return false;

        frames[index (nextWritten)] = frame;
        ++nextWritten;
        return true;
    }

    // The half-open available interval is [firstFrame, firstFrame + frameCount).
    SourceRegion retainedRegion() const noexcept HBA_NONBLOCKING {
        const auto first = oldestRetained();
        return { epoch, first, nextWritten - first };
    }

    // Empty, malformed, partially arrived and partially expired regions fail.
    bool isAvailable(SourceRegion region) const noexcept HBA_NONBLOCKING {
        return validRegion (region) && region.firstFrame >= oldestRetained()
            && region.firstFrame <= nextWritten
            && region.frameCount <= nextWritten - region.firstFrame;
    }

    // A planned region may still be arriving or partly expired. Only this
    // position must be retained; check isAvailable separately when full source
    // completion is required (for example, before starting a reverse voice).
    std::optional<StereoFrame> read(SourceRegion region, std::int64_t offset) const noexcept HBA_NONBLOCKING {
        if (! validRegion (region) || offset < 0 || offset >= region.frameCount)
            return std::nullopt;

        const auto absolute = region.firstFrame + offset;
        if (absolute < oldestRetained() || absolute >= nextWritten)
            return std::nullopt;

        return frames[index (absolute)];
    }

    // offset is the integer source position; fraction must be finite in [0, 1).
    // Keeping the integer separate avoids double's loss of absolute-frame
    // precision. Direction and rate belong to the caller's playback transform.
    // An exact integer needs no next frame; fractional reads require both
    // neighbors inside the declared region AND the retained source interval.
    std::optional<StereoFrame> readFractional(SourceRegion region, std::int64_t offset,
                                              double fraction) const noexcept HBA_NONBLOCKING {
        if (! std::isfinite (fraction) || fraction < 0.0 || fraction >= 1.0)
            return std::nullopt;

        const auto first = read (region, offset);
        if (! first || fraction == 0.0)
            return first;

        // A successful read implies offset < frameCount <= INT64_MAX, so +1 is safe.
        const auto second = read (region, offset + 1);
        if (! second)
            return std::nullopt;

        const auto blend = static_cast<float> (fraction);
        return StereoFrame { first->left + blend * (second->left - first->left),
                             first->right + blend * (second->right - first->right) };
    }

    // Causal monotone cubic Hermite (PCHIP) interpolation over {-1, 0, +1}.
    // Needs zero lookahead beyond offset + 1, so 0-latency Live and 500ms-lookahead
    // Studio modes read the exact same frames and remain bit-identical. Because
    // the normalized endpoint slopes stay within [0, 2], the curve is C1-smooth
    // with O(h^3) accuracy on waveforms while mathematically guaranteeing zero
    // peak overshoot beyond [min(y0, y1), max(y0, y1)].
    std::optional<StereoFrame> readCubic(SourceRegion region, std::int64_t offset,
                                         double fraction) const noexcept HBA_NONBLOCKING {
        if (!std::isfinite(fraction) || fraction < 0.0 || fraction >= 1.0)
            return std::nullopt;

        const auto y0 = read(region, offset);
        if (!y0 || fraction == 0.0)
            return y0;

        const auto y1 = read(region, offset + 1);
        if (!y1)
            return std::nullopt;

        const auto ym1 = offset >= 1 ? read(region, offset - 1) : std::nullopt;
        if (!ym1) {
            const auto blend = static_cast<float>(fraction);
            return StereoFrame{y0->left + blend * (y1->left - y0->left), y0->right + blend * (y1->right - y0->right)};
        }

        const float t = static_cast<float>(fraction);
        const float t2 = t * t;
        const float omt = 1.0f - t;
        const float h01 = t2 * (3.0f - 2.0f * t);
        const float h10 = t * omt * omt;
        const float h11 = t2 * (t - 1.0f);

        const auto interp = [=](float pm1, float p0, float p1) noexcept {
            const float d0 = p1 - p0;
            if (std::abs(d0) <= 1.0e-30f)
                return p0;
            const float r = (p0 - pm1) / d0;
            const float a = r > 0.0f ? (2.0f * r) / (1.0f + r) : 0.0f;
            const float b = std::clamp(1.5f - 0.5f * r, 0.0f, 2.0f);
            const float u = std::clamp(h01 + a * h10 + b * h11, 0.0f, 1.0f);
            return std::clamp(p0 + u * d0, std::min(p0, p1), std::max(p0, p1));
        };

        return StereoFrame{interp(ym1->left, y0->left, y1->left), interp(ym1->right, y0->right, y1->right)};
    }

    struct Window
    {
        std::span<const StereoFrame> first, second;
    };

    // Valid only until the next append/reset/prepare on this single-threaded
    // history. Validate once, then read a finite FIR window without per-tap
    // modulo or changing the Classic read/interpolation path.
    std::optional<Window> readWindow(SourceRegion region, std::int64_t offset,
                                     std::size_t count) const noexcept HBA_NONBLOCKING {
        if (! validRegion (region) || offset < 0 || count == 0
            || std::cmp_greater (count, region.frameCount)
            || offset > region.frameCount - static_cast<std::int64_t> (count))
            return std::nullopt;
        const auto absolute = region.firstFrame + offset;
        if (absolute < oldestRetained() || absolute > nextWritten
            || std::cmp_greater (count, nextWritten - absolute))
            return std::nullopt;
        const auto firstIndex = index (absolute);
        const auto firstCount = std::min (count, frames.size() - firstIndex);
        return Window { { frames.data() + firstIndex, firstCount },
                        { frames.data(), count - firstCount } };
    }

private:
    static constexpr auto maximumFrame = std::numeric_limits<std::int64_t>::max();
    static constexpr auto maximumEpoch = std::numeric_limits<std::uint64_t>::max();

    bool validRegion(SourceRegion region) const noexcept HBA_NONBLOCKING {
        return active && region.epoch == epoch && region.firstFrame >= 0
            && region.frameCount > 0 && region.frameCount <= maximumFrame - region.firstFrame;
    }

    std::int64_t oldestRetained() const noexcept HBA_NONBLOCKING {
        const auto capacity = static_cast<std::int64_t> (frames.size());
        return nextWritten > capacity ? nextWritten - capacity : 0;
    }

    std::size_t index(std::int64_t absolute) const noexcept HBA_NONBLOCKING {
        return static_cast<std::size_t> (absolute % static_cast<std::int64_t> (frames.size()));
    }

    std::vector<StereoFrame> frames;
    std::uint64_t epoch = 0;
    std::int64_t nextWritten = 0;
    bool active = false;
};

} // namespace livecut::engine
