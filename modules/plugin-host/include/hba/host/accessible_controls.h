// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hba/ui/accessible_controls.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <visage_widgets/button.h>

#include <map>

namespace hba::host {

inline juce::Rectangle<int> controlBounds(const visage::Frame& frame, const visage::Frame& root, int width) {
    const auto pos = frame.positionInWindow();
    const auto scale = static_cast<float>(width) / std::max(1.0f, root.width());
    return (juce::Rectangle<float>(pos.x, pos.y, frame.width(), frame.height()) * scale).toNearestInt();
}

// Native screen readers query this on the message thread. The Visage view remains the editor's pointer target.
class AccessibleControl final : public juce::Component {
  public:
    AccessibleControl(visage::Frame& frame, juce::RangedAudioParameter* parameter)
        : frame_(frame), life_(frame.aliveToken()), parameter_(parameter) {
        setInterceptsMouseClicks(false, false);
    }
    bool expired() const { return life_.expired(); }
    void sync(juce::Rectangle<int> bounds) {
        setTitle(juce::String::fromUTF8(frame_.name().c_str()));
        setBounds(bounds);
        setEnabled(frame_.acceptsKeystrokes());
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        juce::AccessibilityActions actions;
        actions.addAction(juce::AccessibilityActionType::focus, [this] {
            if (!expired() && isEnabled())
                frame_.requestKeyboardFocus();
        });
        if (parameter_ != nullptr)
            return std::make_unique<juce::AccessibilityHandler>(
                *this, juce::AccessibilityRole::slider, actions,
                juce::AccessibilityHandler::Interfaces{std::make_unique<Value>(*this)});
        actions.addAction(juce::AccessibilityActionType::press, [this] {
            if (expired() || !isEnabled())
                return;
            if (auto* action = dynamic_cast<hba::ui::ActionControl*>(&frame_))
                action->activate();
            else if (auto* button = dynamic_cast<visage::Button*>(&frame_))
                button->notify(false);
        });
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::button, actions);
    }

  private:
    class Value final : public juce::AccessibilityValueInterface {
      public:
        explicit Value(AccessibleControl& control) : control_(control), parameter_(*control.parameter_) {}
        bool isReadOnly() const override { return control_.expired() || !control_.isEnabled(); }
        double getCurrentValue() const override { return parameter_.convertFrom0to1(parameter_.getValue()); }
        juce::String getCurrentValueAsString() const override {
            if (control_.expired())
                return {};
            return juce::String::fromUTF8(
                dynamic_cast<hba::ui::ParameterControl&>(control_.frame_).valueText().c_str());
        }
        void setValue(double v) override { setNormalised(parameter_.convertTo0to1(static_cast<float>(v))); }
        void setValueAsString(const juce::String& s) override { setNormalised(parameter_.getValueForText(s)); }
        AccessibleValueRange getRange() const override {
            const auto& range = parameter_.getNormalisableRange();
            const double step = range.interval > 0 ? range.interval : (range.end - range.start) * 0.01;
            return {{range.start, range.end}, step};
        }

      private:
        void setNormalised(float value) {
            if (isReadOnly())
                return;
            dynamic_cast<hba::ui::ParameterControl&>(control_.frame_).setFromNormalised(value);
            control_.getAccessibilityHandler()->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
        }
        AccessibleControl& control_;
        juce::RangedAudioParameter& parameter_;
    };

    visage::Frame& frame_;
    std::weak_ptr<bool> life_;
    juce::RangedAudioParameter* parameter_;
};

class AccessibleControls final : public juce::Component {
  public:
    AccessibleControls(visage::Frame& root, juce::AudioProcessor& processor) : root_(root), processor_(processor) {
        setComponentID("studioControls");
        setTitle("Instrument controls");
        setInterceptsMouseClicks(false, false);
        setFocusContainerType(FocusContainerType::focusContainer);
    }

    std::unique_ptr<juce::ComponentTraverser> createFocusTraverser() override {
        if (!syncing_) {
            const juce::ScopedValueSetter<bool> guard(syncing_, true);
            std::erase_if(controls_, [](const auto& item) { return item.second->expired(); });
            visit(root_, true);
        }
        return Component::createFocusTraverser();
    }

  private:
    void visit(visage::Frame& parent, bool visible) {
        for (auto* frame : parent.children()) {
            const bool shown = visible && frame->isVisible();
            auto it = controls_.find(frame);
            if (it == controls_.end() && shown) {
                auto* value = dynamic_cast<hba::ui::ParameterControl*>(frame);
                if (value || dynamic_cast<hba::ui::ActionControl*>(frame) || dynamic_cast<visage::Button*>(frame)) {
                    auto* parameter = value ? findParameter(value->parameterKey()) : nullptr;
                    jassert(value == nullptr || parameter != nullptr);
                    if (value == nullptr || parameter != nullptr) {
                        it = controls_.emplace(frame, std::make_unique<AccessibleControl>(*frame, parameter)).first;
                        addChildComponent(*it->second);
                    }
                }
            }
            if (it != controls_.end()) {
                it->second->setVisible(shown);
                if (shown)
                    it->second->sync(controlBounds(*frame, root_, getWidth()));
            }
            visit(*frame, shown);
        }
    }

    juce::RangedAudioParameter* findParameter(std::string_view key) const {
        const auto id = juce::String::fromUTF8(key.data(), static_cast<int>(key.size()));
        for (auto* item : processor_.getParameters())
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(item); p && p->paramID == id)
                return p;
        return nullptr;
    }
    visage::Frame& root_;
    juce::AudioProcessor& processor_;
    std::map<visage::Frame*, std::unique_ptr<AccessibleControl>> controls_;
    bool syncing_ = false;
};

} // namespace hba::host
