// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include <catch2/catch_test_macros.hpp>
#include <hba/core/wav.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

@interface HBAFileDragInfo : NSObject
@property(nonatomic, assign) NSPasteboard* draggingPasteboard;
@property(nonatomic) NSPoint draggingLocation;
@end
@implementation HBAFileDragInfo
- (id)draggingSource {
    return nil;
}
- (NSDragOperation)draggingSourceOperationMask {
    return NSDragOperationCopy;
}
@end

TEST_CASE("Dropping a WAV on the embedded Mac editor imports it into processor state", "[mac-file-drop]") {
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto originalPolicy = NSApp.activationPolicy;
    const juce::ScopeGuard restorePolicy{[originalPolicy] { [NSApp setActivationPolicy:originalPolicy]; }};
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    juce::Component host;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
    REQUIRE(editor != nullptr);
    host.setBounds(-4000, -4000, editor->getWidth(), editor->getHeight());
    host.addAndMakeVisible(*editor);
    host.addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
    host.setVisible(true);

    juce::TemporaryFile folder;
    REQUIRE(folder.getFile().createDirectory().wasOk());
    const auto file = folder.getFile().getChildFile("ImportProbe.wav");
    std::array<float, 2048> samples{};
    samples[32] = 0.8f;
    REQUIRE(hba::core::writeWavFloat(file.getFullPathName().toStdString(), samples, 1, 48000));

    auto* surface = dynamic_cast<juce::NSViewComponent*>(editor->findChildWithID("instrumentSurface"));
    REQUIRE(surface != nullptr);
    NSView* container = static_cast<NSView*>(surface->getView());
    REQUIRE(container.window != nil);
    const auto point = [container convertPoint:NSMakePoint(280, NSHeight(container.bounds) - 400) toView:nil];
    NSView* root = container.window.contentView;
    NSView* target = [root hitTest:[root.superview convertPoint:point fromView:nil]];
    REQUIRE(target == container.subviews.firstObject);
    NSPasteboard* pasteboard = [NSPasteboard pasteboardWithUniqueName];
    const juce::ScopeGuard releasePasteboard{[pasteboard] { [pasteboard releaseGlobally]; }};
    auto* path = [NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()];
    REQUIRE([pasteboard writeObjects:@[ [NSURL fileURLWithPath:path] ]]);
    while (target != nil && [pasteboard availableTypeFromArray:target.registeredDraggedTypes] == nil)
        target = target.superview;
    REQUIRE(target != nil);

    HBAFileDragInfo* info = [[[HBAFileDragInfo alloc] init] autorelease];
    info.draggingPasteboard = pasteboard;
    info.draggingLocation = point;
    auto drag = (id<NSDraggingInfo>)info;
    REQUIRE([target draggingEntered:drag] != NSDragOperationNone);
    REQUIRE([target prepareForDragOperation:drag]);
    REQUIRE([target performDragOperation:drag]);
    // JUCE dispatches the file-drop callback on the message queue.
    REQUIRE(juce::MessageManager::callAsync([] { CFRunLoopStop(CFRunLoopGetCurrent()); }));
    CFRunLoopRun();
    juce::MemoryBlock state;
    processor->getStateInformation(state);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(xml != nullptr);
    CHECK(xml->toString().contains("ImportProbe"));
    if (processor->getName() == "Pole Field")
        CHECK(xml->getStringAttribute("imprintName3") == "ImportProbe");
}
