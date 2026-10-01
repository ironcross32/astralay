#pragma once

namespace astralay::ui
{

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

} // namespace astralay::ui
