// SPDX-License-Identifier: GPL-3.0-or-later
#include "Plugin.h"

#include <chrono>
#include <cmath>

namespace stage {
namespace {

std::int64_t nowMicros() {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

float rms(const juce::AudioBuffer<float>& buffer, int channels) {
    double sum = 0.0;
    for (int channel = 0; channel < channels; ++channel) {
        const auto* samples = buffer.getReadPointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    const auto count = static_cast<double>(juce::jmax(1, channels * buffer.getNumSamples()));
    return static_cast<float>(std::sqrt(sum / count));
}

// The layers taken by instances in this process, one bit each. A new instance takes the lowest free
// one, so the second track you put Stage Link on is layer 2 without a click.
std::atomic<unsigned> layersTaken{0};

int claimLayer() {
    auto taken = layersTaken.load();
    for (;;) {
        int layer = 1;
        while (layer < 4 && (taken & (1U << layer)) != 0)
            ++layer;
        if (layersTaken.compare_exchange_weak(taken, taken | (1U << layer)))
            return layer;
    }
}

// Small and plain: what this track plays (Stage Link only), a way to bring the Stage app up, and
// whether this instance has reached it.
class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
  public:
    explicit Editor(Processor& processor) : AudioProcessorEditor(processor), processor_(processor) {
        addAndMakeVisible(status_);
#if JUCE_MAC
        open_.setButtonText("Open Stage");
        open_.onClick = [this] { opener_.start(juce::StringArray{"/usr/bin/open", "-a", "Stage"}); };
        addAndMakeVisible(open_);
#endif
        if (JucePlugin_IsSynth) { // Stage Sound has one job and nothing to choose
            static constexpr const char* names[] = {"Drums", "Layer 1", "Layer 2", "Layer 3", "Layer 4"};
            for (int i = 0; i < roleCount; ++i) {
                auto& button = roles_[static_cast<std::size_t>(i)];
                button.setButtonText(names[i]);
                button.setClickingTogglesState(true);
                button.setRadioGroupId(1);
                button.setToggleState(i == processor_.role(), juce::dontSendNotification);
                button.onClick = [this, i] {
                    if (roles_[static_cast<std::size_t>(i)].getToggleState())
                        processor_.setRole(i);
                };
                addAndMakeVisible(button);
            }
        }
        timerCallback();
        startTimerHz(4);
        const int rows = (JucePlugin_IsSynth ? 1 : 0) + (hasOpenButton ? 1 : 0);
        setSize(JucePlugin_IsSynth ? 440 : 280, 2 * margin + rows * (rowHeight + gap) + statusHeight);
    }
    ~Editor() override { stopTimer(); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff17161d)); }
    void resized() override {
        auto area = getLocalBounds().reduced(margin);
        status_.setBounds(area.removeFromBottom(statusHeight));
        if (JucePlugin_IsSynth) {
            auto row = area.removeFromTop(rowHeight);
            for (auto& button : roles_)
                button.setBounds(row.removeFromLeft(80).reduced(1));
            area.removeFromTop(gap);
        }
#if JUCE_MAC
        open_.setBounds(area.removeFromTop(rowHeight));
#endif
    }

  private:
    void timerCallback() override {
        status_.setText(processor_.hubConnected() ? "Connected" : "Not connected", juce::dontSendNotification);
#if JUCE_MAC
        (void)opener_.isRunning(); // collects `open` once it has exited, so no zombie is left in the host
#endif
    }

    static constexpr int roleCount = 5;
    static constexpr int margin = 20;
    static constexpr int rowHeight = 44;
    static constexpr int gap = 12;
    static constexpr int statusHeight = 20;
#if JUCE_MAC
    static constexpr bool hasOpenButton = true;
    juce::TextButton open_;
    juce::ChildProcess opener_;
#else
    static constexpr bool hasOpenButton = false; // no launcher on other platforms yet
#endif
    Processor& processor_;
    juce::Label status_;
    std::array<juce::TextButton, roleCount> roles_;
};

} // namespace

Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withInput(JucePlugin_IsSynth ? "Sidechain" : "Input", juce::AudioChannelSet::stereo(),
                                    !JucePlugin_IsSynth)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      link_(capture_, samples_) {
    // Fixed ids. Hosts forbid changing the automatable set, so scenes bind to these slots.
    for (int i = 0; i < macroCount; ++i) {
        const auto number = juce::String(i + 1);
        auto parameter = std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"macro" + number, 1},
                                                                     "Macro " + number, 0.0F, 1.0F, 0.5F);
        macros_[static_cast<std::size_t>(i)] = parameter.get();
        addParameter(parameter.release());
    }
    if (!JucePlugin_IsSynth) {
        setRole(soundRole);
        return;
    }
    claimedLayer_ = claimLayer();
    setRole(claimedLayer_);
}

Processor::~Processor() {
    if (claimedLayer_ > 0)
        layersTaken.fetch_and(~(1U << claimedLayer_));
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto input = layouts.getMainInputChannelSet();
    if (!JucePlugin_IsSynth)
        return input == juce::AudioChannelSet::stereo() && layouts.getMainOutputChannelSet() == input;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           (input.isDisabled() || input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    const juce::ScopedNoDenormals noDenormals;

    Event block;
    block.micros = nowMicros();
    block.level = rms(buffer, getTotalNumInputChannels());
    for (int i = 0; i < macroCount; ++i)
        block.macros[static_cast<std::size_t>(i)] = macros_[static_cast<std::size_t>(i)]->get();

    // Every transport field is optional in every plug-in format. Fall back, never assume.
    if (const auto* playHead = getPlayHead()) {
        if (const auto position = playHead->getPosition()) {
            block.playing = position->getIsPlaying();
            block.bpm = position->getBpm().orFallback(120.0);
            block.ppq = position->getPpqPosition().orFallback(0.0);
            if (const auto signature = position->getTimeSignature()) {
                block.sigNumerator = static_cast<std::uint8_t>(signature->numerator);
                block.sigDenominator = static_cast<std::uint8_t>(signature->denominator);
            }
        }
    }
    // Sound at the input goes to the Stage with the block that carried it, so a take can line up
    // samples and notes exactly. The count only moves when the ring took the block.
    if (const int inputs = getTotalNumInputChannels(); inputs > 0) {
        block.frame = static_cast<std::int64_t>(framesPushed_);
        if (samples_.push(buffer.getReadPointer(0), buffer.getReadPointer(inputs > 1 ? 1 : 0), buffer.getNumSamples()))
            framesPushed_ += static_cast<std::uint64_t>(buffer.getNumSamples());
    }
    capture_.push(block);

    const double beatsPerSample = block.bpm / (60.0 * sampleRate_);
    const double microsPerSample = 1.0e6 / sampleRate_;
    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        if (!message.isNoteOnOrOff())
            continue;
        Event note = block;
        note.type = message.isNoteOn() ? Event::Type::noteOn : Event::Type::noteOff;
        note.note = static_cast<std::uint8_t>(message.getNoteNumber());
        note.channel = static_cast<std::uint8_t>(message.getChannel());
        note.velocity = message.getFloatVelocity();
        note.ppq = block.ppq + (block.playing ? metadata.samplePosition * beatsPerSample : 0.0);
        note.micros = block.micros + static_cast<std::int64_t>(metadata.samplePosition * microsPerSample);
        capture_.push(note);
    }

    midi.clear();
    if (!JucePlugin_IsSynth)
        return; // Stage Sound: the mix goes on untouched
    for (int channel = getTotalNumInputChannels(); channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
    for (int channel = 0; channel < juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels()); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& destination) {
    juce::MemoryOutputStream stream(destination, false);
    for (const auto* macro : macros_)
        stream.writeFloat(macro->get());
    stream.writeInt(role());
}

void Processor::setStateInformation(const void* data, int size) {
    juce::MemoryInputStream stream(data, static_cast<std::size_t>(size), false);
    for (auto* macro : macros_)
        if (stream.getNumBytesRemaining() >= static_cast<juce::int64>(sizeof(float)))
            *macro = stream.readFloat();
    if (stream.getNumBytesRemaining() >= static_cast<juce::int64>(sizeof(int)))
        setRole(JucePlugin_IsSynth ? juce::jlimit(0, 4, stream.readInt()) : soundRole);
}

} // namespace stage

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new stage::Processor(); }
