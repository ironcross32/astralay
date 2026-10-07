#include "AccessibilitySettings.h"
#include "ParameterControls.h"
#include "Theme.h"

namespace astralay::ui
{

AccessibilitySettings::AccessibilitySettings (bool helpTagsOn)
{
    // Tab stays among the controls in here.
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    addAndMakeVisible (panel);

    describe (helpTags, "Help tags", "When on, screen readers read a short description of each control after its name.");
    helpTags.setToggleState (helpTagsOn, juce::dontSendNotification);
    helpTags.onClick = [this]
    {
        if (onHelpTagsChanged != nullptr)
            onHelpTagsChanged (helpTags.getToggleState());
    };

    describe (closeButton, "Close", "Closes the accessibility settings.");
    closeButton.setWantsKeyboardFocus (true);
    closeButton.onClick = [this]
    {
        if (onClose != nullptr)
            onClose();
    };

    panel.addInOrder (helpTags);
    panel.addInOrder (closeButton);
}

void AccessibilitySettings::focusFirstControl()
{
    helpTags.grabKeyboardFocus();
}

bool AccessibilitySettings::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (onClose != nullptr)
            onClose();

        return true;
    }

    if (key.isKeyCode (juce::KeyPress::tabKey))
    {
        auto* focused = getCurrentlyFocusedComponent();

        if (focused != nullptr && isParentOf (focused))
            focused->moveKeyboardFocusToSibling (! key.getModifiers().isShiftDown());
        else
            focusFirstControl();

        return true;
    }

    return false;
}

void AccessibilitySettings::paint (juce::Graphics& g)
{
    g.fillAll (colours::background.withAlpha (0.8f));
}

void AccessibilitySettings::resized()
{
    constexpr int rowHeight = 32, gap = 10;

    panel.setBounds (getLocalBounds().withSizeKeepingCentre (460, 8 + (int) sizes::headingHeight + 6 + 2 * rowHeight + gap + 8));

    auto area = panel.getContentBounds();
    helpTags.setBounds (area.removeFromTop (rowHeight));
    area.removeFromTop (gap);
    closeButton.setBounds (area.removeFromTop (rowHeight).removeFromLeft (110));
}

} // namespace astralay::ui
