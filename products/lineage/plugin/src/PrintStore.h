// SPDX-License-Identifier: AGPL-3.0-or-later
// Background non-destructive Gen-2 / Gen-N resampling baker & external WAV loader.
#pragma once

#include "ln/engine.h"
#include "ln/ui/model.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ln {

class PrintStore final : private juce::Thread {
  public:
    explicit PrintStore(std::function<void(PrintHandle)> onPublish);
    ~PrintStore() override;

    ui::PrintState state() const noexcept { return printState_.load(std::memory_order_acquire); }
    int generation() const noexcept { return generation_.load(std::memory_order_acquire); }
    std::string label() const;
    std::array<float, PrintBuffer::kWaveformPoints> peaks() const;
    PrintBuffer copyBuffer() const;
    bool hasPrint() const noexcept { return state() == ui::PrintState::ready && generation() > 0; }

    void triggerPrint(const Params& params, const LaneCurves& curves, double sampleRate, double bpm, int rootNote,
                      bool stackGeneration);
    void renderSync(const Params& params, const LaneCurves& curves, double sampleRate, double bpm, int rootNote,
                    bool stackGeneration);

    bool loadAudioFile(const juce::File& file, double targetSampleRate);
    void browseLoadWavAsync(double targetSampleRate);
    bool canStepBack() const noexcept { return canStepBack_.load(std::memory_order_acquire); }
    bool stepBack();
    void clear();

    struct Snapshot {
        PrintHandle audio;
        juce::ValueTree legacy;
        bool operator==(const Snapshot&) const = default;
    };
    Snapshot snapshot() const;
    void restore(Snapshot print);
    void restoreSession(Snapshot print, const Params& params, const LaneCurves& curves, double sampleRate);
    void appendState(juce::ValueTree& session) const;
    static bool decodeState(juce::ValueTree& session, Snapshot& print);
    bool waitForIdle(int timeoutMs = 5000);

  private:
    struct Job {
        Params params = Params::defaults();
        LaneCurves curves = defaultLaneCurves();
        double sampleRate = 48000.0;
        double bpm = 172.0;
        int rootNote = 29;
        bool stackGeneration = false;
        std::uint64_t ticket = 0;
    };

    void run() override;
    void commitBuffer(PrintBuffer buf, std::uint64_t ticket);
    void setCurrent(Snapshot print);
    std::function<void(PrintHandle)> onPublish_;
    mutable juce::CriticalSection lock_;
    Snapshot current_;
    std::vector<Snapshot> history_;
    Job pendingJob_;
    bool hasPendingJob_ = false;
    juce::WaitableEvent wakeEvent_;

    std::atomic<std::uint64_t> ticketCounter_{0};
    std::atomic<ui::PrintState> printState_{ui::PrintState::idle};
    std::atomic<int> generation_{0};
    std::atomic<bool> canStepBack_{false};

    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace ln
