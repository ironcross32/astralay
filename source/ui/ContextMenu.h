#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace astralay::ui
{

class Announcer;

/** Implemented by controls that can have a context menu. The editor opens the focused control's
    menu when the right bracket key is pressed; each control opens its own on a right-click and
    on the screen reader's show-menu action (VO+Shift+M in VoiceOver).
*/
class ContextMenuTarget
{
public:
    virtual ~ContextMenuTarget() = default;

    virtual bool hasContextMenu() const = 0;
    virtual void showContextMenu() = 0;
};

/** The context menu that opens from c: its own, or that of the nearest control around it that has
    one. Returns nullptr if there is none.
*/
ContextMenuTarget* findContextMenu (juce::Component* c);

/** Tells screen reader users that the control they have moved to has a context menu, by speaking
    "has context menu" after the screen reader has read the control. It doesn't say what is in the
    menu. Focus coming back to the same control, as it does when a menu closes, isn't hinted again.
*/
class ContextMenuHint final : private juce::Timer
{
public:
    explicit ContextMenuHint (Announcer& announcerToUse);

    /** Call when focus moves, with the newly focused component or nullptr if there is none. */
    void focusChanged (juce::Component* focused);

    static juce::String message() { return "has context menu"; }

    /** True while a hint is waiting to be spoken. */
    bool isPending() const { return isTimerRunning(); }

private:
    void timerCallback() override;

    Announcer& announcer;
    juce::Component::SafePointer<juce::Component> lastFocused;
};

} // namespace astralay::ui
