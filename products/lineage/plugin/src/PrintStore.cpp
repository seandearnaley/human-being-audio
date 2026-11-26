// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PrintStore.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ln {

namespace {
const juce::Identifier kPrintGenProp{"LineagePrintGen"};
const juce::Identifier kPrintRootProp{"LineagePrintRoot"};
const juce::Identifier kPrintBpmProp{"LineagePrintBpm"};
const juce::Identifier kPrintFileProp{"LineagePrintFile"};
} // namespace

PrintStore::PrintStore(juce::AudioProcessorValueTreeState& state, std::function<void(const PrintBuffer&)> onPublish,
                       std::function<void()> onClear)
    : juce::Thread("Lineage Print Worker"), state_(state), onPublish_(std::move(onPublish)),
      onClear_(std::move(onClear)) {
    startThread();
}

PrintStore::~PrintStore() {
    *alive_ = false;
    signalThreadShouldExit();
    wakeEvent_.signal();
    stopThread(5000);
}

std::string PrintStore::label() const {
    const juce::ScopedLock sl(lock_);
    return current_.valid() ? current_.label : std::string("Not printed");
}

std::array<float, PrintBuffer::kWaveformPoints> PrintStore::peaks() const {
    const juce::ScopedLock sl(lock_);
    return current_.peaks;
}

PrintBuffer PrintStore::copyBuffer() const {
    const juce::ScopedLock sl(lock_);
    return current_;
}

void PrintStore::commitBuffer(PrintBuffer buf, const juce::String& externalFilePath, bool recordHistory) {
    if (!buf.valid())
        return;
    bool hasHist = false;
    {
        const juce::ScopedLock sl(lock_);
        if (recordHistory && current_.valid()) {
            if (history_.size() >= 4u)
                history_.erase(history_.begin());
            history_.push_back(current_);
        }
        current_ = buf;
        hasHist = !history_.empty();
    }
    generation_.store(buf.generation, std::memory_order_release);
    canStepBack_.store(hasHist, std::memory_order_release);
    printState_.store(ui::PrintState::ready, std::memory_order_release);
    if (onPublish_)
        onPublish_(buf);

    // Record lightweight recall metadata on ValueTree (avoiding thread safety issues on message thread)
    const int gen = buf.generation;
    const int root = buf.rootMidiNote;
    const auto alive = alive_;
    auto updateTree = [this, alive, gen, root, externalFilePath] {
        if (!*alive)
            return;
        state_.state.setProperty(kPrintGenProp, gen, nullptr);
        state_.state.setProperty(kPrintRootProp, root, nullptr);
        if (externalFilePath.isNotEmpty())
            state_.state.setProperty(kPrintFileProp, externalFilePath, nullptr);
        else
            state_.state.removeProperty(kPrintFileProp, nullptr);
    };
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating()) {
        if (mm->isThisTheMessageThread())
            updateTree();
        else
            juce::MessageManager::callAsync(std::move(updateTree));
    }
}

void PrintStore::triggerPrint(const Params& params, const LaneCurves& curves, double sampleRate, double bpm,
                              int rootNote, bool stackGeneration) {
    const std::uint64_t ticket = ticketCounter_.fetch_add(1, std::memory_order_acq_rel) + 1u;
    {
        const juce::ScopedLock sl(lock_);
        pendingJob_.params = params;
        pendingJob_.curves = curves;
        pendingJob_.sampleRate = sampleRate > 8000.0 ? sampleRate : 48000.0;
        pendingJob_.bpm = std::clamp(bpm, 40.0, 260.0);
        pendingJob_.rootNote = std::clamp(rootNote, 12, 84);
        pendingJob_.stackGeneration = stackGeneration;
        pendingJob_.ticket = ticket;
        hasPendingJob_ = true;
    }
    printState_.store(ui::PrintState::printing, std::memory_order_release);
    wakeEvent_.signal();
}

void PrintStore::renderSync(const Params& params, const LaneCurves& curves, double sampleRate, double bpm, int rootNote,
                            bool stackGeneration) {
    ticketCounter_.fetch_add(1, std::memory_order_acq_rel);
    PrintBuffer prev;
    if (stackGeneration) {
        const juce::ScopedLock sl(lock_);
        prev = current_;
    }
    PrintBuffer buf = Engine::renderPrint(params, curves, sampleRate > 8000.0 ? sampleRate : 48000.0,
                                          std::clamp(bpm, 40.0, 260.0), std::clamp(rootNote, 12, 84),
                                          (stackGeneration && prev.valid()) ? &prev : nullptr, stackGeneration);
    commitBuffer(std::move(buf));
}

bool PrintStore::loadAudioFile(const juce::File& file, double targetSampleRate) {
    if (!file.existsAsFile())
        return false;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 64)
        return false;

    const double srcRate = reader->sampleRate > 8000.0 ? reader->sampleRate : 48000.0;
    const double dstRate = targetSampleRate > 8000.0 ? targetSampleRate : 48000.0;
    const double ratio = srcRate / dstRate;

    const int maxSrcSamples = static_cast<int>(std::min<juce::int64>(
        reader->lengthInSamples, static_cast<juce::int64>(std::ceil(PrintBuffer::kMaxPrintSamples * ratio))));
    const int srcCh = std::clamp(static_cast<int>(reader->numChannels), 1, 2);

    juce::AudioBuffer<float> tmp(srcCh, maxSrcSamples);
    if (!reader->read(&tmp, 0, maxSrcSamples, 0, true, srcCh > 1))
        return false;

    const int dstLen = std::clamp(static_cast<int>(std::floor(static_cast<double>(maxSrcSamples) / ratio)), 65,
                                  PrintBuffer::kMaxPrintSamples);

    PrintBuffer buf;
    buf.left.resize(static_cast<std::size_t>(dstLen));
    buf.right.resize(static_cast<std::size_t>(dstLen));
    buf.length = dstLen;
    buf.sampleRate = dstRate;
    buf.rootMidiNote = 29; // F1 default root for chromatically pitched audio
    buf.generation = 2;

    const float* inL = tmp.getReadPointer(0);
    const float* inR = srcCh > 1 ? tmp.getReadPointer(1) : inL;
    float peak = 0.0f;

    for (int i = 0; i < dstLen; ++i) {
        const double sp = static_cast<double>(i) * ratio;
        const int i0 = std::clamp(static_cast<int>(sp), 0, maxSrcSamples - 1);
        const int i1 = std::min(i0 + 1, maxSrcSamples - 1);
        const float frac = static_cast<float>(sp - static_cast<double>(i0));
        const float l = inL[i0] + (inL[i1] - inL[i0]) * frac;
        const float r = inR[i0] + (inR[i1] - inR[i0]) * frac;
        buf.left[static_cast<std::size_t>(i)] = l;
        buf.right[static_cast<std::size_t>(i)] = r;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }

    if (peak > 1e-5f) {
        const float norm = 0.84f / peak; // -1.5 dBFS headroom
        for (int i = 0; i < dstLen; ++i) {
            buf.left[static_cast<std::size_t>(i)] *= norm;
            buf.right[static_cast<std::size_t>(i)] *= norm;
        }
    }

    // Gentle 2ms boundary fade for click-free loops
    const int fade = std::min(96, dstLen / 16);
    for (int i = 0; i < fade; ++i) {
        const float w = static_cast<float>(i) / static_cast<float>(std::max(1, fade));
        buf.left[static_cast<std::size_t>(i)] *= w;
        buf.right[static_cast<std::size_t>(i)] *= w;
        const auto tail = static_cast<std::size_t>(dstLen - 1 - i);
        buf.left[tail] *= w;
        buf.right[tail] *= w;
    }

    std::string stem = file.getFileNameWithoutExtension().toStdString();
    if (stem.size() > 16)
        stem = stem.substr(0, 15) + "\xe2\x80\xa6";
    buf.label = "EXT \xc2\xb7 " + stem;
    buf.computePeaks();

    ticketCounter_.fetch_add(1, std::memory_order_acq_rel);
    commitBuffer(std::move(buf), file.getFullPathName());
    return true;
}

void PrintStore::browseLoadWavAsync(double targetSampleRate) {
    chooser_ = std::make_unique<juce::FileChooser>("Load Audio into Lineage Strata",
                                                   juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    const auto alive = alive_;
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, alive, targetSampleRate](const juce::FileChooser& fc) {
                              if (!*alive)
                                  return;
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  loadAudioFile(file, targetSampleRate);
                          });
}

bool PrintStore::stepBack() {
    PrintBuffer prev;
    bool more = false;
    {
        const juce::ScopedLock sl(lock_);
        if (history_.empty())
            return false;
        prev = std::move(history_.back());
        history_.pop_back();
        more = !history_.empty();
    }
    ticketCounter_.fetch_add(1, std::memory_order_acq_rel);
    commitBuffer(std::move(prev), {}, false);
    canStepBack_.store(more, std::memory_order_release);
    return true;
}

void PrintStore::clear() {
    ticketCounter_.fetch_add(1, std::memory_order_acq_rel);
    {
        const juce::ScopedLock sl(lock_);
        current_ = PrintBuffer{};
        history_.clear();
        hasPendingJob_ = false;
    }
    generation_.store(0, std::memory_order_release);
    canStepBack_.store(false, std::memory_order_release);
    printState_.store(ui::PrintState::idle, std::memory_order_release);
    if (onClear_)
        onClear_();
    state_.state.removeProperty(kPrintGenProp, nullptr);
    state_.state.removeProperty(kPrintFileProp, nullptr);
}

void PrintStore::reloadFromState(const Params& params, const LaneCurves& curves, double sampleRate) {
    if (state_.state.hasProperty(kPrintFileProp)) {
        const juce::File f(state_.state.getProperty(kPrintFileProp).toString());
        if (f.existsAsFile() && loadAudioFile(f, sampleRate))
            return;
    }
    const int savedGen = static_cast<int>(state_.state.getProperty(kPrintGenProp, 0));
    if (savedGen >= 2) {
        const int savedRoot = static_cast<int>(state_.state.getProperty(kPrintRootProp, 29));
        const double savedBpm = static_cast<double>(state_.state.getProperty(kPrintBpmProp, 172.0));
        triggerPrint(params, curves, sampleRate, savedBpm, savedRoot, false);
    } else {
        clear();
    }
}

bool PrintStore::waitForIdle(int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(std::max(1, timeoutMs));
    while (printState_.load(std::memory_order_acquire) == ui::PrintState::printing) {
        if (juce::Time::getMillisecondCounter() >= deadline)
            return false;
        juce::Thread::sleep(5);
    }
    return true;
}

void PrintStore::run() {
    while (!threadShouldExit()) {
        wakeEvent_.wait(200);
        if (threadShouldExit())
            break;

        Job job;
        PrintBuffer prev;
        {
            const juce::ScopedLock sl(lock_);
            if (!hasPendingJob_)
                continue;
            job = pendingJob_;
            hasPendingJob_ = false;
            if (job.stackGeneration)
                prev = current_;
        }

        if (ticketCounter_.load(std::memory_order_acquire) != job.ticket)
            continue;

        PrintBuffer buf =
            Engine::renderPrint(job.params, job.curves, job.sampleRate, job.bpm, job.rootNote,
                                (job.stackGeneration && prev.valid()) ? &prev : nullptr, job.stackGeneration);

        if (threadShouldExit() || ticketCounter_.load(std::memory_order_acquire) != job.ticket)
            continue;

        commitBuffer(std::move(buf));
    }
}

} // namespace ln
