#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AccessibleGroup.h"
#include "ContextMenu.h"

namespace astralay::ui
{

/** The accessibility settings, laid over the whole editor until its Close button or Escape closes
    it. Tab moves between its controls and wraps, without reaching the editor's controls behind it.
*/
class AccessibilitySettings final : public juce::Component
{
public:
    explicit AccessibilitySettings (bool helpTagsOn);

    /** Called when the help tags checkbox is switched, with its new state. */
    std::function<void (bool)> onHelpTagsChanged;

    /** Called by the Close button and Escape. The panel does not hide itself. */
    std::function<void()> onClose;

    void focusFirstControl();

    /** Handles Escape and Tab, and leaves every other key alone. */
    bool keyPressed (const juce::KeyPress&) override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AccessibleGroup panel { "Accessibility settings" };
    MenuControl<juce::ToggleButton> helpTags { "Help tags" };
    MenuControl<juce::TextButton> closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccessibilitySettings)
};

} // namespace astralay::ui
