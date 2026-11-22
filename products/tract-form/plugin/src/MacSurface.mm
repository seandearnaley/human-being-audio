#import <Cocoa/Cocoa.h>
#include "PluginEditor.h"

namespace tf {

PluginEditor::MacSurface::MacSurface() {
    // NSViewComponent retains the view and follows JUCE visibility, bounds and peer changes.
    setView([[[NSView alloc] initWithFrame:NSZeroRect] autorelease]);
}

}
