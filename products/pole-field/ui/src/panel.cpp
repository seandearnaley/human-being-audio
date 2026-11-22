#include "panels.h"

#include <algorithm>

namespace pf::ui {

Panel::Panel(Context& context) : context_(context) { context_.live.push_back(this); }

Panel::~Panel() { std::erase(context_.live, static_cast<Live*>(this)); }

}
