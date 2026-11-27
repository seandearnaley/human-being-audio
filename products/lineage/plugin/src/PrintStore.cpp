// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PrintStore.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ln {

PrintStore::PrintStore(std::function<void(PrintHandle)> onPublish)
    : juce::Thread("Lineage Print Worker"), onPublish_(std::move(onPublish)) {
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
    return current_ ? current_->label : std::string("Not printed");
}

std::array<float, PrintBuffer::kWaveformPoints> PrintStore::peaks() const {
    const juce::ScopedLock sl(lock_);
    return current_ ? current_->peaks : std::array<float, PrintBuffer::kWaveformPoints>{};
}

PrintBuffer PrintStore::copyBuffer() const {
    const auto print = snapshot();
    return print ? *print : PrintBuffer{};
}

PrintHandle PrintStore::snapshot() const {
    const juce::ScopedLock sl(lock_);
    return current_;
}

// Caller holds lock_: the UI, host restore and worker share one publication order.
void PrintStore::setCurrent(PrintHandle print) {
    current_ = std::move(print);
    onPublish_(current_);
    generation_.store(current_ ? current_->generation : 0, std::memory_order_release);
    canStepBack_.store(!history_.empty(), std::memory_order_release);
    printState_.store(current_ ? ui::PrintState::ready : ui::PrintState::idle, std::memory_order_release);
}

void PrintStore::commitBuffer(PrintBuffer buf, std::uint64_t ticket) {
    auto next = std::make_shared<const PrintBuffer>(std::move(buf));
    const juce::ScopedLock sl(lock_);
    if (ticket != ticketCounter_.load())
        return;
    if (current_) {
        if (history_.size() == 4)
            history_.erase(history_.begin());
        history_.push_back(current_);
    }
    setCurrent(std::move(next));
}

void PrintStore::triggerPrint(const Params& params, const LaneCurves& curves, double sampleRate, double bpm,
                              int rootNote, bool stackGeneration) {
    {
        const juce::ScopedLock sl(lock_);
        pendingJob_.params = params;
        pendingJob_.curves = curves;
        pendingJob_.sampleRate = sampleRate > 8000.0 ? sampleRate : 48000.0;
        pendingJob_.bpm = std::clamp(bpm, 40.0, 260.0);
        pendingJob_.rootNote = std::clamp(rootNote, 12, 84);
        pendingJob_.stackGeneration = stackGeneration;
        pendingJob_.ticket = ++ticketCounter_;
        hasPendingJob_ = true;
        printState_.store(ui::PrintState::printing, std::memory_order_release);
    }
    wakeEvent_.signal();
}

void PrintStore::renderSync(const Params& params, const LaneCurves& curves, double sampleRate, double bpm, int rootNote,
                            bool stackGeneration) {
    const auto ticket = ++ticketCounter_;
    auto prev = stackGeneration ? snapshot() : nullptr;
    PrintBuffer buf =
        Engine::renderPrint(params, curves, sampleRate > 8000.0 ? sampleRate : 48000.0, std::clamp(bpm, 40.0, 260.0),
                            std::clamp(rootNote, 12, 84), prev.get(), stackGeneration);
    commitBuffer(std::move(buf), ticket);
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
        if (!std::isfinite(l) || !std::isfinite(r))
            return false;
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

    commitBuffer(std::move(buf), ++ticketCounter_);
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
    const juce::ScopedLock sl(lock_);
    if (history_.empty())
        return false;
    ++ticketCounter_;
    hasPendingJob_ = false;
    auto prev = std::move(history_.back());
    history_.pop_back();
    setCurrent(std::move(prev));
    return true;
}

void PrintStore::clear() { restore({}); }

void PrintStore::restore(PrintHandle print) {
    const juce::ScopedLock sl(lock_);
    ++ticketCounter_;
    hasPendingJob_ = false;
    history_.clear();
    setCurrent(std::move(print));
}

void PrintStore::appendState(juce::ValueTree& session) const {
    const auto print = snapshot();
    if (!print)
        return;
    juce::MemoryOutputStream pcm(static_cast<size_t>(print->length) * 2 * sizeof(float));
    for (size_t i = 0; i < static_cast<size_t>(print->length); ++i) {
        pcm.writeFloat(print->left[i]);
        pcm.writeFloat(print->right[i]);
    }
    juce::ValueTree asset("Print");
    asset.setProperty("version", 1, nullptr);
    asset.setProperty("pcm", pcm.getMemoryBlock(), nullptr);
    asset.setProperty("rate", print->sampleRate, nullptr);
    asset.setProperty("root", print->rootMidiNote, nullptr);
    asset.setProperty("generation", print->generation, nullptr);
    asset.setProperty("label", juce::String(print->label), nullptr);
    session.appendChild(asset, nullptr);
}

bool PrintStore::decodeState(juce::ValueTree& session, PrintHandle& print) {
    const auto asset = session.getChildWithName("Print");
    if (!asset.isValid())
        return true;
    const auto* pcm = asset["pcm"].getBinaryData();
    constexpr size_t frameBytes = 2 * sizeof(float);
    if (static_cast<int>(asset["version"]) != 1 || pcm == nullptr || pcm->getSize() % frameBytes != 0 ||
        pcm->getSize() <= 64 * frameBytes || pcm->getSize() > PrintBuffer::kMaxPrintSamples * frameBytes)
        return false;
    auto next = std::make_shared<PrintBuffer>();
    next->rootMidiNote = asset["root"];
    next->generation = asset["generation"];
    next->sampleRate = asset["rate"];
    if (next->rootMidiNote < 0 || next->rootMidiNote > 127 || next->generation < 2 ||
        next->generation == std::numeric_limits<int>::max() || !std::isfinite(next->sampleRate) ||
        next->sampleRate <= 0)
        return false;
    next->label = asset["label"].toString().toStdString();
    const auto frames = pcm->getSize() / frameBytes;
    next->length = static_cast<int>(frames);
    next->left.resize(frames);
    next->right.resize(frames);
    juce::MemoryInputStream input(*pcm, false);
    for (size_t i = 0; i < frames; ++i) {
        next->left[i] = input.readFloat();
        next->right[i] = input.readFloat();
        if (!std::isfinite(next->left[i]) || !std::isfinite(next->right[i]))
            return false;
    }
    next->computePeaks();
    print = std::move(next);
    session.removeChild(asset, nullptr);
    return true;
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
        wakeEvent_.wait();
        if (threadShouldExit())
            break;

        Job job;
        PrintHandle prev;
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

        PrintBuffer buf = Engine::renderPrint(job.params, job.curves, job.sampleRate, job.bpm, job.rootNote, prev.get(),
                                              job.stackGeneration);

        if (threadShouldExit() || ticketCounter_.load(std::memory_order_acquire) != job.ticket)
            continue;

        commitBuffer(std::move(buf), job.ticket);
    }
}

} // namespace ln
