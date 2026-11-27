// SPDX-License-Identifier: AGPL-3.0-or-later
#include "panels.h"

#include <algorithm>

namespace pv::ui {

Panel::Panel(Context& context) : context_(context) { context_.live.push_back(this); }

Panel::~Panel() { std::erase(context_.live, static_cast<Live*>(this)); }

} // namespace pv::ui
