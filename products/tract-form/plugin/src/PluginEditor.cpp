#include "PluginEditor.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <system_error>
#endif

namespace tf {

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(processor), model_(processor), view_(model_, [safe = juce::Component::SafePointer<PluginEditor>(this)] {
          // Leave the native mouse callback before closing its Visage window.
          juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->showAbout(); });
      }) {
#if JUCE_LINUX || JUCE_WINDOWS
    addAndMakeVisible(nativeHost_);
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
#if JUCE_LINUX || JUCE_WINDOWS
    nativeHost_.setBounds(getLocalBounds());
#endif
    about_.setBounds(getLocalBounds());
    aboutText_.setBounds(about_.getLocalBounds().reduced(40).withTrimmedTop(64));
    if (attachedTo_ != nullptr) view_.setSize(getWidth(), getHeight());
}

void PluginEditor::parentHierarchyChanged() { attachWhenOnScreen(); }

void PluginEditor::showAbout() {
    if (about_.isVisible()) return;
    detachView();
#if JUCE_LINUX || JUCE_WINDOWS
    nativeHost_.setVisible(false);
#endif
    about_.open();
}

void PluginEditor::attachWhenOnScreen() {
    if (about_.isVisible()) return;
    auto* peer = getPeer();
    if (peer == nullptr) {
        detachView();
        return;
    }
#if JUCE_LINUX || JUCE_WINDOWS
    nativeHost_.setVisible(true);
#endif
#if JUCE_LINUX
    auto* nativeView = reinterpret_cast<void*>(nativeHost_.getHostWindowID());
#elif JUCE_WINDOWS
    if (nativeHost_.getHWND() == nullptr) {
        // JUCE positions this child inside the editor and owns its lifetime.
        auto window = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 0, 0, static_cast<HWND>(peer->getNativeHandle()),
                                      nullptr, nullptr, nullptr);
        if (window == nullptr)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Create editor window");
        nativeHost_.setHWND(window);
    }
    auto* nativeView = nativeHost_.getHWND();
#else
    auto* nativeView = peer->getNativeHandle();
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
