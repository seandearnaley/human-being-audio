// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include <hba/host/visage_surface.h>

namespace hba::host {

VisageSurface::VisageSurface() { setView([[[NSView alloc] initWithFrame:NSZeroRect] autorelease]); }

void VisageSurface::useHostFileDropTarget() {
    // The embedded Metal view registers file URLs. Let AppKit route them to the JUCE editor.
    NSView* container = static_cast<NSView*>(getView());
    for (NSView* child in container.subviews)
        [child unregisterDraggedTypes];
}

} // namespace hba::host
