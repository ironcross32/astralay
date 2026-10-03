#include "ContextMenu.h"
#include "Announcer.h"

namespace astralay::ui
{

ContextMenuTarget* findContextMenu (juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
        if (auto* target = dynamic_cast<ContextMenuTarget*> (c); target != nullptr && target->hasContextMenu())
            return target;

    return nullptr;
}

//==============================================================================
ContextMenuHint::ContextMenuHint (Announcer& announcerToUse)
    : announcer (announcerToUse)
{
}

void ContextMenuHint::focusChanged (juce::Component* focused)
{
    // Focus leaves the editor while one of its menus is open and comes back to the same control
    // when it closes. That isn't moving to the control, so it gets no hint, and neither does
    // coming back from another window.
    if (focused == nullptr)
    {
        stopTimer();
        return;
    }

    if (focused == lastFocused.getComponent())
        return;

    lastFocused = focused;

    // The screen reader drops pending speech when focus moves, so wait until it has started on
    // the control. Moving on before then cancels the hint.
    if (findContextMenu (focused) != nullptr)
        startTimer (250);
    else
        stopTimer();
}

void ContextMenuHint::timerCallback()
{
    stopTimer();

    // The menu was opened before the hint was due.
    if (juce::Component::getCurrentlyModalComponent() != nullptr)
        return;

    // Queued behind the control's name and value rather than cutting them off.
    announcer.announce (message(), false);
}

} // namespace astralay::ui
