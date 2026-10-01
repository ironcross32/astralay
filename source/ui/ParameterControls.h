#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Steps.h"

namespace astralay::ui
{

/** A slider bound to one plugin parameter at a time, rebindable when the selected tap or the
    host sync setting changes.

    Keyboard, per the spec: arrows step by a unit-appropriate amount (Shift fine, Ctrl or Cmd
    coarse), Home and End go to the maximum and minimum, Delete resets to the
    default, and Enter opens a type-in field.
*/
class ParameterSlider final : public juce::Slider
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

    /** Opens the type-in field over the slider. */
    void showTypeIn();

    juce::String getTextFromValue (double value) override;
    bool keyPressed (const juce::KeyPress&) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class AccessibilityHandler;
    class TypeIn;

    void setFromUser (double newValue);
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
    std::unique_ptr<TypeIn> typeIn;
    bool updatingFromParameter = false;

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
