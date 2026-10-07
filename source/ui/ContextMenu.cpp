#include "ContextMenu.h"

namespace astralay::ui
{

ContextMenuTarget* findContextMenu (juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
        if (auto* target = dynamic_cast<ContextMenuTarget*> (c); target != nullptr && target->hasContextMenu())
            return target;

    return nullptr;
}

} // namespace astralay::ui
