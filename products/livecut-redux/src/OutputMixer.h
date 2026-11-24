// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hba/dsp/crossover.h>
#include <hba/dsp/limiter.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace livecut
{
// Global Sub-Anchor LR4 crossover (mono dry sub passthrough below subAnchorHz),
// linear wet/dry, output trim (smoothed 20 ms), and optional zero-latency C^2-smooth
// linked stereo safety limiter (identity <= -1.1 dBFS, asymptotes strictly under 0 dBFS).
class OutputMixer
{
public:
  void prepare(double newSampleRate, float wet, float trimDb, float subAnchorHz = 0.0f, bool limiterOn = false) {
      sampleRate = std::max(newSampleRate, 1.0);
      mix.reset(sampleRate, 0.02);
      gain.reset(sampleRate, 0.02);
      subMix.reset(sampleRate, 0.01);
      mix.setCurrentAndTargetValue(wet);
      gain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(trimDb));
      for (auto& s : drySplit)
          s.reset();
      for (auto& s : wetSplit)
          s.reset();
      cachedSubHz = -1.0f;
      setTargets(wet, trimDb, subAnchorHz, limiterOn);
      subMix.setCurrentAndTargetValue(subAnchorHz >= 40.0f ? 1.0f : 0.0f);
      lastGrDb = 0.0f;
  }

  void setTargets(float wet, float trimDb, float subAnchorHz = 0.0f, bool limiterOn = false) {
      mix.setTargetValue(wet);
      gain.setTargetValue(juce::Decibels::decibelsToGain(trimDb));
      limiterEnabled = limiterOn;
      const bool anchorOn = subAnchorHz >= 40.0f;
      subMix.setTargetValue(anchorOn ? 1.0f : 0.0f);
      if (anchorOn) {
          const float clamped = std::clamp(subAnchorHz, 40.0f, 200.0f);
          if (std::abs(clamped - cachedSubHz) > 0.05f) {
              cachedSubHz = clamped;
              subCoefs = hba::dsp::butterworth(static_cast<double>(clamped), sampleRate);
          }
      }
  }

    void process (float dryLeft, float dryRight, float& left, float& right)
    {
        const auto wet = mix.getNextValue();
        const auto outputGain = gain.getNextValue();
        const auto anchor = subMix.getNextValue();

        if (anchor > 0.0f && cachedSubHz >= 40.0f) {
            double dryLoL = 0.0, dryHiL = 0.0, dryLoR = 0.0, dryHiR = 0.0;
            double wetLoL = 0.0, wetHiL = 0.0, wetLoR = 0.0, wetHiR = 0.0;
            drySplit[0].process(subCoefs, static_cast<double>(dryLeft), dryLoL, dryHiL);
            drySplit[1].process(subCoefs, static_cast<double>(dryRight), dryLoR, dryHiR);
            wetSplit[0].process(subCoefs, static_cast<double>(left), wetLoL, wetHiL);
            wetSplit[1].process(subCoefs, static_cast<double>(right), wetLoR, wetHiR);
            const double drySubMono = 0.5 * (dryLoL + dryLoR);
            const auto anchoredL = static_cast<float>(drySubMono + wetHiL);
            const auto anchoredR = static_cast<float>(drySubMono + wetHiR);
            left += anchor * (anchoredL - left);
            right += anchor * (anchoredR - right);
        }

        if (juce::exactlyEqual (wet, 0.0f))
        {
            left = dryLeft;
            right = dryRight;
        }
        else if (! juce::exactlyEqual (wet, 1.0f))
        {
            left = dryLeft + wet * (left - dryLeft);
            right = dryRight + wet * (right - dryRight);
        }

        if (! juce::exactlyEqual (outputGain, 1.0f))
        {
            left *= outputGain;
            right *= outputGain;
        }

        if (limiterEnabled) {
            double l = static_cast<double>(left);
            double r = static_cast<double>(right);
            // Identity below 0.88 (-1.1 dBFS); C^2-smooth linked shoulder asymptoting to 0.985 (-0.13 dBFS).
            const double gr = hba::dsp::softClipLinked(l, r, 0.88, 0.985);
            left = static_cast<float>(l);
            right = static_cast<float>(r);
            lastGrDb = gr < 0.9999 ? static_cast<float>(-20.0 * std::log10(std::max(gr, 1.0e-6))) : 0.0f;
        } else {
            lastGrDb = 0.0f;
        }
    }

    float gainReductionDb() const noexcept { return lastGrDb; }

  private:
    double sampleRate = 48000.0;
    float cachedSubHz = -1.0f;
    float lastGrDb = 0.0f;
    bool limiterEnabled = true;
    hba::dsp::SvfCoefs subCoefs{};
    hba::dsp::Lr4Split drySplit[2]{};
    hba::dsp::Lr4Split wetSplit[2]{};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix{1.0f}, gain{1.0f}, subMix{0.0f};
};
} // namespace livecut
