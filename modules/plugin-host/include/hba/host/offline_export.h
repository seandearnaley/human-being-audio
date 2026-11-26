// SPDX-License-Identifier: GPL-3.0-or-later
// Shared background offline WAV render worker (generation-stamped) & temp MIDI drag writer.
#pragma once

#include <hba/core/wav.h>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace hba::host {

// Writes a temporary .mid file under <temp>/HumanBeingAudio/<productSubdir>/ and prunes older files.
inline juce::File writeTempMidiFile(const juce::String& productSubdir, const juce::String& filename,
                                    std::span<const std::uint8_t> bytes, int maxKeep = 8) {
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("HumanBeingAudio")
                         .getChildFile(productSubdir);
    dir.createDirectory();

    const auto file = dir.getChildFile(filename);
    file.replaceWithData(bytes.data(), bytes.size());

    if (maxKeep > 0) {
        auto existing = dir.findChildFiles(juce::File::findFiles, false, "*.mid");
        if (existing.size() > maxKeep) {
            std::sort(existing.begin(), existing.end(), [](const juce::File& a, const juce::File& b) {
                return a.getLastModificationTime() > b.getLastModificationTime();
            });
            for (int i = maxKeep; i < existing.size(); ++i)
                existing[i].deleteFile();
        }
    }
    return file;
}

template <typename RequestT, typename StateT> class OfflineWavWorker final : private juce::Thread {
  public:
    using RenderFn = std::function<std::vector<float>(const RequestT&)>;

    OfflineWavWorker(const juce::String& threadName, RenderFn renderFn)
        : juce::Thread(threadName), renderFn_(std::move(renderFn)) {}

    ~OfflineWavWorker() override { stopThread(10000); }

    StateT state() const noexcept { return state_.load(std::memory_order_acquire); }

    bool start(const RequestT& request, const juce::File& target, int sampleRate) {
        if (isThreadRunning())
            return false;
        {
            const juce::ScopedLock scoped(lock_);
            request_ = request;
            target_ = target;
            sampleRate_ = sampleRate;
        }
        const std::uint64_t gen = generation_.fetch_add(1, std::memory_order_acq_rel) + 1u;
        activeGen_.store(gen, std::memory_order_release);
        state_.store(StateT::rendering, std::memory_order_release);
        startThread();
        return true;
    }

    void invalidate() noexcept {
        generation_.fetch_add(1, std::memory_order_acq_rel);
        StateT expected = StateT::ready;
        state_.compare_exchange_strong(expected, StateT::idle, std::memory_order_acq_rel);
    }

    juce::File file() const {
        if (state_.load(std::memory_order_acquire) != StateT::ready)
            return {};
        const juce::ScopedLock scoped(lock_);
        return target_;
    }

  private:
    void run() override {
        const std::uint64_t myGen = activeGen_.load(std::memory_order_acquire);
        RequestT req;
        juce::File target;
        int sampleRate = 48000;
        {
            const juce::ScopedLock scoped(lock_);
            req = request_;
            target = target_;
            sampleRate = sampleRate_;
        }
        const auto audio = renderFn_(req);
        if (generation_.load(std::memory_order_acquire) != myGen) {
            state_.store(StateT::idle, std::memory_order_release);
            return;
        }
        const bool ok = target.getParentDirectory().createDirectory() &&
                        hba::core::writeWavFloat(target.getFullPathName().toStdString(), audio, 2, sampleRate);
        const bool stillCurrent = generation_.load(std::memory_order_acquire) == myGen;
        state_.store((ok && stillCurrent) ? StateT::ready : StateT::idle, std::memory_order_release);
    }

    RenderFn renderFn_;
    mutable juce::CriticalSection lock_;
    RequestT request_{};
    juce::File target_{};
    int sampleRate_ = 48000;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> activeGen_{0};
    std::atomic<StateT> state_{StateT::idle};
};

} // namespace hba::host
