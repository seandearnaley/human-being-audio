#include "PluginEditor.h"
#include <embedded/tf_brand.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <system_error>
#endif

namespace tf {

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(processor), model_(processor), view_(model_, [this] { showAbout(); }) {
    nativeHost_.setComponentID("instrumentSurface");
    addAndMakeVisible(nativeHost_);
    addChildComponent(about_);
    about_.onClosed = [this] {
        nativeHost_.setVisible(true);
        attachWhenOnScreen();
    };
    model_.setDragSource(this);
    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth * 2, ui::kDefaultHeight * 2);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(ui::kDefaultWidth) / ui::kDefaultHeight);
    setSize(ui::kDefaultWidth, ui::kDefaultHeight);
}

PluginEditor::AboutPage::AboutPage() {
    const auto& art = ui::brand::tract_form_logotype_on_dark_svg;
    wordmark = juce::Drawable::createFromImageData(art.data, static_cast<size_t>(art.size));
    const auto& symbol = ui::brand::tract_form_mark_on_dark_svg;
    monogram = juce::Drawable::createFromImageData(symbol.data, static_cast<size_t>(symbol.size));
    setDescription(getDescription() + ". Monophonic bass synthesizer.");
}

void PluginEditor::AboutPage::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0b0e13));
    StudioAbout::paint(g);
    auto body = getLocalBounds().toFloat().withTrimmedTop(64).reduced(static_cast<float>(getWidth()) * 0.075f, 0);
    const float illustrationSize = juce::jmin(224.0f, body.getWidth() * 0.24f);
    auto illustration = body.removeFromRight(illustrationSize);
    body.removeFromRight(32);
    body.removeFromTop(body.getHeight() * 0.19f);
    monogram->drawWithin(g, { illustration.getX(), body.getY(), illustrationSize, illustrationSize },
                         juce::RectanglePlacement::centred, 1.0f);
    wordmark->drawWithin(g, body.removeFromTop(60).withWidth(310), juce::RectanglePlacement::xLeft, 1.0f);
    body.removeFromTop(22);
    g.setFont(juce::FontOptions(23.0f));
    g.setColour(juce::Colour(0xffe6ebf0));
    g.drawText("Monophonic bass synthesizer", body.removeFromTop(34), juce::Justification::centredLeft);
    body.removeFromTop(12);
    g.setColour(juce::Colour(0xffb2bbc8));
    g.setFont(juce::FontOptions(17.0f));
    g.drawFittedText("One master sine drives two reese voices and a clean sub. Shape the growl with a five-formant tract, wavefolding and four movement lanes.",
                     body.removeFromTop(70).withWidth(juce::jmin(480.0f, body.getWidth())).toNearestInt(),
                     juce::Justification::topLeft, 3);
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
    nativeHost_.setBounds(getLocalBounds());
    about_.setBounds(getLocalBounds());
    if (attachedTo_ != nullptr) view_.setSize(getWidth(), getHeight());
}

void PluginEditor::parentHierarchyChanged() { attachWhenOnScreen(); }

void PluginEditor::showAbout() {
    if (about_.isVisible()) return;
    nativeHost_.setVisible(false);
    about_.open();
}

void PluginEditor::attachWhenOnScreen() {
    auto* peer = getPeer();
    if (peer == nullptr) {
        detachView();
        return;
    }
    if (about_.isVisible()) return;
#if JUCE_MAC
    auto* nativeView = nativeHost_.getView();
#elif JUCE_LINUX
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
