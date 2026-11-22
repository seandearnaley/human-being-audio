// SPDX-License-Identifier: AGPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include "PluginEditor.h"

namespace cl {

PluginEditor::MacSurface::MacSurface() { setView([[[NSView alloc] initWithFrame:NSZeroRect] autorelease]); }

} // namespace cl
