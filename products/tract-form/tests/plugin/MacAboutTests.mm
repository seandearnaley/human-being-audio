#import <Cocoa/Cocoa.h>
#include "PluginEditor.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("A click on the wordmark reveals About inside a native Mac host") {
    const juce::ScopedJuceInitialiser_GUI gui;
    tf::PluginProcessor processor;
    juce::Component host;
    tf::PluginEditor editor(processor);
    host.setSize(1100, 680);
    host.addAndMakeVisible(editor);
    editor.setBounds(30, 36, tf::ui::kMinWidth, tf::ui::kMinHeight);
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

    // Click inside the painted logo, in an editor offset inside the host window.
    const auto scale = NSWidth(container.bounds) / tf::ui::kDefaultWidth;
    NSPoint point = [container convertPoint:NSMakePoint(72 * scale, NSHeight(container.bounds) - 44 * scale) toView:nil];
    NSView* root = container.window.contentView;
    const auto hit = [&] { return [root hitTest:[root.superview convertPoint:point fromView:nil]]; };
    for (int repeat = 0; repeat < 2; ++repeat) {
        REQUIRE(hit() == renderer);
        for (const auto type : { NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp }) {
            NSEvent* event = [NSEvent mouseEventWithType:type location:point modifierFlags:0
                                timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:container.window.windowNumber
                                context:nil eventNumber:0 clickCount:1 pressure:1.0];
            if (type == NSEventTypeLeftMouseDown) [renderer mouseDown:event];
            else [renderer mouseUp:event];
        }
        REQUIRE(about->isShowing());
        REQUIRE([renderer isHiddenOrHasHiddenAncestor]);
        REQUIRE(hit() != renderer);

        REQUIRE(about->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        REQUIRE_FALSE([renderer isHiddenOrHasHiddenAncestor]);
        REQUIRE(container.subviews.firstObject == renderer);
    }
}
