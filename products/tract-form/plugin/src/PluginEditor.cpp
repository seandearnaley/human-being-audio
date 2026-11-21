#include "PluginEditor.h"

namespace tf {

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(processor), model_(processor), view_(model_, [safe = juce::Component::SafePointer<PluginEditor>(this)] {
          // Leave the native mouse callback before closing its Visage window.
          juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->showAbout(); });
      }) {
    addChildComponent(about_);
    aboutText_.setText("Tract Form\n\nMonophonic bass synthesizer\n\n"
                       "One master sine drives two reese voices and a clean sub. Shape the growl "
                       "with a five-formant tract, wavefolding and four movement lanes.",
                       juce::dontSendNotification);
    aboutText_.setFont(juce::FontOptions(24.0f));
    aboutText_.setColour(juce::Label::textColourId, juce::Colours::white);
    about_.addAndMakeVisible(aboutText_);
    about_.onClosed = [this] { attachWhenOnScreen(); };
    model_.setDragSource(this);
    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth * 2, ui::kDefaultHeight * 2);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(ui::kDefaultWidth) / ui::kDefaultHeight);
    setSize(ui::kDefaultWidth, ui::kDefaultHeight);
}

PluginEditor::~PluginEditor() { view_.detach(); }

void PluginEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0b0e13)); }

void PluginEditor::resized() {
    about_.setBounds(getLocalBounds());
    aboutText_.setBounds(about_.getLocalBounds().reduced(40).withTrimmedTop(64));
    if (attachedTo_ != nullptr) view_.setSize(getWidth(), getHeight());
}

void PluginEditor::parentHierarchyChanged() { attachWhenOnScreen(); }

void PluginEditor::showAbout() {
    if (about_.isVisible()) return;
    view_.detach();
    attachedTo_ = nullptr;
    about_.open();
}

void PluginEditor::attachWhenOnScreen() {
    if (about_.isVisible()) return;
    auto* peer = getPeer();
    void* nativeView = peer != nullptr ? peer->getNativeHandle() : nullptr;
    if (nativeView == attachedTo_) return;

    view_.detach();
    attachedTo_ = nativeView;
    if (nativeView != nullptr) view_.attach(nativeView, getWidth(), getHeight());
}

}
