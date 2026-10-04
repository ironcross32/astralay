#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ContextMenu.h"
#include "Steps.h"

namespace astralay::ui
{

/** A text field shown over a control for typing a value or a name. Enter passes the text to
    onCommit, which returns an empty string to accept it, or a message to announce, in which case
    the field stays open with the rejected text selected. Accepting, Escape and losing focus all
    end with onClose, which is told whether focus should go back to where it was.
*/
class TypeInField final : public juce::TextEditor
{
public:
    /** Opens a field over target and gives it focus. Returns nullptr if target isn't in a window. */
    static std::unique_ptr<TypeInField> show (juce::Component& target, const juce::String& title,
                                              const juce::String& help, const juce::String& text);

    /** Takes a field down. Safe to call from the field's own callbacks. */
    static void dismiss (std::unique_ptr<TypeInField>& field);

    std::function<juce::String (const juce::String&)> onCommit;
    std::function<void (bool refocus)> onClose;

private:
    TypeInField (const juce::String& title, const juce::String& help, const juce::String& text, float fontHeight);

    bool dismissed = false;
};

/** The text for how far a macro moves a parameter: an amount in the parameter's own unit, such
    as "-200 ms", "25%" or "30 left".
*/
juce::String formatModulation (const juce::RangedAudioParameter& parameter, double amount);

/** The same for a note value that is standing in for a time while host sync is on. Its amount is
    shown as a percentage of the range, such as "71.43%", since there is no such thing as part of
    a note value to move it by.
*/
juce::String formatModulationPercent (double percent);
std::optional<double> parseModulationPercent (const juce::String& text);

/** Reads an amount typed as formatModulation writes it, with the unit optional. Returns nothing
    if the text isn't understood. Doesn't check the amount against any range.
*/
std::optional<double> parseModulation (const juce::RangedAudioParameter& parameter, const juce::String& text);

/** The largest amount a macro can move a parameter by, either way: the width of its range. */
double modulationLimit (const juce::RangedAudioParameter& parameter);

/** A slider bound to one plugin parameter at a time, rebindable when the selected tap or the
    host sync setting changes.

    Keyboard, per the spec: arrows step by a unit-appropriate amount (Shift fine, Ctrl or Cmd
    coarse), Home and End go to the maximum and minimum, Delete resets to the
    default, and Enter opens a type-in field.

    While a macro is armed the slider sets how far that macro moves the parameter instead, in the
    parameter's own unit and by up to the width of its range either way, with the same keys. Its
    default is 0.
*/
class ParameterSlider final : public juce::Slider,
                              public ContextMenuTarget
{
public:
    ParameterSlider();
    ~ParameterSlider() override;

    /** Binds to a parameter. helpKey selects the help text. For note-value choice parameters,
        firstNoteIndex is the index into NoteValues::all() of the parameter's first choice.
    */
    void bind (juce::RangedAudioParameter& parameter, Unit unit, const juce::String& helpKey,
               bool isNoteValue = false, int firstNoteIndex = 0);

    juce::RangedAudioParameter* getParameter() const noexcept { return parameter; }

    /** Switches the bound slider to setting how far a macro moves its parameter, until the next
        bind(). The amount is in the parameter's unit, and so is the value onChange is given; or
        with asPercentage, both are a percentage from -100 to 100.
    */
    void editModulation (const juce::String& macroName, double amount, std::function<void (double)> onChange,
                         bool asPercentage = false);

    bool isEditingModulation() const noexcept { return modulationChanged != nullptr; }

    /** Shows a modulation amount that was changed from somewhere else. */
    void setModulationAmount (double amount);

    /** Opens the type-in field over the slider. */
    void showTypeIn();

    /** Gives the slider a context menu: show is called to open it. */
    void setContextMenu (std::function<void()> show);

    bool hasContextMenu() const override { return showMenu != nullptr; }
    void showContextMenu() override;

    juce::String getTextFromValue (double value) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class AccessibilityHandler;

    void setFromUser (double newValue);
    void notifyValueChanged();
    void closeTypeIn (bool refocus);

    /** Checks typed text; on success sets the value and returns an empty string, otherwise returns
        the message to announce.
    */
    juce::String commitTypedText (const juce::String& text);

    juce::String describeRange() const;

    void valueChanged() override;
    void startedDragging() override;
    void stoppedDragging() override;

    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    StepContext steps;
    std::unique_ptr<TypeInField> typeIn;
    bool updatingFromParameter = false;
    std::function<void (double)> modulationChanged;
    bool modulationIsPercentage = false;
    std::function<void()> showMenu;
    bool menuClick = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterSlider)
};

/** An on/off button bound to a bool parameter, rebindable. */
class ParameterToggle final : public juce::ToggleButton
{
public:
    explicit ParameterToggle (const juce::String& visibleLabel);

    void bind (juce::RangedAudioParameter& parameter, const juce::String& helpKey);

private:
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

/** A drop-down list bound to a choice parameter. */
class ParameterChoice final : public juce::ComboBox
{
public:
    void bind (juce::RangedAudioParameter& parameter, const juce::String& helpKey);

private:
    std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
};

/** Sets a control's accessible title and help text (as both help text and tooltip, since JUCE's
    handlers for sliders, combo boxes and labels report the tooltip as help).
*/
void describe (juce::Component& component, const juce::String& title, const juce::String& help);

} // namespace astralay::ui
