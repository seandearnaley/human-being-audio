// Monophonic voice control: the note stack (last-note priority), glide and the
// amp gate.
#pragma once

#include "tf/nonblocking.h"

#include <algorithm>
#include <array>
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

// Linear amp gate: 2 ms attack, 30 ms release, and a 1 ms fade used before a
// phase reset while the gate is still open.
class Gate {
public:
    enum class Stage : std::uint8_t { closed, attack, open, release, fade };

    static constexpr double kAttackSeconds = 0.002;
    static constexpr double kReleaseSeconds = 0.030;
    static constexpr double kFadeSeconds = 0.001;

    void prepare(double sampleRate) noexcept TF_NONBLOCKING {
        attackStep_ = 1.0 / (kAttackSeconds * sampleRate);
        releaseStep_ = 1.0 / (kReleaseSeconds * sampleRate);
        fadeStep_ = 1.0 / (kFadeSeconds * sampleRate);
    }
    void reset() noexcept TF_NONBLOCKING {
        stage_ = Stage::closed;
        value_ = 0.0;
    }
    void open() noexcept TF_NONBLOCKING { stage_ = value_ >= 1.0 ? Stage::open : Stage::attack; }
    void close() noexcept TF_NONBLOCKING {
        if (stage_ != Stage::closed) stage_ = Stage::release;
    }
    void fade() noexcept TF_NONBLOCKING { stage_ = Stage::fade; }

    // Advances one sample. Returns true when a fade has just reached zero.
    bool step() noexcept TF_NONBLOCKING {
        switch (stage_) {
            case Stage::closed:
            case Stage::open: return false;
            case Stage::attack:
                value_ = std::min(1.0, value_ + attackStep_);
                if (value_ >= 1.0) stage_ = Stage::open;
                return false;
            case Stage::release:
                value_ = std::max(0.0, value_ - releaseStep_);
                if (value_ <= 0.0) stage_ = Stage::closed;
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
    Stage stage_ = Stage::closed;
    double value_ = 0.0;
    double attackStep_ = 0.01, releaseStep_ = 0.001, fadeStep_ = 0.02;
};

}
