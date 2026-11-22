#pragma once

#include "controls.h"

#include <visage_widgets/button.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pf::ui {

class TopBar final : public visage::Frame, public Live {
public:
    TopBar(Context& context, std::function<void()> showAbout);
    ~TopBar() override;

    void tick() override;
    void resized() override;

private:
    void drawWordmark(visage::Canvas& canvas);
    void showPresetMenu();

    Context& context_;
    visage::Button wordmark_;
    Button previous_;
    Button name_;
    Button next_;
    Button save_;
    std::vector<std::unique_ptr<Knob>> macros_;
    std::string shownPreset_;
};

}
