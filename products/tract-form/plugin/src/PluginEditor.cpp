#include "PluginEditor.h"

namespace tf {

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(processor), model_(processor), view_(model_, [safe = juce::Component::SafePointer<PluginEditor>(this)] {
          // Leave the native mouse callback before closing its Visage window.
          juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->showAbout(); });
      }) {
#if JUCE_LINUX
    addAndMakeVisible(linuxHost_);
#endif
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

PluginEditor::~PluginEditor() { detachView(); }

void PluginEditor::detachView() {
#if JUCE_LINUX
    if (const int fd = view_.eventFd(); fd >= 0)
        juce::LinuxEventLoop::unregisterFdCallback(fd);
#endif
    view_.detach();
    attachedTo_ = nullptr;
}

void PluginEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0b0e13)); }

void PluginEditor::resized() {
#if JUCE_LINUX
    linuxHost_.setBounds(getLocalBounds());
#endif
    about_.setBounds(getLocalBounds());
    aboutText_.setBounds(about_.getLocalBounds().reduced(40).withTrimmedTop(64));
    if (attachedTo_ != nullptr) view_.setSize(getWidth(), getHeight());
}

void PluginEditor::parentHierarchyChanged() { attachWhenOnScreen(); }

void PluginEditor::showAbout() {
    if (about_.isVisible()) return;
    detachView();
#if JUCE_LINUX
    linuxHost_.setVisible(false);
#endif
    about_.open();
}

void PluginEditor::attachWhenOnScreen() {
    if (about_.isVisible()) return;
    auto* peer = getPeer();
#if JUCE_LINUX
    linuxHost_.setVisible(true);
    void* nativeView = peer != nullptr ? reinterpret_cast<void*>(linuxHost_.getHostWindowID()) : nullptr;
#else
    void* nativeView = peer != nullptr ? peer->getNativeHandle() : nullptr;
#endif
    if (nativeView == attachedTo_) return;

    detachView();
    if (nativeView == nullptr) return;
    view_.attach(nativeView, getWidth(), getHeight());
    attachedTo_ = nativeView;
#if JUCE_LINUX
    // The embedded Visage window owns a separate X11 connection.
    if (const int fd = view_.eventFd(); fd >= 0)
        juce::LinuxEventLoop::registerFdCallback(fd, [this](int) { view_.dispatchEvents(); });
#endif
}

}
