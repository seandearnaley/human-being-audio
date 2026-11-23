// Monophonic voice control: the note stack (last-note priority), glide and the
// amp gate.
#pragma once

#include "tf/nonblocking.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tf::dsp {

class NoteStack {
public:
    // Adds `note` as the most recent (moving it up if already held).
    void push(int note) noexcept TF_NONBLOCKING {
        remove(note);
        if (count_ == static_cast<int>(notes_.size())) remove(notes_[0]);
        notes_[static_cast<std::size_t>(count_++)] = static_cast<std::uint8_t>(note);
    }
    // Returns true if the note was held.
    bool remove(int note) noexcept TF_NONBLOCKING {
        for (int i = 0; i < count_; ++i) {
            if (notes_[static_cast<std::size_t>(i)] != note) continue;
            for (int j = i + 1; j < count_; ++j) notes_[static_cast<std::size_t>(j - 1)] = notes_[static_cast<std::size_t>(j)];
            --count_;
            return true;
        }
        return false;
    }
    void clear() noexcept TF_NONBLOCKING { count_ = 0; }
    bool empty() const noexcept TF_NONBLOCKING { return count_ == 0; }
    int size() const noexcept TF_NONBLOCKING { return count_; }
    int top() const noexcept TF_NONBLOCKING { return count_ > 0 ? notes_[static_cast<std::size_t>(count_ - 1)] : -1; }

private:
    std::array<std::uint8_t, 128> notes_ {};
    int count_ = 0;
};

// Pitch in semitones, moving linearly to a new note over a fixed time.
class Glide {
public:
    void jump(double note) noexcept TF_NONBLOCKING {
        from_ = to_ = current_ = note;
        position_ = length_ = 0.0;
    }
    void glideTo(double note, double lengthSamples) noexcept TF_NONBLOCKING {
        if (lengthSamples <= 0.0) return jump(note);
        from_ = current_;
        to_ = note;
        position_ = 0.0;
        length_ = lengthSamples;
    }
    // Moves `samples` forward and returns the pitch there.
    double advance(double samples) noexcept TF_NONBLOCKING {
        if (position_ < length_) {
            position_ = std::min(length_, position_ + samples);
            current_ = from_ + (to_ - from_) * (position_ / length_);
        } else {
            current_ = to_;
        }
        return current_;
    }
    double current() const noexcept TF_NONBLOCKING { return current_; }
    double target() const noexcept TF_NONBLOCKING { return to_; }

private:
    double from_ = 0.0, to_ = 0.0, current_ = 0.0;
    double position_ = 0.0, length_ = 0.0;
};

// Shaped exponential ADSR amp envelope with a 1 ms linear fade used before a
// phase reset while the gate is still open.
class Gate {
public:
  enum class Stage : std::uint8_t { closed, attack, decay, sustain, release, fade };

  static constexpr double kAttackSeconds = 0.002;
  static constexpr double kReleaseSeconds = 0.030;
  static constexpr double kFadeSeconds = 0.001;

  void prepare(double sampleRate) noexcept TF_NONBLOCKING {
      sr_ = std::max(sampleRate, 1000.0);
      fadeStep_ = 1.0 / (kFadeSeconds * sr_);
      setEnvelope(2.0, 180.0, 1.0, 30.0);
  }

  void setEnvelope(double attackMs, double decayMs, double sustain, double releaseMs) noexcept TF_NONBLOCKING {
      const double atkSec = std::clamp(attackMs, 0.5, 5000.0) * 1.0e-3;
      const double decSec = std::clamp(decayMs, 5.0, 10000.0) * 1.0e-3;
      const double relSec = std::clamp(releaseMs, 2.0, 10000.0) * 1.0e-3;
      // RC charge targeting 1.6 reaches 1.0 in ~atkSec with natural convex curve.
      attackCoef_ = 1.0 - std::exp(-0.980829253 / (atkSec * sr_));
      decayCoef_ = 1.0 - std::exp(-4.605170186 / (decSec * sr_));
      // Virtual asymptote at -0.005: smooth analog exponential decay reaching exact
      // 0.0 in relSec (ln(1.005 / 0.005) = 5.3033049) with a silent termination slope.
      releaseCoef_ = 1.0 - std::exp(-5.303304908 / (relSec * sr_));
      sustain_ = std::clamp(sustain, 0.0, 1.0);
  }

    void reset() noexcept TF_NONBLOCKING {
        stage_ = Stage::closed;
        value_ = 0.0;
    }

    void open() noexcept TF_NONBLOCKING { stage_ = Stage::attack; }

    void close() noexcept TF_NONBLOCKING {
        if (stage_ != Stage::closed) stage_ = Stage::release;
    }

    void fade() noexcept TF_NONBLOCKING { stage_ = Stage::fade; }

    // Advances one sample. Returns true when a fade has just reached zero.
    bool step() noexcept TF_NONBLOCKING {
        switch (stage_) {
            case Stage::closed:
                return false;
            case Stage::attack:
                value_ += attackCoef_ * (1.6 - value_);
                if (value_ >= 1.0) {
                    value_ = 1.0;
                    stage_ = sustain_ >= 0.9999 ? Stage::sustain : Stage::decay;
                }
                return false;
            case Stage::decay:
                value_ += decayCoef_ * (sustain_ - value_);
                if (std::abs(value_ - sustain_) <= 1.0e-4) {
                    value_ = sustain_;
                    stage_ = Stage::sustain;
                }
                return false;
            case Stage::sustain:
                value_ += decayCoef_ * (sustain_ - value_);
                return false;
            case Stage::release:
                value_ += releaseCoef_ * (-0.005 - value_);
                if (value_ <= 0.0) {
                    value_ = 0.0;
                    stage_ = Stage::closed;
                }
                return false;
            case Stage::fade:
                value_ = std::max(0.0, value_ - fadeStep_);
                if (value_ > 0.0) return false;
                stage_ = Stage::closed;
                return true;
        }
        return false;
    }

    double value() const noexcept TF_NONBLOCKING { return value_; }
    Stage stage() const noexcept TF_NONBLOCKING { return stage_; }
    bool silent() const noexcept TF_NONBLOCKING { return stage_ == Stage::closed && value_ <= 0.0; }

private:
  double sr_ = 48000.0;
  Stage stage_ = Stage::closed;
  double value_ = 0.0;
  double sustain_ = 1.0;
  double attackCoef_ = 0.02;
  double decayCoef_ = 0.001;
  double releaseCoef_ = 0.003;
  double fadeStep_ = 0.02;
};

// Phase-locked exponential pitch transient envelope (0..1 -> +0..24 st).
class PitchPunch {
  public:
    void prepare(double sampleRate) noexcept TF_NONBLOCKING {
        sr_ = std::max(sampleRate, 1000.0);
        reset();
    }

    void reset() noexcept TF_NONBLOCKING { value_ = 0.0; }

    void trigger(double strength = 1.0) noexcept TF_NONBLOCKING { value_ = std::clamp(strength, 0.0, 1.0); }

    double advance(double samples, double decayMs) noexcept TF_NONBLOCKING {
        if (value_ <= 1.0e-5) {
            value_ = 0.0;
            return 0.0;
        }
        const double tau = std::max(0.002, decayMs * 1.0e-3) * sr_;
        // Reaches ~1% of peak in decayMs (exp(-4.6) ~= 0.01).
        value_ *= std::exp(-4.605170186 * samples / tau);
        if (value_ <= 1.0e-5)
            value_ = 0.0;
        return value_;
    }

    double value() const noexcept TF_NONBLOCKING { return value_; }

  private:
    double sr_ = 48000.0;
    double value_ = 0.0;
};
}
