// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PhraseCapture.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace livecut
{
// Capture and Save run on the worker. Dragging writes synchronously on the UI thread.
class CaptureService
{
public:
    ~CaptureService();
    PhraseCapture& rolling() { return recorder; }
    const PhraseCapture& rolling() const { return recorder; }
    bool requestCapture();
    bool requestSave (const juce::File& target);
    // Each drag publishes a new stereo float WAV. Hosts may retain its path in saved sessions.
    juce::Result prepareDragWav(const juce::String& label, const juce::File& directory, juce::File& output);
    bool hasPhraseReady() const { return recorder.hasLatest() || captured() != nullptr; }
    bool isBusy() const { return working.load (std::memory_order_acquire); }
    std::shared_ptr<const CapturedPhrase> captured() const;
    juce::String statusText() const;

    // Also used by tests/importers; never call from the audio callback.
    static juce::Result writeWav (const CapturedPhrase&, const juce::File& target);

private:
    enum class Operation { idle, capturing, captured, saving, saved, empty, busy, error };
    void finish (Operation, const juce::String& error = {});

    PhraseCapture recorder;
    mutable std::mutex snapshotMutex;
    std::shared_ptr<const CapturedPhrase> snapshot;
    juce::String errorMessage;
    std::atomic<bool> working { false };
    std::atomic<Operation> operation { Operation::idle };
    juce::ThreadPool worker { 1 };
};
} // namespace livecut
