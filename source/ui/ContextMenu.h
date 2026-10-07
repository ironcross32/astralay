#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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

/** Standard controls with an owning context menu and an optional consumed learn selection.
    Keep the base control's native accessibility value/role and ordinary interactions. */
template <typename Base>
class MenuControl : public Base, public ContextMenuTarget
{
public:
    using Base::Base;
    std::function<bool()> selectMidiTarget;
    void setContextMenu (std::function<void()> callback)
    {
        menu = std::move (callback);
        this->invalidateAccessibilityHandler();
    }
    bool hasContextMenu() const override { return menu != nullptr; }
    void showContextMenu() override { if (menu) menu(); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.isKeyCode (juce::KeyPress::returnKey) && selectMidiTarget && selectMidiTarget()) return true;
        return Base::keyPressed (key);
    }
    void mouseDown (const juce::MouseEvent& event) override
    {
        consumed = event.mods.isPopupMenu() && hasContextMenu();
        if (consumed) showContextMenu();
        else if (selectMidiTarget && selectMidiTarget()) consumed = true;
        else Base::mouseDown (event);
    }
    void mouseDrag (const juce::MouseEvent& event) override { if (! consumed) Base::mouseDrag (event); }
    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! consumed) Base::mouseUp (event);
        consumed = false;
    }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        auto handler = Base::createAccessibilityHandler();
        if (handler && menu)
        {
            // JUCE exposes the action set as const, but this newly constructed handler is mutable.
            auto& actions = const_cast<juce::AccessibilityActions&> (handler->getActions());
            const auto original = actions;
            for (const auto action : { juce::AccessibilityActionType::press, juce::AccessibilityActionType::toggle })
                if (original.contains (action))
                    actions.addAction (action, [this, original, action]
                    {
                        if (! selectMidiTarget || ! selectMidiTarget()) original.invoke (action);
                    });
            actions.addAction (juce::AccessibilityActionType::showMenu, [this] { showContextMenu(); });
        }
        return handler;
    }
private:
    std::function<void()> menu;
    bool consumed = false;
};

/** The context menu that opens from c: its own, or that of the nearest control around it that has
    one. Returns nullptr if there is none.
*/
ContextMenuTarget* findContextMenu (juce::Component* c);

} // namespace astralay::ui
