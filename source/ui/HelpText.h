#pragma once

#include <juce_core/juce_core.h>

namespace astralay::ui
{

/** Help tag for a control: a per-tap parameter suffix (for example "feedback"), a global
    parameter ID, or one of the UI-only keys below. Returns an empty string for unknown keys.

    All help text lives in HelpText.cpp so it can be reviewed and edited in one place.
*/
juce::String helpFor (const juce::String& key);

namespace helpKeys
{
    inline constexpr auto undo = "ui_undo";
    inline constexpr auto redo = "ui_redo";
    inline constexpr auto save = "ui_save";
    inline constexpr auto load = "ui_load";
    inline constexpr auto presetName = "ui_presetName";
    inline constexpr auto randomize = "ui_randomize";
    inline constexpr auto tapSelector = "ui_tapSelector";
    inline constexpr auto performance = "ui_performance";
    inline constexpr auto macroArm = "ui_macroArm";
    inline constexpr auto macroValue = "ui_macroValue";
    inline constexpr auto modulationAmount = "ui_modulationAmount";
}

} // namespace astralay::ui
