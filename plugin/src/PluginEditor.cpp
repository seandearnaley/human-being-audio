#include "PluginEditor.h"

namespace tf {

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(processor), model_(processor), view_(model_) {
    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(ui::kMinWidth, ui::kMinHeight, ui::kDefaultWidth * 2, ui::kDefaultHeight * 2);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(ui::kDefaultWidth) / ui::kDefaultHeight);
    setSize(ui::kDefaultWidth, ui::kDefaultHeight);
}

PluginEditor::~PluginEditor() { view_.detach(); }

void PluginEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0b0e13)); }

void PluginEditor::resized() {
    if (attachedTo_ != nullptr) view_.setSize(getWidth(), getHeight());
}

void PluginEditor::parentHierarchyChanged() { attachWhenOnScreen(); }

void PluginEditor::attachWhenOnScreen() {
    auto* peer = getPeer();
    void* nativeView = peer != nullptr ? peer->getNativeHandle() : nullptr;
    if (nativeView == attachedTo_) return;

    view_.detach();
    attachedTo_ = nativeView;
    if (nativeView != nullptr) view_.attach(nativeView, getWidth(), getHeight());
}

}
