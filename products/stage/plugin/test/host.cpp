// SPDX-License-Identifier: GPL-3.0-or-later
// A stand-in DAW: runs the real processor at real-time pace with a playing transport and exits
// after N seconds. Prints READY once the processors are prepared and a DONE line at the end.
//   stage-link-host [seconds] [bpm] [blockSize]           one track: drums and a scene clip together
//   stage-link-host [seconds] [bpm] [blockSize] tracks    three tracks: drums, layer 1, layer 2
#include "Plugin.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Transport final : juce::AudioPlayHead {
    double ppq{0.0};
    double bpm{174.0};
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setTimeSignature(TimeSignature{4, 4});
        return info;
    }
};

struct Hit {
    double beat;
    int note;
    float velocity;
};
constexpr Hit pattern[] = {{0.0, 36, 1.0F}, {1.0, 38, 0.9F}, {2.5, 36, 1.0F}, {3.0, 38, 0.9F},
                           {0.0, 42, 0.6F}, {0.5, 42, 0.6F}, {1.0, 42, 0.6F}, {1.5, 42, 0.6F},
                           {2.0, 42, 0.6F}, {2.5, 42, 0.6F}, {3.0, 42, 0.6F}, {3.5, 42, 0.6F}};

// Scene clips in absolute beats. Each first note starts before the Stage can connect, or overlaps
// the next, so the Stage must learn of held notes and cut between scenes.
struct Span {
    double on;
    double off;
    int note;
};
constexpr Span single[] = {{0.0, 14.0, 60}, {12.0, 20.0, 62}, {20.0, 28.0, 64}};
constexpr Span back[] = {{0.0, 14.0, 60}, {12.0, 24.0, 62}};
constexpr Span front[] = {{6.0, 10.0, 60}, {16.0, 22.0, 62}, {20.0, 30.0, 60}};

struct Track {
    Track(int role, bool playsDrums, std::span<const Span> scenes) : drums(playsDrums), clip(scenes) {
        processor.setRole(role);
    }
    stage::Processor processor;
    bool drums;
    // With sound: the drums are also heard at this track's input. Samples since the last kick and snare.
    bool sound{false};
    std::int64_t sinceKick{1 << 30};
    std::int64_t sinceSnare{1 << 30};
    unsigned noise{1};
    std::span<const Span> clip;
    juce::MidiBuffer midi;
};

} // namespace

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::atof(argv[1]) : 20.0;
    const double bpm = argc > 2 ? std::atof(argv[2]) : 174.0;
    const int blockSize = argc > 3 ? std::atoi(argv[3]) : 256;
    const bool manyTracks = argc > 4 && std::string_view(argv[4]) == "tracks";
    const double sampleRate = 48000.0;

    const juce::ScopedJuceInitialiser_GUI juce;
    std::vector<std::unique_ptr<Track>> tracks;
    if (manyTracks) {
        tracks.push_back(std::make_unique<Track>(0, true, std::span<const Span>{}));
        tracks.push_back(std::make_unique<Track>(1, false, back));
        tracks.push_back(std::make_unique<Track>(2, false, front));
        // Each track has its own knobs: give layer 2 a first knob the Stage can tell apart.
        tracks[2]->processor.getParameters()[0]->setValueNotifyingHost(0.9F);
        tracks[0]->sound = true;
    } else {
        tracks.push_back(std::make_unique<Track>(1, true, single));
    }

    Transport transport;
    transport.bpm = bpm;
    for (auto& track : tracks) {
        track->processor.setPlayHead(&transport);
        track->processor.setPlayConfigDetails(track->sound ? 2 : 0, 2, sampleRate, blockSize);
        track->processor.prepareToPlay(sampleRate, blockSize);
    }

    std::printf("READY\n");
    std::fflush(stdout);

    juce::AudioBuffer<float> buffer(2, blockSize);
    const double beatsPerSample = bpm / (60.0 * sampleRate);
    const auto blocks = static_cast<std::int64_t>(seconds * sampleRate / blockSize);
    const auto start = std::chrono::steady_clock::now();

    for (std::int64_t block = 0; block < blocks; ++block) {
        const double from = static_cast<double>(block * blockSize) * beatsPerSample;
        const double to = from + blockSize * beatsPerSample;
        const auto offset = [&](double at) {
            return juce::jlimit(0, blockSize - 1, static_cast<int>((at - from) / beatsPerSample));
        };
        transport.ppq = from;
        for (auto& track : tracks) {
            auto& midi = track->midi;
            midi.clear();
            int kickAt = -1;
            int snareAt = -1;
            for (const auto& hit : pattern) {
                if (!track->drums)
                    break;
                // The next occurrence of this hit at or after `from`.
                const double at = hit.beat + std::ceil((from - hit.beat) / 4.0) * 4.0;
                if (at < to) {
                    if (hit.note == 36)
                        kickAt = offset(at);
                    if (hit.note == 38)
                        snareAt = offset(at);
                    midi.addEvent(juce::MidiMessage::noteOn(1, hit.note, hit.velocity), offset(at));
                    midi.addEvent(juce::MidiMessage::noteOff(1, hit.note), juce::jmin(blockSize - 1, offset(at) + 1));
                }
            }
            for (const auto& span : track->clip) {
                if (span.off >= from && span.off < to)
                    midi.addEvent(juce::MidiMessage::noteOff(1, span.note), offset(span.off));
                if (span.on >= from && span.on < to)
                    midi.addEvent(juce::MidiMessage::noteOn(1, span.note, 1.0F), offset(span.on));
            }
            buffer.clear();
            // The sound of the drums, sample-exact with their notes: each kick is a one-sample click
            // and a 55 Hz thump, each snare a burst of noise.
            for (int i = 0; track->sound && i < blockSize; ++i) {
                if (i == kickAt)
                    track->sinceKick = 0;
                if (i == snareAt)
                    track->sinceSnare = 0;
                const double k = static_cast<double>(track->sinceKick++) / sampleRate;
                const double s = static_cast<double>(track->sinceSnare++) / sampleRate;
                track->noise = track->noise * 1664525U + 1013904223U;
                const double hiss = static_cast<double>(track->noise) / 2147483648.0 - 1.0;
                const double value = (k == 0.0 ? 0.95
                                               : 0.6 * std::exp(-k * 18.0) *
                                                     std::sin(2.0 * juce::MathConstants<double>::pi * 55.0 * k)) +
                                     0.25 * std::exp(-s * 30.0) * hiss;
                buffer.setSample(0, i, static_cast<float>(value));
                buffer.setSample(1, i, static_cast<float>(value));
            }
            track->processor.processBlock(buffer, midi);
        }
        std::this_thread::sleep_until(
            start + std::chrono::duration<double>(static_cast<double>((block + 1) * blockSize) / sampleRate));
    }

    unsigned long long dropped = 0;
    int onHub = 0;
    for (const auto& track : tracks) {
        dropped += track->processor.dropped();
        onHub += track->processor.hubConnected() ? 1 : 0;
    }
    std::printf("DONE dropped=%llu onHub=%d\n", dropped, onHub);
    return 0;
}
