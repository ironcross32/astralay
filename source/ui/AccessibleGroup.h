#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ContextMenu.h"
#include "Theme.h"

namespace astralay::ui
{

/** A titled group of controls. Screen readers see it as a group (VoiceOver users interact with it
    to reach the controls inside), while Tab moves through it without stopping, since it is a focus
    container but not a keyboard focus container.

    Children are placed in focus order in the order they are added with addInOrder().

    A group can have a context menu, which then also opens from the controls inside it that have
    none of their own.
*/
class AccessibleGroup final : public juce::Component,
                              public ContextMenuTarget
{
public:
    explicit AccessibleGroup (const juce::String& title, bool drawFrame = true)
        : frame (drawFrame)
    {
        setTitle (title);
        setFocusContainerType (FocusContainerType::focusContainer);
        setAccessible (true);
    }

    /** Adds a child and gives it the next place in the focus order. */
    void addInOrder (juce::Component& child)
    {
        child.setExplicitFocusOrder (++lastFocusOrder);
        addAndMakeVisible (child);
    }

    /** The area inside the frame and heading, for laying out children. */
    juce::Rectangle<int> getContentBounds() const
    {
        return frame ? getLocalBounds().reduced (8).withTrimmedTop ((int) sizes::headingHeight + 6)
                     : getLocalBounds();
    }

    /** Gives the group a context menu: show is called to open it. */
    void setContextMenu (std::function<void()> show)
    {
        showMenu = std::move (show);

        // The handler's actions are fixed when it is made.
        invalidateAccessibilityHandler();
    }

    bool hasContextMenu() const override { return showMenu != nullptr; }

    void showContextMenu() override
    {
        if (showMenu != nullptr)
            showMenu();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            showContextMenu();
    }

    void paint (juce::Graphics& g) override
    {
        if (! frame)
            return;

        g.setColour (colours::panel);
        g.fillRect (getLocalBounds());
        g.setColour (colours::outline);
        g.drawRect (getLocalBounds(), 1);
        g.setColour (colours::text);
        g.setFont (juce::Font (juce::FontOptions (sizes::headingHeight, juce::Font::bold)));
        g.drawText (getTitle(), getLocalBounds().reduced (8, 4).withHeight ((int) sizes::headingHeight + 2),
                    juce::Justification::centredLeft);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        juce::AccessibilityActions actions;

        if (hasContextMenu())
            actions.addAction (juce::AccessibilityActionType::showMenu, [this] { showContextMenu(); });

        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group, actions);
    }

private:
    bool frame;
    int lastFocusOrder = 0;
    std::function<void()> showMenu;
};

} // namespace astralay::ui
