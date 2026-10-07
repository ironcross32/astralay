#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace astralay::ui
{

/** Whether controls give screen readers their help tags. One switch for every editor in the
    process, which the editor sets from the settings file. Screen readers ask for help each time
    they reach a control, so a change is heard from the next control on. Tooltips show either way.
*/
bool helpTagsEnabled() noexcept;
void setHelpTagsEnabled (bool shouldBeEnabled) noexcept;

/** The text to give a screen reader as a control's help tag: nothing while help tags are off. */
juce::String helpTag (const juce::String& help);

/** A tooltip as a control should report it from getTooltip(). JUCE's handlers for buttons, combo
    boxes and labels read the tooltip out as the help tag, so this is empty while help tags are
    off, except to a TooltipWindow that is about to show it.
*/
juce::String tooltipAsHelpTag (const juce::String& tooltip);

/** Shows tooltips whether or not help tags are on. */
class TooltipWindow final : public juce::TooltipWindow
{
public:
    using juce::TooltipWindow::TooltipWindow;

    juce::String getTipFor (juce::Component&) override;
};

/** An accessibility handler that leaves out the help tag while help tags are off. */
class HelpTagHandler : public juce::AccessibilityHandler
{
public:
    using juce::AccessibilityHandler::AccessibilityHandler;

    juce::String getHelp() const override { return helpTag (juce::AccessibilityHandler::getHelp()); }
};

} // namespace astralay::ui
