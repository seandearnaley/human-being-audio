// Wordmark, preset navigation and the six macros.
#pragma once

#include "controls.h"

#include <array>
#include <memory>

namespace tf::ui {

class TopBar final : public visage::Frame, public Live {
public:
    explicit TopBar(Context& context);
    ~TopBar() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;

private:
    void showPresetMenu();

    Context& context_;
    Button previous_;
    Button name_;
    Button next_;
    Button save_;
    std::array<std::unique_ptr<Knob>, 6> macros_;
    std::string shownPreset_;
};

}
