// SPDX-License-Identifier: GPL-3.0-or-later
#import <Cocoa/Cocoa.h>
#include <hba/host/visage_surface.h>

namespace hba::host {

VisageSurface::VisageSurface() { setView([[[NSView alloc] initWithFrame:NSZeroRect] autorelease]); }

} // namespace hba::host
