// SPDX-License-Identifier: AGPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include "PluginEditor.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("A click on the Cutline wordmark reveals About inside a native Mac host") {
    const juce::ScopedJuceInitialiser_GUI gui;
    cl::PluginProcessor processor;
    juce::Component host;
    cl::PluginEditor editor(processor);
    host.setSize(1100, 680);
    host.addAndMakeVisible(editor);
    editor.setBounds(30, 36, cl::ui::kMinWidth, cl::ui::kMinHeight);
    host.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    host.setVisible(true);
    auto* loop = juce::MessageManager::getInstance();
    loop->runDispatchLoopUntil(30);

    auto* surface = dynamic_cast<juce::NSViewComponent*>(editor.findChildWithID("instrumentSurface"));
    auto* about = editor.findChildWithID("aboutSheet");
    REQUIRE(surface != nullptr);
    REQUIRE(about != nullptr);
    NSView* container = static_cast<NSView*>(surface->getView());
    REQUIRE(container.window != nil);
    NSView* renderer = container.subviews.firstObject;
    REQUIRE(renderer != nil);

    const auto scale = NSWidth(container.bounds) / cl::ui::kDefaultWidth;
    NSPoint point = [container convertPoint:NSMakePoint(72 * scale, NSHeight(container.bounds) - 44 * scale)
                                     toView:nil];
    NSView* root = container.window.contentView;
    const auto hit = [&] { return [root hitTest:[root.superview convertPoint:point fromView:nil]]; };
    for (int repeat = 0; repeat < 2; ++repeat) {
        REQUIRE(hit() == renderer);
        for (const auto type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
            NSEvent* event = [NSEvent mouseEventWithType:type
                                                location:point
                                           modifierFlags:0
                                               timestamp:NSProcessInfo.processInfo.systemUptime
                                            windowNumber:container.window.windowNumber
                                                 context:nil
                                             eventNumber:0
                                              clickCount:1
                                                pressure:1.0];
            if (type == NSEventTypeLeftMouseDown)
                [renderer mouseDown:event];
            else
                [renderer mouseUp:event];
        }
        REQUIRE(about->isShowing());
        REQUIRE([renderer isHiddenOrHasHiddenAncestor]);
        REQUIRE(hit() != renderer);

        REQUIRE(about->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        REQUIRE_FALSE([renderer isHiddenOrHasHiddenAncestor]);
        REQUIRE(container.subviews.firstObject == renderer);
    }
}

TEST_CASE("Cutline exposes visible shared controls to a native accessibility client") {
    const juce::ScopedJuceInitialiser_GUI gui;
    cl::PluginProcessor processor;
    juce::Component host;
    cl::PluginEditor editor(processor);
    host.setSize(cl::ui::kMinWidth, cl::ui::kMinHeight);
    host.addAndMakeVisible(editor);
    editor.setBounds(host.getLocalBounds());
    host.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    host.setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    auto* group = editor.findChildWithID("studioControls");
    REQUIRE(group != nullptr);
    const auto children = group->getAccessibilityHandler()->getChildren();
    auto button =
        std::find_if(children.begin(), children.end(), [](auto* child) { return child->getTitle() == "Next preset"; });
    REQUIRE(button != children.end());
    auto* nativeButton = (id<NSAccessibilityElement>)(*button)->getNativeImplementation();
    REQUIRE(nativeButton != nil);
    auto before = processor.presets.currentName();
    REQUIRE([(id)nativeButton accessibilityPerformPress]);
    CHECK(processor.presets.currentName() != before);
    auto slider = std::find_if(children.begin(), children.end(), [](auto* child) {
        return child->getRole() == juce::AccessibilityRole::slider && child->getValueInterface() != nullptr;
    });
    REQUIRE(slider != children.end());
    auto* value = (*slider)->getValueInterface();
    const auto range = value->getRange();
    REQUIRE(range.isValid());
    value->setValue(range.getMaximumValue());
    CHECK(value->getCurrentValue() == range.getMaximumValue());
    CHECK(value->getCurrentValueAsString().isNotEmpty());
}
