// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sb/export.h"

#include <hba/dsp/gain.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>

namespace sb {

namespace {
void writeLe16(std::ofstream& f, std::uint16_t v) {
    const char b[2] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu)};
    f.write(b, 2);
}

void writeLe32(std::ofstream& f, std::uint32_t v) {
    const char b[4] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu),
                       static_cast<char>((v >> 16) & 0xFFu), static_cast<char>((v >> 24) & 0xFFu)};
    f.write(b, 4);
}
} // namespace

std::vector<float> renderOneShot(const OneShotRequest& req) {
    const double sr = req.sampleRate > 8000.0 ? req.sampleRate : 48000.0;
    const double tight = std::clamp(static_cast<double>(req.params.real(ParamId::macroTight)) * 0.01, -1.0, 1.0);
    const double decayScale = std::pow(2.45, -tight);
    const double maxDecaySec = std::max({static_cast<double>(req.params.real(ParamId::bodyDecay)),
                                         static_cast<double>(req.params.real(ParamId::shellDecay)),
                                         static_cast<double>(req.params.real(ParamId::wireDecay))}) *
                               0.001 * decayScale;
    const double maxSec = std::clamp(maxDecaySec * 5.2 + 0.25, 0.40, 3.8);
    const int totalFrames = static_cast<int>(std::llround(maxSec * sr));

    constexpr int kBlock = 256;
    Engine engine;
    engine.prepare(sr, kBlock, req.oversampling);
    Params p = req.params;
    p.set(ParamId::humanize, 0.0f);
    // Prime cached params and trigger directly (invariant to Kit Map vs Chromatic mode!)
    engine.process(p, nullptr, nullptr, 0);
    engine.trigger(req.articulation, req.velocity, req.pitchRatio);

    std::vector<float> raw(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    std::vector<float> bufL(kBlock), bufR(kBlock);
    int done = 0;
    while (done < totalFrames) {
        const int n = std::min(kBlock, totalFrames - done);
        engine.process(p, bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i) {
            raw[static_cast<std::size_t>(done + i) * 2u] = bufL[static_cast<std::size_t>(i)];
            raw[static_cast<std::size_t>(done + i) * 2u + 1u] = bufR[static_cast<std::size_t>(i)];
        }
        done += n;
    }

    // Strip exact leading latency silence so every bounced one-shot hits SAMPLE-0 grid-locked!
    const int latFrames = std::clamp(engine.latencySamples(), 0, totalFrames / 4);
    float globalPeak = 1e-6f;
    for (float s : raw)
        globalPeak = std::max(globalPeak, std::abs(s));
    const float onsetThresh = globalPeak * static_cast<float>(hba::dsp::dbToGain(-48.0));
    int startFrame = latFrames;
    const int maxSearch = std::min(totalFrames - 1, latFrames + static_cast<int>(0.012 * sr));
    for (int i = latFrames; i < maxSearch; ++i) {
        if (std::abs(raw[static_cast<std::size_t>(i) * 2u]) >= onsetThresh ||
            std::abs(raw[static_cast<std::size_t>(i) * 2u + 1u]) >= onsetThresh) {
            startFrame = std::max(0, i - 2);
            break;
        }
    }

    // Trim trailing silence below -72 dBFS (keeping at least 180 ms)
    const float tailThresh = static_cast<float>(hba::dsp::dbToGain(-72.0));
    int lastAudible = std::min(totalFrames - 1, startFrame + static_cast<int>(0.18 * sr));
    for (int i = totalFrames - 1; i >= lastAudible; --i) {
        const float l = std::abs(raw[static_cast<std::size_t>(i) * 2u]);
        const float r = std::abs(raw[static_cast<std::size_t>(i) * 2u + 1u]);
        if (l > tailThresh || r > tailThresh) {
            lastAudible = i;
            break;
        }
    }
    const int tailPad = static_cast<int>(0.012 * sr);
    const int endFrame = std::min(totalFrames, lastAudible + tailPad);
    const int outFrames = std::max(1, endFrame - startFrame);

    std::vector<float> out(static_cast<std::size_t>(outFrames) * 2u);
    for (int i = 0; i < outFrames; ++i) {
        out[static_cast<std::size_t>(i) * 2u] = raw[static_cast<std::size_t>(startFrame + i) * 2u];
        out[static_cast<std::size_t>(i) * 2u + 1u] = raw[static_cast<std::size_t>(startFrame + i) * 2u + 1u];
    }

    // 4-sample micro fade-in if needed + 6 ms raised-cosine fade-out to exact zero
    const int fadeIn = std::min(4, outFrames);
    for (int i = 0; i < fadeIn; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(fadeIn);
        out[static_cast<std::size_t>(i) * 2u] *= g;
        out[static_cast<std::size_t>(i) * 2u + 1u] *= g;
    }
    const int fadeOut = std::min(outFrames / 4, static_cast<int>(0.006 * sr));
    for (int i = 0; i < fadeOut; ++i) {
        const float g = 0.5f * (1.0f + std::cos(3.14159265f * static_cast<float>(i) / static_cast<float>(fadeOut)));
        const auto idx = static_cast<std::size_t>(outFrames - fadeOut + i) * 2u;
        out[idx] *= g;
        out[idx + 1u] *= g;
    }

    if (req.normalizePeak) {
        float peak = 1e-6f;
        for (float s : out)
            peak = std::max(peak, std::abs(s));
        const float target = static_cast<float>(hba::dsp::dbToGain(-0.5));
        const float gain = target / peak;
        for (float& s : out)
            s *= gain;
    }
    return out;
}

bool writeWavFloat(const std::string& path, const std::vector<float>& interleavedStereo, int sampleRate) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(interleavedStereo.size() * sizeof(float));
    const std::uint32_t riffSize = 36u + dataBytes;

    f.write("RIFF", 4);
    writeLe32(f, riffSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeLe32(f, 16u);
    writeLe16(f, 3u); // IEEE float
    writeLe16(f, 2u); // Stereo
    writeLe32(f, static_cast<std::uint32_t>(sampleRate));
    writeLe32(f, static_cast<std::uint32_t>(sampleRate * 2 * 4));
    writeLe16(f, 8u);
    writeLe16(f, 32u);
    f.write("data", 4);
    writeLe32(f, dataBytes);
    f.write(reinterpret_cast<const char*>(interleavedStereo.data()), static_cast<std::streamsize>(dataBytes));
    return f.good();
}

std::string keyTaggedStem(const std::string& presetName, float bodyHz) {
    std::string clean;
    for (char c : presetName) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            clean.push_back(c);
        else if (c == ' ' || c == '-' || c == '_') {
            if (!clean.empty() && clean.back() != '_')
                clean.push_back('_');
        }
    }
    while (!clean.empty() && clean.back() == '_')
        clean.pop_back();
    if (clean.empty())
        clean = "StrikeBody";
    char suffix[40]{};
    const std::string note = hzToNoteTag(bodyHz);
    std::snprintf(suffix, sizeof(suffix), "_%s_%.0fHz", note.c_str(), static_cast<double>(bodyHz));
    return clean + suffix;
}

} // namespace sb
