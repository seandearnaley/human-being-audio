// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginEditor.h"

LivecutAboutPage::LivecutAboutPage() {
    setDescription(getDescription() +
                   ". Real-time breakbeat & spectral live slicer with Pad performance surface, "
                   "4-voice resonator, granular & tape readers, Sub-Anchor and MIDI C1-G1 triggering.");
}

void LivecutAboutPage::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0a0d12));
    StudioAbout::paint(g);

    auto body = getLocalBounds().toFloat().withTrimmedTop(64).reduced(static_cast<float>(getWidth()) * 0.075f, 0);
    body.removeFromTop(body.getHeight() * 0.16f);

    g.setFont(juce::FontOptions(38.0f).withStyle("Bold"));
    g.setColour(juce::Colour(0xffe6ebf0));
    g.drawText("LIVE", body.withHeight(44).withWidth(94), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffff6b1a));
    g.drawLine(body.getX() + 94.0f, body.getY() + 38.0f, body.getX() + 108.0f, body.getY() + 6.0f, 3.5f);
    g.setColour(juce::Colour(0xffe6ebf0));
    g.drawText("CUT REDUX", body.withTrimmedLeft(114).withTrimmedTop(4).withHeight(44),
               juce::Justification::centredLeft);

    body.removeFromTop(62);
    g.setFont(juce::FontOptions(22.0f));
    g.setColour(juce::Colour(0xffe6ebf0));
    g.drawText("Breakbeat & spectral live slicer", body.removeFromTop(32), juce::Justification::centredLeft);

    body.removeFromTop(12);
    g.setColour(juce::Colour(0xffa9b4c2));
    g.setFont(juce::FontOptions(16.0f));
    g.drawFittedText(
        "Real-time BBCut-inspired phrase slicer and performance instrument. Trigger eight tactile Pad programs "
        "by touch or MIDI C1-G1 with CC1/CC74 expression, lock sub-bass mono with LR4 Sub-Anchor, shape every "
        "repeat through ADAA drive, SVF filter, tuned resonator and spectral freeze, and drag captured phrases "
        "straight to your DAW as 32-bit float WAV.",
        body.removeFromTop(104).withWidth(juce::jmin(560.0f, body.getWidth())).toNearestInt(),
        juce::Justification::topLeft, 5);
}

LivecutAudioProcessorEditor::LivecutAudioProcessorEditor(LivecutAudioProcessor& proc)
    : StudioPluginEditor(proc, lc::ui::kMinWidth, lc::ui::kMinHeight, lc::ui::kDefaultWidth, lc::ui::kDefaultHeight,
                         0xff0a0d12u) {}
