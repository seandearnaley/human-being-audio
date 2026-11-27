// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace visage {
class Frame;
}

namespace hba::ui {

struct ParameterControl {
    virtual ~ParameterControl() = default;
    virtual std::string_view parameterKey() const = 0;
    virtual std::string valueText() const = 0;
    virtual void setFromNormalised(float value) = 0;
};

struct ActionControl {
    virtual ~ActionControl() = default;
    virtual void activate() = 0;
};

struct PresetMenuHost {
    std::function<void(visage::Frame&)> openHostPresetMenu;
};

} // namespace hba::ui
