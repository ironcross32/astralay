#include "HelpTags.h"

namespace astralay::ui
{

namespace
{
    std::atomic<bool> enabled { true };

    // Only touched on the message thread, where both tooltips and accessibility are handled.
    bool showingTooltip = false;
}

bool helpTagsEnabled() noexcept                         { return enabled.load(); }
void setHelpTagsEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled); }

juce::String helpTag (const juce::String& help)
{
    return helpTagsEnabled() ? help : juce::String();
}

juce::String tooltipAsHelpTag (const juce::String& tooltip)
{
    return showingTooltip ? tooltip : helpTag (tooltip);
}

juce::String TooltipWindow::getTipFor (juce::Component& c)
{
    const juce::ScopedValueSetter<bool> scope (showingTooltip, true);
    return juce::TooltipWindow::getTipFor (c);
}

} // namespace astralay::ui
