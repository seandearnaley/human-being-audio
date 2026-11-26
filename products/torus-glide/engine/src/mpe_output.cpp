// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/engine.h"

#include <algorithm>
#include <cmath>

namespace tg {

void Engine::noteOn(int note, float velocity, bool allowKeySwitch) noexcept HBA_NONBLOCKING {
    const int n = std::clamp(note, 0, 127);
    if (allowKeySwitch && n >= 12 && n <= 15) {
        // Low-octave C0..D#0 keyswitches glide straight to Scene A, B, C, or D without transposing root!
        keySwitchScene_ = n - 12;
        return;
    }
    const bool wasClosed = !gateOpen_;
    keysDown_[static_cast<std::size_t>(n)] = true;
    currentNote_ = n;
    velocity_ = std::clamp(velocity, 0.05f, 1.0f);
    gateOpen_ = true;
    silentTailSamples_ = 0;
    if (wasClosed)
        retrigPpqOffset_ = lastTransportPpq_;
}

void Engine::noteOff(int note) noexcept HBA_NONBLOCKING {
    const int n = std::clamp(note, 0, 127);
    if (n >= 12 && n <= 15 && !keysDown_[static_cast<std::size_t>(n)])
        return;
    keysDown_[static_cast<std::size_t>(n)] = false;
    if (sustainDown_)
        return;

    int fallback = -1;
    for (int k = 127; k >= 0; --k) {
        if (keysDown_[static_cast<std::size_t>(k)]) {
            fallback = k;
            break;
        }
    }
    if (fallback >= 0) {
        currentNote_ = fallback;
    } else {
        gateOpen_ = false;
    }
}

void Engine::sustain(bool down) noexcept HBA_NONBLOCKING {
    sustainDown_ = down;
    if (!down) {
        bool any = false;
        for (int k = 127; k >= 0; --k) {
            if (keysDown_[static_cast<std::size_t>(k)]) {
                currentNote_ = k;
                any = true;
                break;
            }
        }
        if (!any)
            gateOpen_ = false;
    }
}

void Engine::allNotesOff() noexcept HBA_NONBLOCKING {
    keysDown_.fill(false);
    sustainDown_ = false;
    gateOpen_ = false;
}

void Engine::emitMpeConfig(OutMidiBuffer& out, int offset, int rangeSt) noexcept HBA_NONBLOCKING {
    // MPE Lower Zone: Master ch 1 (0), 5 member channels (ch 2..6 -> 1..5).
    out.push(offset, 0xB0, 101, 0);
    out.push(offset, 0xB0, 100, 6);
    out.push(offset, 0xB0, 6, static_cast<std::uint8_t>(kVoiceCount));
    const auto clampedRange = static_cast<std::uint8_t>(std::clamp(rangeSt, 1, 96));
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto ch = static_cast<std::uint8_t>(v + 1);
        out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 101, 0);
        out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 100, 0);
        out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 6, clampedRange);
        out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 38, 0);
    }
    mpeConfigured_ = true;
    lastMpeRangeSt_ = rangeSt;
}

void Engine::emitMpeAllNotesOff(OutMidiBuffer& out, int offset) noexcept HBA_NONBLOCKING {
    for (int v = 0; v < kVoiceCount; ++v) {
        auto& mv = mpeVoices_[static_cast<std::size_t>(v)];
        if (mv.active) {
            const auto ch = static_cast<std::uint8_t>(v + 1);
            out.push(offset, static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(mv.baseNote), 0);
            mv.active = false;
        }
    }
}

void Engine::updateMpeVoices(OutMidiBuffer& out, int offset, const std::array<double, kVoiceCount>& pitches,
                             bool sounding, int rangeSt, double filterNorm) noexcept HBA_NONBLOCKING {
    if (!mpeConfigured_ || lastMpeRangeSt_ != rangeSt)
        emitMpeConfig(out, offset, rangeSt);

    if (!sounding) {
        emitMpeAllNotesOff(out, offset);
        return;
    }

    const int vel7 = std::clamp(static_cast<int>(std::lround(velocity_ * 127.0f)), 1, 127);
    const int timbre7 = std::clamp(static_cast<int>(std::lround(filterNorm * 127.0)), 0, 127);

    for (int v = 0; v < kVoiceCount; ++v) {
        auto& mv = mpeVoices_[static_cast<std::size_t>(v)];
        const auto ch = static_cast<std::uint8_t>(v + 1);
        const double targetPitch = std::clamp(pitches[static_cast<std::size_t>(v)], 12.0, 116.0);

        if (!mv.active) {
            mv.baseNote = std::clamp(static_cast<int>(std::lround(targetPitch)), 12, 116);
            mv.lastBend14 = 8192;
            mv.lastTimbre7 = timbre7;
            out.push(offset, static_cast<std::uint8_t>(0xE0 | ch), 0, 64);
            out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 74, static_cast<std::uint8_t>(timbre7));
            out.push(offset, static_cast<std::uint8_t>(0x90 | ch), static_cast<std::uint8_t>(mv.baseNote),
                     static_cast<std::uint8_t>(vel7));
            mv.active = true;
        }

        const double bendSt = std::clamp(targetPitch - static_cast<double>(mv.baseNote), -static_cast<double>(rangeSt),
                                         static_cast<double>(rangeSt));
        const double bendNorm = bendSt / std::max(1.0, static_cast<double>(rangeSt));
        const int bend14 = std::clamp(static_cast<int>(std::lround(8192.0 + bendNorm * 8191.0)), 0, 16383);

        if (std::abs(bend14 - mv.lastBend14) >= 4) {
            out.push(offset, static_cast<std::uint8_t>(0xE0 | ch), static_cast<std::uint8_t>(bend14 & 0x7F),
                     static_cast<std::uint8_t>((bend14 >> 7) & 0x7F));
            mv.lastBend14 = bend14;
        }
        if (std::abs(timbre7 - mv.lastTimbre7) >= 2) {
            out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 74, static_cast<std::uint8_t>(timbre7));
            mv.lastTimbre7 = timbre7;
        }
    }
}

} // namespace tg
