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
        : juce::AccessibilityHandler (s, juce::AccessibilityRole::slider, actionsFor (s),
                                      Interfaces { std::make_unique<Value> (s) }),
          slider (s)
    {
    }

    juce::String getHelp() const override { return slider.getHelpText(); }

private:
    static juce::AccessibilityActions actionsFor (ParameterSlider& s)
    {
        juce::AccessibilityActions actions;

        if (s.hasContextMenu())
            actions.addAction (juce::AccessibilityActionType::showMenu, [&s] { s.showContextMenu(); });

        return actions;
    }

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
TypeInField::TypeInField (const juce::String& title, const juce::String& help, const juce::String& text, float fontHeight)
{
    setTitle (title);
    setHelpText (help);
    setFont (juce::FontOptions (fontHeight));
    setJustification (juce::Justification::centredLeft);
    setSelectAllWhenFocused (true);
    setText (text, false);

    const auto close = [this] (bool refocus)
    {
        if (! dismissed && onClose != nullptr)
            onClose (refocus);
    };

    onReturnKey = [this, close]
    {
        if (dismissed)
            return;

        const auto problem = onCommit != nullptr ? onCommit (getText()) : juce::String();

        if (problem.isEmpty())
        {
            close (true);
            return;
        }

        // Select the rejected text: typing replaces it, or the arrow keys unselect it for fixing.
        selectAll();
        announceFrom (*this, problem);
    };

    onEscapeKey = [close] { close (true); };
    onFocusLost = [close] { close (false); };
}

std::unique_ptr<TypeInField> TypeInField::show (juce::Component& target, const juce::String& title,
                                                const juce::String& help, const juce::String& text)
{
    auto* top = target.getTopLevelComponent();

    if (top == nullptr)
        return nullptr;

    const auto area = top->getLocalArea (&target, target.getLocalBounds());
    const auto scale = target.getHeight() > 0 ? (float) area.getHeight() / (float) target.getHeight() : 1.0f;

    std::unique_ptr<TypeInField> field (new TypeInField (title, help, text, sizes::textHeight * scale));
    top->addAndMakeVisible (*field);
    field->setBounds (area);
    field->grabKeyboardFocus();
    return field;
}

void TypeInField::dismiss (std::unique_ptr<TypeInField>& field)
{
    if (field == nullptr)
        return;

    // Hiding it takes focus away, which must not close it a second time.
    field->dismissed = true;
    field->setVisible (false);

    if (auto* parent = field->getParentComponent())
        parent->removeChildComponent (field.get());

    // This can run inside the text editor's own callbacks, so delete it once they have returned.
    juce::MessageManager::callAsync ([doomed = std::shared_ptr<TypeInField> (field.release())] {});
}

juce::String formatModulationPercent (double percent)
{
    auto text = juce::String (percent, 2);

    if (text.containsChar ('.'))
        text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

    return (text == "-0" ? juce::String ("0") : text) + "%";
}

juce::String formatModulation (const juce::RangedAudioParameter& parameter, double amount)
{
    const auto value = (float) amount;
    const auto unit = params::unitForId (parameter.paramID);

    switch (unit)
    {
        case Unit::pan:
        {
            // The direction the control moves in, rather than a position.
            const auto distance = juce::roundToInt (std::abs (value));
            return distance == 0 ? juce::String ("0") : juce::String (distance) + (value < 0.0f ? " left" : " right");
        }

        // Already signed. An amount in decibels has no floor to show as -inf.
        case Unit::semitones:   return Units::format (unit, value);
        case Unit::decibels:    return Units::format (unit, value, -1.0e9f);

        case Unit::milliseconds:
        case Unit::percent:
        case Unit::hertz:
        case Unit::semitonesPerPass:
        case Unit::ratio:
        case Unit::index:
        case Unit::multiplier:
        case Unit::bits:
        case Unit::grainsPerSecond:
        case Unit::chunks:
        case Unit::macro:
        case Unit::plain:
            break;
    }

    // The unit's own formatting picks its precision from the size of the value, so it is given
    // the size and the sign goes in front.
    const auto text = Units::format (unit, std::abs (value));
    return value < 0.0f && Units::format (unit, 0.0f) != text ? "-" + text : text;
}

std::optional<double> parseModulationPercent (const juce::String& text)
{
    if (const auto parsed = Units::parse (Unit::percent, text))
        return (double) *parsed;

    return std::nullopt;
}

std::optional<double> parseModulation (const juce::RangedAudioParameter& parameter, const juce::String& text)
{
    // For volume, "-inf" is the whole range downwards.
    if (const auto parsed = Units::parse (params::unitForId (parameter.paramID), text, (float) -modulationLimit (parameter)))
        return (double) *parsed;

    return std::nullopt;
}

double modulationLimit (const juce::RangedAudioParameter& parameter)
{
    const auto& range = parameter.getNormalisableRange();
    return (double) range.end - (double) range.start;
}

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
    modulationChanged = nullptr;
    setColour (juce::Slider::trackColourId, colours::sliderFill);
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
        notifyValueChanged();
}

void ParameterSlider::editModulation (const juce::String& macroName, double amount, std::function<void (double)> onChange,
                                      bool asPercentage)
{
    if (parameter == nullptr)
        return;

    closeTypeIn (false);
    attachment.reset();
    modulationChanged = std::move (onChange);
    modulationIsPercentage = asPercentage;
    setColour (juce::Slider::trackColourId, colours::modulationFill);

    // Up to the width of the parameter's range either way, in its unit and on its own steps.
    const auto limit = asPercentage ? 100.0 : modulationLimit (*parameter);
    const auto whole = steps.isInteger && ! asPercentage;
    setNormalisableRange (juce::NormalisableRange<double> (-limit, limit, whole ? 1.0 : 0.0));

    if (asPercentage)
    {
        steps = {};
        steps.unit = Unit::percent;
    }

    steps.minimum = -limit;
    steps.maximum = limit;
    steps.isInteger = whole;
    steps.isOffset = true;

    describe (*this, parameter->getName (128) + ", " + macroName + " amount", helpFor (helpKeys::modulationAmount));
    setDoubleClickReturnValue (true, 0.0);

    {
        const juce::ScopedValueSetter<bool> updating (updatingFromParameter, true);
        setValue (amount, juce::dontSendNotification);
    }

    updateText();

    if (hasKeyboardFocus (false))
        notifyValueChanged();
}

void ParameterSlider::setModulationAmount (double amount)
{
    if (! isEditingModulation() || std::abs (amount - getValue()) < 1.0e-4)
        return;

    setValue (amount, juce::dontSendNotification);

    if (hasKeyboardFocus (false))
        notifyValueChanged();
}

juce::String ParameterSlider::getTextFromValue (double value)
{
    if (parameter == nullptr)
        return Slider::getTextFromValue (value);

    if (isEditingModulation())
        return modulationIsPercentage ? formatModulationPercent (value) : formatModulation (*parameter, value);

    return parameter->getText (parameter->convertTo0to1 ((float) value), 0);
}

void ParameterSlider::notifyValueChanged()
{
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void ParameterSlider::setFromUser (double newValue)
{
    const auto limited = juce::jlimit (getMinimum(), getMaximum(), newValue);

    if (isEditingModulation())
    {
        setValue (limited, juce::dontSendNotification);
        modulationChanged (limited);
    }
    else if (attachment != nullptr)
    {
        attachment->setValueAsCompleteGesture ((float) limited);
    }
    else
    {
        return;
    }

    notifyValueChanged();
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

    // Not Backspace, which the editor uses to turn the selected tap on or off.
    if (code == juce::KeyPress::deleteKey)
    {
        // A modulation's default is none at all.
        setFromUser (isEditingModulation() ? 0.0 : (double) parameter->convertFrom0to1 (parameter->getDefaultValue()));
        return true;
    }

    if (code == juce::KeyPress::returnKey)
    {
        showTypeIn();
        return true;
    }

    return false;
}

void ParameterSlider::setContextMenu (std::function<void()> show)
{
    showMenu = std::move (show);

    // The handler's actions are fixed when it is made.
    invalidateAccessibilityHandler();
}

void ParameterSlider::showContextMenu()
{
    if (showMenu != nullptr)
        showMenu();
}

void ParameterSlider::mouseDown (const juce::MouseEvent& e)
{
    // A right-click opens the menu, and the rest of that click is kept from dragging the slider.
    menuClick = e.mods.isPopupMenu() && hasContextMenu();

    if (menuClick)
        showContextMenu();
    else
        Slider::mouseDown (e);
}

void ParameterSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! menuClick)
        Slider::mouseDrag (e);
}

void ParameterSlider::mouseUp (const juce::MouseEvent& e)
{
    if (! menuClick)
        Slider::mouseUp (e);

    menuClick = false;
}

void ParameterSlider::showTypeIn()
{
    if (typeIn != nullptr || parameter == nullptr)
        return;

    typeIn = TypeInField::show (*this, getTitle() + ", type a value",
                                "Type a value and press Enter, or press Escape to cancel. Accepted range: " + describeRange() + ".",
                                getTextFromValue (getValue()));

    if (typeIn == nullptr)
        return;

    typeIn->onCommit = [this] (const juce::String& text) { return commitTypedText (text); };
    typeIn->onClose = [this] (bool refocus) { closeTypeIn (refocus); };
}

void ParameterSlider::closeTypeIn (bool refocus)
{
    if (typeIn == nullptr)
        return;

    TypeInField::dismiss (typeIn);

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

    if (isEditingModulation())
    {
        const auto parsed = modulationIsPercentage ? parseModulationPercent (text) : parseModulation (*parameter, text);

        if (! parsed.has_value())
            return "Not understood. Enter a value from " + describeRange() + ".";

        value = steps.isInteger ? std::round (*parsed) : *parsed;
    }
    else if (steps.isNoteValue)
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
    if (updatingFromParameter)
        return;

    if (isEditingModulation())
        modulationChanged (getValue());
    else if (attachment != nullptr)
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
