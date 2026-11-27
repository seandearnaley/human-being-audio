// SPDX-License-Identifier: GPL-3.0-or-later
// Shared JUCE AudioProcessorEditor + About overlay hosting an embedded VisageSurface.
#pragma once

#include <StudioAbout.h>
#include <hba/host/accessible_controls.h>
#include <hba/host/visage_surface.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <memory>

namespace hba::host {

struct StudioAboutConfig {
    const void* wordmarkData = nullptr;
    std::size_t wordmarkSize = 0;
    const void* monogramData = nullptr;
    std::size_t monogramSize = 0;
    const char* accessibilitySuffix = "";
    const char* headline = "";
    const char* body = "";
    std::uint32_t backgroundArgb = 0xff0b0e13u;
    float wordmarkWidth = 310.0f;
    float wordmarkHeight = 60.0f;
    float iconMaxSize = 224.0f;
    float iconWidthFrac = 0.24f;
    float topPadFrac = 0.19f;
    float headlineFont = 23.0f;
    float bodyFont = 17.0f;
    float bodyMaxWidth = 480.0f;
    float bodyHeight = 84.0f;
    int bodyMaxLines = 4;
};

class StudioAboutPage : public hba::StudioAbout {
  public:
    explicit StudioAboutPage(const StudioAboutConfig& config) : config_(config) {
        if (config_.wordmarkData != nullptr && config_.wordmarkSize > 0u)
            wordmark_ = juce::Drawable::createFromImageData(config_.wordmarkData, config_.wordmarkSize);
        if (config_.monogramData != nullptr && config_.monogramSize > 0u)
            monogram_ = juce::Drawable::createFromImageData(config_.monogramData, config_.monogramSize);
        if (config_.accessibilitySuffix != nullptr && config_.accessibilitySuffix[0] != '\0')
            setDescription(getDescription() + config_.accessibilitySuffix);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(config_.backgroundArgb));
        StudioAbout::paint(g);
        auto body = getLocalBounds().toFloat().withTrimmedTop(64).reduced(static_cast<float>(getWidth()) * 0.075f, 0);
        const float iconSize = juce::jmin(config_.iconMaxSize, body.getWidth() * config_.iconWidthFrac);
        auto iconArea = body.removeFromRight(iconSize);
        body.removeFromRight(32);
        body.removeFromTop(body.getHeight() * config_.topPadFrac);
        if (monogram_ != nullptr) {
            monogram_->drawWithin(g, {iconArea.getX(), body.getY(), iconSize, iconSize},
                                  juce::RectanglePlacement::centred, 1.0f);
        }
        if (wordmark_ != nullptr) {
            wordmark_->drawWithin(g, body.removeFromTop(config_.wordmarkHeight).withWidth(config_.wordmarkWidth),
                                  juce::RectanglePlacement::xLeft, 1.0f);
        }
        body.removeFromTop(20);
        g.setFont(juce::FontOptions(config_.headlineFont));
        g.setColour(juce::Colour(0xffe6ebf0));
        g.drawText(config_.headline, body.removeFromTop(34), juce::Justification::centredLeft);
        body.removeFromTop(12);
        g.setColour(juce::Colour(0xffb2bbc8));
        g.setFont(juce::FontOptions(config_.bodyFont));
        g.drawFittedText(config_.body,
                         body.removeFromTop(config_.bodyHeight)
                             .withWidth(juce::jmin(config_.bodyMaxWidth, body.getWidth()))
                             .toNearestInt(),
                         juce::Justification::topLeft, config_.bodyMaxLines);
    }

  private:
    StudioAboutConfig config_;
    std::unique_ptr<juce::Drawable> wordmark_;
    std::unique_ptr<juce::Drawable> monogram_;
};

template <typename ProcessorT, typename ModelT, typename ViewT, typename AboutPageT = StudioAboutPage>
class StudioPluginEditor : public juce::AudioProcessorEditor {
  public:
    template <typename... AboutArgs>
    StudioPluginEditor(ProcessorT& owner, int minW, int minH, int defaultW, int defaultH, std::uint32_t backgroundArgb,
                       AboutArgs&&... aboutArgs)
        : AudioProcessorEditor(owner), model_(owner), view_(model_, [this] { showAbout(); }),
          controls_(view_.rootFrame(), owner), about_(std::forward<AboutArgs>(aboutArgs)...),
          backgroundArgb_(backgroundArgb) {
        nativeHost_.setComponentID("instrumentSurface");
        addAndMakeVisible(nativeHost_);
        addAndMakeVisible(controls_);
        addChildComponent(about_);
        model_.openHostPresetMenu = [this](visage::Frame& anchor) { openPresetMenu(anchor); };
        about_.onClosed = [this] {
            controls_.setVisible(true);
            nativeHost_.setVisible(true);
            attachWhenOnScreen();
        };
        if constexpr (requires { model_.setDragSource(this); })
            model_.setDragSource(this);
        setOpaque(true);
        setResizable(true, true);
        setResizeLimits(minW, minH, defaultW * 2, defaultH * 2);
        getConstrainer()->setFixedAspectRatio(static_cast<double>(defaultW) / static_cast<double>(defaultH));
        setSize(defaultW, defaultH);
    }

    ~StudioPluginEditor() override { detachView(); }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(backgroundArgb_)); }

    void resized() override {
        nativeHost_.setBounds(getLocalBounds());
        controls_.setBounds(getLocalBounds());
        about_.setBounds(getLocalBounds());
        if (attachedTo_ != nullptr)
            view_.setSize(getWidth(), getHeight());
    }

    void parentHierarchyChanged() override { attachWhenOnScreen(); }

    void showAbout() {
        if (about_.isVisible())
            return;
        controls_.setVisible(false);
        nativeHost_.setVisible(false);
        about_.open();
    }

  protected:
    void openPresetMenu(visage::Frame& anchor) {
        juce::PopupMenu menu;
        const auto presets = model_.presets();
        int index = 0;
        const int count = static_cast<int>(presets.size());
        while (index < count) {
            const auto category = presets[static_cast<std::size_t>(index)].category;
            juce::PopupMenu group;
            do {
                const auto& preset = presets[static_cast<std::size_t>(index)];
                group.addItem(++index, juce::String::fromUTF8(preset.name.c_str()));
            } while (index < count && presets[static_cast<std::size_t>(index)].category == category);
            menu.addSubMenu(juce::String::fromUTF8(category.c_str()), group);
        }
        menu.addSeparator();
        menu.addItem(count + 1, "Save User Preset...");
        menu.addItem(count + 2,
                     model_.isSlotB() ? "Copy B to A (Shift+Enter on A/B)" : "Copy A to B (Shift+Enter on A/B)");
        const auto area = controlBounds(anchor, view_.rootFrame(), getWidth());
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(area)),
            [editor = juce::Component::SafePointer<StudioPluginEditor>(this), count](int selection) {
                if (!editor || selection == 0)
                    return;
                if (selection == count + 1)
                    editor->model_.savePreset();
                else if (selection == count + 2)
                    editor->model_.copyAB();
                else
                    editor->model_.loadPreset(selection - 1);
            });
    }

    void attachWhenOnScreen() {
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
        // The embedded Visage window owns a separate X11 connection.
        if (const int fd = view_.eventFd(); fd >= 0)
            juce::LinuxEventLoop::registerFdCallback(fd, [this](int) { view_.dispatchEvents(); });
#endif
    }

    void detachView() {
#if JUCE_LINUX
        if (const int fd = view_.eventFd(); fd >= 0)
            juce::LinuxEventLoop::unregisterFdCallback(fd);
#endif
        view_.detach();
        attachedTo_ = nullptr;
    }

    ModelT model_;
    ViewT view_;
    VisageSurface nativeHost_;
    AccessibleControls controls_;
    AboutPageT about_;
    void* attachedTo_ = nullptr;
    std::uint32_t backgroundArgb_ = 0xff0b0e13u;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioPluginEditor)
};

} // namespace hba::host
