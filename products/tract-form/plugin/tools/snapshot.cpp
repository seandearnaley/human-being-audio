// tf-plugin-snapshot <out.png> [width height] [note] [seconds] [preset name]
// The whole stack without a host or an audio device: plays a note through the
// real processor, then renders the interface from the real analysis data.
#include "PluginModel.h"
#include "PluginProcessor.h"
#include "tf/ui/editor.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: tf-plugin-snapshot <out.png> [width height] [note] [seconds] [preset name]\n");
        return 2;
    }
    const int width = argc > 3 ? std::atoi(argv[2]) : tf::ui::kDefaultWidth;
    const int height = argc > 3 ? std::atoi(argv[3]) : tf::ui::kDefaultHeight;
    const int note = argc > 4 ? std::atoi(argv[4]) : 29;
    const double seconds = argc > 5 ? std::atof(argv[5]) : 1.0;

    constexpr double sampleRate = 48000.0;
    constexpr int block = 512;

    const juce::ScopedJuceInitialiser_GUI juce;
    tf::PluginProcessor processor;
    processor.setPlayConfigDetails(0, 2, sampleRate, block);
    processor.prepareToPlay(sampleRate, block);
    tf::PluginModel model(processor);

    if (argc > 6) {
        const std::string wanted = argv[6];
        const auto presets = model.presets();
        int index = -1;
        for (std::size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == wanted) index = static_cast<int>(i);
        if (index < 0) {
            std::fprintf(stderr, "no preset named \"%s\"\n", wanted.c_str());
            return 1;
        }
        model.loadPreset(index);
    }

    juce::AudioBuffer<float> buffer(2, block);
    const int blocks = static_cast<int>(seconds * sampleRate / block);
    for (int i = 0; i < blocks; ++i) {
        juce::MidiBuffer midi;
        if (i == 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
        // Read the analysis as the interface would, so the queue never overflows.
        if (i % 2 == 0)
            model.refreshVisuals();
    }

    tf::ui::Editor editor(model);
    editor.renderToPng(argv[1], width, height);
    std::printf("%s %dx%d note %d\n", argv[1], width, height, note);
    return 0;
}
