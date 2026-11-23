#include "PluginEditor.h"
#include <embedded/pf_brand.h>

namespace pf {

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(processor), model_(processor), view_(model_, [this] { showAbout(); }) {
    nativeHost_.setComponentID("instrumentSurface");
    addAndMakeVisible(nativeHost_);
    addChildComponent(about_);
    about_.onClosed = [this] {
        nativeHost_.setVisible(true);
        attachWhenOnScreen();
    };
    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth * 2, ui::kDefaultHeight * 2);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(ui::kDefaultWidth) / ui::kDefaultHeight);
    setSize(ui::kDefaultWidth, ui::kDefaultHeight);
}

PluginEditor::AboutPage::AboutPage() {
    const auto& art = ui::brand::pole_field_logotype_on_dark_svg;
    wordmark = juce::Drawable::createFromImageData(art.data, static_cast<size_t>(art.size));
    const auto& symbol = ui::brand::pole_field_mark_on_dark_svg;
    monogram = juce::Drawable::createFromImageData(symbol.data, static_cast<size_t>(symbol.size));
    setDescription(getDescription() + ". 14-pole Z-plane morphing filterbank.");
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
    g.drawText("14-pole Z-plane morphing filterbank", body.removeFromTop(34), juce::Justification::centredLeft);
    body.removeFromTop(12);
    g.setColour(juce::Colour(0xffb2bbc8));
    g.setFont(juce::FontOptions(17.0f));
    g.drawFittedText("Seven cascaded pole-zero sections interpolated in polar coordinates across 3D morph cubes, with inter-stage ADAA drive, Burg LPC audio imprinting and mono sub lock.",
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
    auto* nativeView = nativeHost_.nativeHandle(peer);
    if (nativeView == attachedTo_) return;

    detachView();
    if (nativeView == nullptr) return;
    view_.attach(nativeView, getWidth(), getHeight());
    attachedTo_ = nativeView;
#if JUCE_LINUX
    if (const int fd = view_.eventFd(); fd >= 0)
        juce::LinuxEventLoop::registerFdCallback(fd, [this](int) { view_.dispatchEvents(); });
#endif
}

bool PluginEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files) {
        const auto ext = juce::File(path).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac")
            return true;
    }
    return false;
}

void PluginEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(ui::kDefaultWidth);
    const float padX = 24.0f * scale;
    const float padY = 100.0f * scale;
    const float padW = 344.0f * scale;
    const float padH = 408.0f * scale;

    int slot = 0;
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    if (fx >= padX && fx <= padX + padW && fy >= padY && fy <= padY + padH) {
        const int col = (fx >= padX + 0.5f * padW) ? 1 : 0;
        const int row = (fy >= padY + 0.5f * padH) ? 1 : 0;
        slot = row * 2 + col;
    }

    for (const auto& path : files) {
        const juce::File file(path);
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac") {
            model_.imprintFileToSlot(file, slot);
            return;
        }
    }
}
}
