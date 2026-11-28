// SPDX-License-Identifier: GPL-3.0-or-later
// Shared real-time MIDI expression parser: RPN 00 00 (14-bit pitch bend range),
// symmetric pitch-wheel normalization (+8191 / -8192), CC1, CC64, CC74, CC121,
// Channel Pressure, Poly Aftertouch, and All Notes/Sound Off.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

namespace hba::host {

class MidiExpressionParser {
  public:
    explicit MidiExpressionParser(float defaultBendRangeSt = 2.0f) noexcept
        : pitchBendRangeSt_(std::clamp(defaultBendRangeSt, 1.0f, 48.0f)) {}

    float pitchBendRangeSt() const noexcept { return pitchBendRangeSt_; }
    void setPitchBendRange(float semitones) noexcept { pitchBendRangeSt_ = std::clamp(semitones, 1.0f, 48.0f); }

    void reset(float defaultBendRangeSt = 2.0f) noexcept {
        rpnDetector_.reset();
        pitchBendRangeSt_ = std::clamp(defaultBendRangeSt, 1.0f, 48.0f);
    }

    static float normalisePitchWheel(int raw14Bit) noexcept;

    // Dispatches a 1..3-byte MIDI message to `sink` (zero heap allocation).
    // `Sink` implements:
    //   onNoteOn(int note, float velocity)
    //   onNoteOff(int note)
    //   onPitchBend(float semitones)
    //   onPitchBendRange(float semitones)
    //   onModWheel(float norm01)
    //   onSustain(bool down)
    //   onTimbre(float norm01)
    //   onChannelPressure(float norm01)
    //   onPolyAftertouch(int note, float norm01)
    //   onResetControllers()
    //   onAllNotesOff()
    template <typename Sink> void process(const juce::MidiMessage& message, Sink&& sink) noexcept {
        if (message.isNoteOn()) {
            sink.onNoteOn(message.getNoteNumber(), message.getFloatVelocity());
        } else if (message.isNoteOff()) {
            sink.onNoteOff(message.getNoteNumber());
        } else if (message.isPitchWheel()) {
            sink.onPitchBend(pitchBendRangeSt_ * normalisePitchWheel(message.getPitchWheelValue()));
        } else if (message.isController()) {
            const int ch = message.getChannel();
            const int cc = message.getControllerNumber();
            const int val = message.getControllerValue();
            if (ch >= 1 && ch <= 16) {
                if (const auto rpn = rpnDetector_.tryParse(ch, cc, val)) {
                    if (!rpn->isNRPN && rpn->parameterNumber == 0) {
                        const float semitones = rpn->is14BitValue ? static_cast<float>(rpn->value >> 7) +
                                                                        static_cast<float>(rpn->value & 0x7f) / 100.0f
                                                                  : static_cast<float>(rpn->value);
                        pitchBendRangeSt_ = std::clamp(semitones, 1.0f, 48.0f);
                        sink.onPitchBendRange(pitchBendRangeSt_);
                    }
                }
            }
            if (cc == 1) {
                sink.onModWheel(static_cast<float>(val) / 127.0f);
            } else if (cc == 64) {
                sink.onSustain(val >= 64);
            } else if (cc == 74) {
                sink.onTimbre(val == 64 ? 0.5f : static_cast<float>(val) / 127.0f);
            } else if (cc == 121) {
                sink.onResetControllers();
            } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                sink.onAllNotesOff();
            }
        } else if (message.isChannelPressure()) {
            sink.onChannelPressure(static_cast<float>(message.getChannelPressureValue()) / 127.0f);
        } else if (message.isAftertouch()) {
            sink.onPolyAftertouch(message.getNoteNumber(), static_cast<float>(message.getAfterTouchValue()) / 127.0f);
        }
    }

  private:
    juce::MidiRPNDetector rpnDetector_;
    float pitchBendRangeSt_ = 2.0f;
};

} // namespace hba::host
