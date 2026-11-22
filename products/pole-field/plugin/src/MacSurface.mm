#import <Cocoa/Cocoa.h>
#include "PluginEditor.h"

namespace pf {

PluginEditor::MacSurface::MacSurface() {
    setView([[[NSView alloc] initWithFrame:NSZeroRect] autorelease]);
}

}
