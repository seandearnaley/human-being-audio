// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

#include <embedded/tg_brand.h>

namespace tg {

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
    const auto& art = ui::brand::torus_glide_logotype_on_dark_svg;
    wordmark = juce::Drawable::createFromImageData(art.data, static_cast<std::size_t>(art.size));
    const auto& sym = ui::brand::torus_glide_mark_on_dark_svg;
    monogram = juce::Drawable::createFromImageData(sym.data, static_cast<std::size_t>(sym.size));
    setDescription(getDescription() + ". Continuous pitch-manifold synthesizer.");
}

void PluginEditor::AboutPage::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0a0d12));
    StudioAbout::paint(g);
    auto body = getLocalBounds().toFloat().withTrimmedTop(64).reduced(static_cast<float>(getWidth()) * 0.075f, 0);
    const float iconSize = juce::jmin(210.0f, body.getWidth() * 0.22f);
    auto iconArea = body.removeFromRight(iconSize);
    body.removeFromRight(32);
    body.removeFromTop(body.getHeight() * 0.18f);
    if (monogram != nullptr)
        monogram->drawWithin(g, {iconArea.getX(), body.getY(), iconSize, iconSize}, juce::RectanglePlacement::centred,
                             1.0f);
    if (wordmark != nullptr)
        wordmark->drawWithin(g, body.removeFromTop(56).withWidth(320), juce::RectanglePlacement::xLeft, 1.0f);
    body.removeFromTop(20);
    g.setFont(juce::FontOptions(22.0f));
    g.setColour(juce::Colour(0xffe6ebf0));
    g.drawText("Continuous pitch-manifold synthesizer", body.removeFromTop(34), juce::Justification::centredLeft);
    body.removeFromTop(12);
    g.setColour(juce::Colour(0xffb2bbc8));
    g.setFont(juce::FontOptions(16.0f));
    g.drawFittedText(
        "Bar-locked Shepard-Risset bass spirals with a mono sub-anchor floor, programmable polyphonic chord bends with "
        "Tymoczko voice-leading and Scale/Just-Intonation gravity wells, and Mobius infinite chord glides.",
        body.removeFromTop(84).withWidth(juce::jmin(520.0f, body.getWidth())).toNearestInt(),
        juce::Justification::topLeft, 4);
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

void PluginEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0a0d12)); }

void PluginEditor::resized() {
    nativeHost_.setBounds(getLocalBounds());
    about_.setBounds(getLocalBounds());
    if (attachedTo_ != nullptr)
        view_.setSize(getWidth(), getHeight());
}

void PluginEditor::parentHierarchyChanged() { attachWhenOnScreen(); }

void PluginEditor::showAbout() {
    if (about_.isVisible())
        return;
    nativeHost_.setVisible(false);
    about_.open();
}

void PluginEditor::attachWhenOnScreen() {
    auto* peer = getPeer();
    if (peer == nullptr) {
        detachView();
        return;
    }
    if (about_.isVisible())
        return;
    auto* nativeView = nativeHost_.nativeHandle(peer);
    if (nativeView == attachedTo_)
        return;

    detachView();
    if (nativeView == nullptr)
        return;
    view_.attach(nativeView, getWidth(), getHeight());
    attachedTo_ = nativeView;
#if JUCE_LINUX
    if (const int fd = view_.eventFd(); fd >= 0)
        juce::LinuxEventLoop::registerFdCallback(fd, [this](int) { view_.dispatchEvents(); });
#endif
}

} // namespace tg
