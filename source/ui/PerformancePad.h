#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ContextMenu.h"

namespace astralay::ui
{

/** The tap a key press names: 1 to 9 and 0 are taps 1 to 10, and Shift+1 to Shift+6 are taps 11
    to 16. Returns its zero-based index, or -1 if the key isn't a tap key.
*/
int tapIndexForKey (const juce::KeyPress& key);

/** The performance area: one focusable control whose keys play the plugin rather than edit it.

    Number keys choose the taps it acts on (Backspace switches between all and none, Shift+Backspace
    between even and odd), the up and down arrows lengthen and shorten those taps' times, the left
    and right arrows change the smear size (the smear amount with Shift), F freezes while held and
    Shift+F switches freeze on or off. Changes to the selection are announced; the arrows and the
    held freeze are silent.

    It shows a cell per tap, which can also be clicked, and whether freeze is on. It owns no plugin
    state: the owner supplies the selection and carries out what the callbacks ask for.
*/
class PerformancePad final : public MenuControl<juce::Component>,
                             private juce::Timer
{
public:
    PerformancePad();
    ~PerformancePad() override;

    /** Called when the user changes the selection, with a bit per tap. */
    std::function<void (juce::uint32)> onSelectionChanged;

    /** Called for each arrow press: 1 for up, -1 for down. continuing is true for the repeats of
        a held key.
    */
    std::function<void (int direction, bool continuing)> onStepTimes;

    /** Called for each left or right arrow press: 1 for right, -1 for left. amount is true with
        Shift, for the smear amount rather than its size.
    */
    std::function<void (bool amount, int direction, bool continuing)> onStepSmear;

    /** Called with true when F goes down and false when it is released or focus leaves. */
    std::function<void (bool held)> onHoldFreeze;

    std::function<void()> onToggleFreeze;

    void setSelection (juce::uint32 newSelection);
    juce::uint32 getSelection() const noexcept { return selection; }

    void setTapEnabled (int tapIndex, bool enabled);
    void setFrozen (bool shouldBeFrozen);

    /** Lets go of a held freeze. */
    void releaseHeldKeys();

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void changeSelection (juce::uint32 newSelection, const juce::String& announcement);
    juce::Rectangle<int> cellArea() const;
    void updateHeldKeys();
    void timerCallback() override;

    juce::uint32 selection = 0;
    juce::uint32 enabledTaps = 0;
    bool frozen = false;

    // Key codes of the keys being held, or 0.
    int heldArrow = 0;
    bool heldArrowShifted = false;
    int heldFreezeKey = 0;
    bool holdingFreeze = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerformancePad)
};

} // namespace astralay::ui
