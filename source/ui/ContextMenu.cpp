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

    // Queued behind the control's name and value rather than cutting them off.
    announcer.announce (message(), false);
}

} // namespace astralay::ui
