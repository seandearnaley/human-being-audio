#include "Resampler.h"

#include "analysis/analysis.h"

namespace tf {

namespace {
constexpr int kOversampling = 16;
constexpr double kBars = 4.0;

juce::String noteName(int note) {
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}
}

juce::File Resampler::directory() {
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Tract Form").getChildFile("Resamples");
}

Resampler::Resampler() : juce::Thread("Tract Form resample") {}

Resampler::~Resampler() { stopThread(10000); }

void Resampler::start(const RenderRequest& request, const juce::String& presetName) {
    if (isThreadRunning()) return;

    const auto name = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Tract Form") : presetName)
                    + " " + noteName(request.note) + " " + juce::String(juce::roundToInt(request.bpm)) + "bpm "
                    + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav";
    {
        const juce::ScopedLock scoped(lock_);
        request_ = request;
        request_.oversampling = kOversampling;
        request_.bars = kBars;
        target_ = directory().getChildFile(name);
    }
    state_.store(ui::ResampleState::rendering);
    startThread();
}

juce::File Resampler::file() const {
    if (state_.load() != ui::ResampleState::ready) return {};
    const juce::ScopedLock scoped(lock_);
    return target_;
}

void Resampler::run() {
    RenderRequest request;
    juce::File target;
    {
        const juce::ScopedLock scoped(lock_);
        request = request_;
        target = target_;
    }

    const auto audio = renderOffline(request);
    const bool written = target.getParentDirectory().createDirectory()
                      && analysis::writeWavFloat(target.getFullPathName().toStdString(), audio, 2,
                                                 juce::roundToInt(request.sampleRate));
    state_.store(written ? ui::ResampleState::ready : ui::ResampleState::idle);
}

}
