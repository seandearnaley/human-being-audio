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

inline juce::File uniqueExportFile(const juce::File& target) {
    return target.getSiblingFile(target.getFileNameWithoutExtension() + " " + juce::Uuid().toString() +
                                 target.getFileExtension());
}

// Writes a temporary .mid file under <temp>/HumanBeingAudio/<productSubdir>/ and prunes older files.
inline juce::File writeTempMidiFile(const juce::String& productSubdir, const juce::String& filename,
                                    std::span<const std::uint8_t> bytes, int maxKeep = 8) {
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("HumanBeingAudio")
                         .getChildFile(productSubdir);
    if (bytes.empty() || dir.createDirectory().failed())
        return {};
    const auto file = uniqueExportFile(dir.getChildFile(filename));
    if (!file.replaceWithData(bytes.data(), bytes.size()))
        return {};

    if (maxKeep > 0) {
        auto existing = dir.findChildFiles(juce::File::findFiles, false, "*.mid");
        if (existing.size() > maxKeep) {
            std::sort(existing.begin(), existing.end(), [](const juce::File& a, const juce::File& b) {
                return a.getLastModificationTime() > b.getLastModificationTime();
            });
            for (int i = maxKeep; i < existing.size(); ++i)
                if (existing[i].getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days(1))
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

    ~OfflineWavWorker() override { stopThread(-1); }

    StateT state() const noexcept {
        const auto result = state_.load(std::memory_order_acquire);
        if (result == StateT::rendering)
            return result;
        return activeGen_.load(std::memory_order_acquire) == generation_.load(std::memory_order_acquire) ? result
                                                                                                         : StateT::idle;
    }

    bool start(const RequestT& request, const juce::File& target, int sampleRate) {
        const juce::ScopedLock guard(lock_);
        if (isThreadRunning())
            return false;
        request_ = request;
        target_ = uniqueExportFile(target);
        sampleRate_ = sampleRate;
        const std::uint64_t gen = generation_.fetch_add(1, std::memory_order_acq_rel) + 1u;
        activeGen_.store(gen, std::memory_order_release);
        state_.store(StateT::rendering, std::memory_order_release);
        if (startThread())
            return true;
        state_.store(StateT::failed, std::memory_order_release);
        return false;
    }

    void invalidate() noexcept { generation_.fetch_add(1, std::memory_order_acq_rel); }

    juce::File file() const {
        const juce::ScopedLock scoped(lock_);
        return state() == StateT::ready ? target_ : juce::File{};
    }

  private:
    void run() override {
        try {
            renderToFile();
        } catch (...) {
            // JUCE thread entry points must contain exceptions from the render callback and file I/O.
            state_.store(StateT::failed, std::memory_order_release);
        }
    }

    void renderToFile() {
        const std::uint64_t myGen = activeGen_.load(std::memory_order_acquire);
        RequestT req;
        juce::File target;
        int sampleRate = 48000;
        {
            const juce::ScopedLock scoped(lock_);
            req = std::move(request_);
            target = target_;
            sampleRate = sampleRate_;
        }
        const auto audio = renderFn_(req);
        if (threadShouldExit() || generation_.load(std::memory_order_acquire) != myGen) {
            state_.store(StateT::idle, std::memory_order_release);
            return;
        }
        juce::TemporaryFile temporary(target);
        const bool ok =
            !audio.empty() && target.getParentDirectory().createDirectory().wasOk() &&
            hba::core::writeWavFloat(temporary.getFile().getFullPathName().toStdString(), audio, 2, sampleRate) &&
            temporary.overwriteTargetFileWithTemporary();
        state_.store(ok ? StateT::ready : StateT::failed, std::memory_order_release);
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
