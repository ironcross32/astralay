#include "ParameterControls.h"
#include "Announcer.h"
#include "HelpText.h"
#include "Theme.h"
#include "params/NoteValues.h"
#include "params/Parameters.h"

namespace astralay::ui
{

void describe (juce::Component& component, const juce::String& title, const juce::String& help)
{
    component.setTitle (title);
    component.setHelpText (help);

    if (auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*> (&component))
        tooltipClient->setTooltip (help);
}

//==============================================================================
class ParameterSlider::AccessibilityHandler final : public juce::AccessibilityHandler
{
public:
    explicit AccessibilityHandler (ParameterSlider& s)
        : juce::AccessibilityHandler (s, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
                                      Interfaces { std::make_unique<Value> (s) }),
          slider (s)
    {
    }

    juce::String getHelp() const override { return slider.getHelpText(); }

private:
    class Value final : public juce::AccessibilityValueInterface
    {
    public:
        explicit Value (ParameterSlider& s) : slider (s) {}

        bool isReadOnly() const override { return slider.parameter == nullptr; }
        double getCurrentValue() const override { return slider.getValue(); }
        void setValue (double newValue) override { slider.setFromUser (newValue); }
        juce::String getCurrentValueAsString() const override { return slider.getTextFromValue (slider.getValue()); }
        void setValueAsString (const juce::String& text) override { slider.commitTypedText (text); }

        AccessibleValueRange getRange() const override
        {
            return { { slider.getMinimum(), slider.getMaximum() }, normalStepSize (slider.steps, slider.getValue()) };
        }

    private:
        ParameterSlider& slider;
    };

    ParameterSlider& slider;
};

//==============================================================================
class ParameterSlider::TypeIn final : public juce::TextEditor
{
public:
    TypeIn (ParameterSlider& ownerToUse, float fontHeight)
        : owner (ownerToUse)
    {
        setTitle (owner.getTitle() + ", type a value");
        setHelpText ("Type a value and press Enter, or press Escape to cancel. Accepted range: " + owner.describeRange() + ".");
        setFont (juce::FontOptions (fontHeight));
        setJustification (juce::Justification::centredLeft);
        setSelectAllWhenFocused (true);
        setText (owner.getTextFromValue (owner.getValue()), false);

        onReturnKey = [this]
        {
            const auto problem = owner.commitTypedText (getText());

            if (problem.isEmpty())
            {
                owner.closeTypeIn (true);
                return;
            }

            // Select the rejected text: typing replaces it, or the arrow keys unselect it for fixing.
            selectAll();
            announceFrom (*this, problem);
        };

        onEscapeKey = [this] { owner.closeTypeIn (true); };
        onFocusLost = [this] { owner.closeTypeIn (false); };
    }

    void detachCallbacks()
    {
        onReturnKey = nullptr;
        onEscapeKey = nullptr;
        onFocusLost = nullptr;
    }

private:
    ParameterSlider& owner;
};

//==============================================================================
ParameterSlider::ParameterSlider()
{
    setSliderStyle (juce::Slider::LinearBar);
    setTextBoxStyle (juce::Slider::TextBoxLeft, true, 0, 0);
    setWantsKeyboardFocus (true);
    setScrollWheelEnabled (true);
}

ParameterSlider::~ParameterSlider()
{
    closeTypeIn (false);
}

void ParameterSlider::bind (juce::RangedAudioParameter& newParameter, Unit unit, const juce::String& helpKey,
                            bool isNoteValue, int firstNoteIndex)
{
    closeTypeIn (false);
    attachment.reset();
    parameter = &newParameter;

    const auto& range = newParameter.getNormalisableRange();
    juce::NormalisableRange<double> sliderRange (range.start, range.end, range.interval, range.skew, range.symmetricSkew);
    setNormalisableRange (sliderRange);

    steps = {};
    steps.unit = unit;
    steps.minimum = range.start;
    steps.maximum = range.end;
    steps.isInteger = dynamic_cast<juce::AudioParameterInt*> (&newParameter) != nullptr;
    steps.isNoteValue = isNoteValue;
    steps.firstNoteIndex = firstNoteIndex;

    describe (*this, newParameter.getName (128), helpFor (helpKey));
    setDoubleClickReturnValue (true, range.convertFrom0to1 (newParameter.getDefaultValue()));

    attachment = std::make_unique<juce::ParameterAttachment> (newParameter, [this] (float value)
    {
        const juce::ScopedValueSetter<bool> updating (updatingFromParameter, true);
        setValue (value, juce::dontSendNotification);
    });

    attachment->sendInitialUpdate();
    updateText();

    // Only the focused control needs to tell the screen reader its value changed. Switching taps
    // rebinds dozens of sliders, and sending an event for each one makes the switch lag while the
    // screen reader processes them.
    if (hasKeyboardFocus (false))
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

juce::String ParameterSlider::getTextFromValue (double value)
{
    if (parameter == nullptr)
        return Slider::getTextFromValue (value);

    return parameter->getText (parameter->convertTo0to1 ((float) value), 0);
}

void ParameterSlider::setFromUser (double newValue)
{
    if (attachment == nullptr)
        return;

    attachment->setValueAsCompleteGesture ((float) juce::jlimit (getMinimum(), getMaximum(), newValue));

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

bool ParameterSlider::keyPressed (const juce::KeyPress& key)
{
    if (parameter == nullptr)
        return false;

    const auto mods = key.getModifiers();

    // Alt/Option combinations belong to the editor (group navigation).
    if (mods.isAltDown())
        return false;

    const auto code = key.getKeyCode();
    const auto size = mods.isShiftDown() ? StepSize::fine
                    : mods.isCommandDown() ? StepSize::coarse
                    : StepSize::normal;

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
    {
        setFromUser (stepValue (steps, getValue(), 1, size));
        return true;
    }

    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
    {
        setFromUser (stepValue (steps, getValue(), -1, size));
        return true;
    }

    if (mods.isCommandDown() || mods.isShiftDown())
        return false;

    if (code == juce::KeyPress::homeKey)
    {
        setFromUser (getMaximum());
        return true;
    }

    if (code == juce::KeyPress::endKey)
    {
        setFromUser (getMinimum());
        return true;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        setFromUser (parameter->convertFrom0to1 (parameter->getDefaultValue()));
        return true;
    }

    if (code == juce::KeyPress::returnKey)
    {
        showTypeIn();
        return true;
    }

    return false;
}

void ParameterSlider::showTypeIn()
{
    if (typeIn != nullptr || parameter == nullptr)
        return;

    auto* top = getTopLevelComponent();

    if (top == nullptr)
        return;

    const auto area = top->getLocalArea (this, getLocalBounds());
    const auto scale = getHeight() > 0 ? (float) area.getHeight() / (float) getHeight() : 1.0f;

    typeIn = std::make_unique<TypeIn> (*this, sizes::textHeight * scale);
    top->addAndMakeVisible (*typeIn);
    typeIn->setBounds (area);
    typeIn->grabKeyboardFocus();
}

void ParameterSlider::closeTypeIn (bool refocus)
{
    if (typeIn == nullptr)
        return;

    typeIn->detachCallbacks();
    typeIn->setVisible (false);

    if (auto* parent = typeIn->getParentComponent())
        parent->removeChildComponent (typeIn.get());

    // This can run inside the text editor's own callbacks, so delete it once they have returned.
    juce::MessageManager::callAsync ([doomed = std::shared_ptr<TypeIn> (typeIn.release())] {});

    if (refocus)
        grabKeyboardFocus();
}

juce::String ParameterSlider::describeRange() const
{
    auto& self = const_cast<ParameterSlider&> (*this);
    return self.getTextFromValue (getMinimum()) + " to " + self.getTextFromValue (getMaximum());
}

juce::String ParameterSlider::commitTypedText (const juce::String& text)
{
    if (parameter == nullptr)
        return "Nothing to set.";

    if (text.trim().isEmpty())
        return "Enter a value from " + describeRange() + ".";

    double value;

    if (steps.isNoteValue)
    {
        const auto index = NoteValues::parse (text);

        if (index < 0)
            return "Not a note value. Enter a value from " + describeRange() + ".";

        value = index - steps.firstNoteIndex;
    }
    else
    {
        const auto parsed = Units::parse (steps.unit, text, params::volumeFloorDb);

        if (! parsed.has_value())
            return "Not understood. Enter a value from " + describeRange() + ".";

        value = steps.isInteger ? std::round (*parsed) : (double) *parsed;
    }

    const auto tolerance = (getMaximum() - getMinimum()) * 1.0e-6;

    if (value < getMinimum() - tolerance || value > getMaximum() + tolerance)
        return "Out of range, " + describeRange() + ".";

    setFromUser (value);
    return {};
}

void ParameterSlider::valueChanged()
{
    if (! updatingFromParameter && attachment != nullptr)
        attachment->setValueAsPartOfGesture ((float) getValue());
}

void ParameterSlider::startedDragging()
{
    if (attachment != nullptr)
        attachment->beginGesture();
}

void ParameterSlider::stoppedDragging()
{
    if (attachment != nullptr)
        attachment->endGesture();
}

std::unique_ptr<juce::AccessibilityHandler> ParameterSlider::createAccessibilityHandler()
{
    return std::make_unique<AccessibilityHandler> (*this);
}

//==============================================================================
ParameterToggle::ParameterToggle (const juce::String& visibleLabel)
{
    setButtonText (visibleLabel);
    setWantsKeyboardFocus (true);
}

void ParameterToggle::bind (juce::RangedAudioParameter& parameter, const juce::String& helpKey)
{
    attachment.reset();
    describe (*this, parameter.getName (128), helpFor (helpKey));
    attachment = std::make_unique<juce::ButtonParameterAttachment> (parameter, *this);
}

//==============================================================================
void ParameterChoice::bind (juce::RangedAudioParameter& parameter, const juce::String& helpKey)
{
    attachment.reset();
    clear (juce::dontSendNotification);
    addItemList (parameter.getAllValueStrings(), 1);
    describe (*this, parameter.getName (128), helpFor (helpKey));
    attachment = std::make_unique<juce::ComboBoxParameterAttachment> (parameter, *this);
}

} // namespace astralay::ui
