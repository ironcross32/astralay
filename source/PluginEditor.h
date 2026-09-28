#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ui/AccessibleGroup.h"
#include "ui/Announcer.h"
#include "ui/ParameterControls.h"
#include "ui/Theme.h"

class AstralayProcessor;

/** The plugin window: three top-level groups (Main, the selected tap, Global) laid out at a fixed
    base size and scaled with the window.

    Keyboard: Tab moves through every control in order and wraps; Alt+period and Alt+comma
    (Cmd on macOS) jump to the first control of the next or previous group.
*/
class AstralayEditor final : public juce::AudioProcessorEditor,
                             public astralay::ui::AnnouncementTarget,
                             private juce::FocusChangeListener
{
public:
    explicit AstralayEditor (AstralayProcessor&);
    ~AstralayEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Speaks through the screen reader. */
    void announce (const juce::String& text) override;

private:
    /** A labelled slider. suffix is a per-tap parameter suffix or a global parameter ID; when
        syncSuffix is set, the row switches to that parameter while host sync is on.
    */
    struct SliderRow
    {
        juce::Label label;
        astralay::ui::ParameterSlider slider;
        juce::String suffix, syncSuffix;
        bool perTap = false;
    };

    /** One line of a column layout: an optional visible label and the control it describes. */
    struct LayoutItem
    {
        juce::Label* label = nullptr;
        juce::Component* control = nullptr;
    };

    struct GlitchSection
    {
        std::unique_ptr<astralay::ui::AccessibleGroup> group;
        std::vector<LayoutItem> items;
    };

    class FocusOutline;

    SliderRow& addSliderRow (astralay::ui::AccessibleGroup& group, std::vector<LayoutItem>& items,
                             const juce::String& visibleLabel, const char* suffix,
                             const char* syncSuffix, bool perTap);
    void addToggle (astralay::ui::AccessibleGroup& group, std::vector<LayoutItem>& items,
                    astralay::ui::ParameterToggle& toggle, const char* parameterId);
    void setUpLabel (juce::Label& label, const juce::String& text);

    void buildMainGroup();
    void buildTapGroup();
    void buildGlobalGroup();

    void bindRow (SliderRow& row);
    void selectTap (int tapIndex);
    void setSynced (bool shouldBeSynced);
    void refreshTapName (int tapIndex, bool on);
    void jumpToGroup (int direction);

    static void layoutColumn (juce::Rectangle<int> area, const std::vector<LayoutItem>& items,
                              int rowHeight, int gap, float labelFraction);

    void globalFocusChanged (juce::Component* focused) override;

    AstralayProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    astralay::ui::LookAndFeel lookAndFeel;
    juce::Component content;

    astralay::ui::AccessibleGroup mainGroup { "Main" }, tapGroup { "Tap 1" }, globalGroup { "Global" };

    // Main
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
    juce::Label presetName;

    // Tap
    juce::Label tapSelectorLabel;
    juce::ComboBox tapSelector;
    astralay::ui::ParameterToggle tapEnabled { "Enabled" };
    std::vector<LayoutItem> tapBasics;
    std::vector<GlitchSection> glitchSections;

    // Global
    astralay::ui::AccessibleGroup timingGroup { "Timing" }, engineGroup { "Glitch engine" }, outputGroup { "Output" };
    std::vector<LayoutItem> timingItems, engineItems, outputItems;
    astralay::ui::ParameterToggle syncToggle { "Host sync" }, freezeToggle { "Freeze" },
                                  reproducibleToggle { "Reproducible randomness" };
    juce::Label placementLabel;
    astralay::ui::ParameterChoice placementChoice;

    std::vector<std::unique_ptr<SliderRow>> sliderRows;
    std::unique_ptr<FocusOutline> focusOutline;
    juce::TooltipWindow tooltipWindow { this, 700 };
    astralay::ui::Announcer announcer { *this };

    std::unique_ptr<juce::ParameterAttachment> syncWatcher;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> enabledWatchers;

    int selectedTap = 0;
    bool synced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AstralayEditor)
};
