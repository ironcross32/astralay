#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ui/AccessibleGroup.h"
#include "ui/Announcer.h"
#include "ui/ContextMenu.h"
#include "ui/ParameterControls.h"
#include "ui/PerformancePad.h"
#include "ui/Theme.h"

class AstralayProcessor;

/** The plugin window: four top-level groups (Main, the selected tap, Global, Performance) laid out
    at a fixed base size and scaled with the window.

    Keyboard: Tab moves through every control in order and wraps; Alt+period and Alt+comma
    (Cmd on macOS) jump to the first control of the next or previous group. Outside the
    performance area, the number keys, minus and equals switch taps without moving focus,
    Backspace turns the selected tap on or off, and Ctrl+C and Ctrl+V copy and paste a tap or one
    of its settings. The right bracket key opens the focused control's context menu, if it has one;
    moving to such a control announces "has context menu".
*/
class AstralayEditor final : public juce::AudioProcessorEditor,
                             public astralay::ui::AnnouncementTarget,
                             private juce::FocusChangeListener,
                             private juce::ValueTree::Listener
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
    void buildPerformanceGroup();

    void bindRow (SliderRow& row);
    void selectTap (int tapIndex);

    /** Selects a tap from the keyboard, announcing its number. Does nothing if it is selected. */
    void switchToTap (int tapIndex);

   #if JUCE_MAC
    /** The value of the focused per-tap control as ", <value>", or an empty string if focus is
        elsewhere.
    */
    juce::String focusedTapControlValue() const;
    int tapSwitchAnnouncement = 0;
   #endif

    void toggleSelectedTap();

    /** Switches a bool parameter as an undoable edit and announces "<name> on" or "<name> off". */
    void toggleAndAnnounce (const char* parameterId, const juce::String& name);
    void holdFreeze (bool held);

    /** Handles copy and paste for the focused control. Returns false if the key isn't one of
        those or focus isn't on the tap selector, the tap's on/off toggle or a per-tap slider.
    */
    bool handleClipboardKey (const juce::KeyPress& key);
    void setSynced (bool shouldBeSynced);
    void refreshTapName (int tapIndex, bool on);
    void refreshPresetName();

    void undo();
    void redo();
    void showSaveDialog();
    void showLoadMenu();
    void showLoadDialog();

    /** Opens the context menu of the focused control, or of the nearest control around it that
        has one. Returns false if there is none.
    */
    bool showContextMenuForFocus();
    void showOutputClipMenu (juce::Component& target);
    void showPitchModeMenu (juce::Component& target);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void jumpToGroup (int direction);

   #if JUCE_MAC
    /** Handles a key that arrives while no control has keyboard focus. JUCE drops the focused
        control when the host's window stops being the key window and doesn't restore it when the
        window becomes key again, although the keys keep arriving, and Tab does nothing without a
        focused control to move from. This puts focus back where it was and passes the key on.
        Returns true if the key was used.
    */
    bool handleKeyWithoutFocus (const juce::KeyPress& key);
   #endif

    static void layoutColumn (juce::Rectangle<int> area, const std::vector<LayoutItem>& items,
                              int rowHeight, int gap, float labelFraction);

    void globalFocusChanged (juce::Component* focused) override;

    AstralayProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    astralay::ui::LookAndFeel lookAndFeel;
    juce::Component content;

    astralay::ui::AccessibleGroup mainGroup { "Main" }, tapGroup { "Tap 1" }, globalGroup { "Global" },
                                  performanceGroup { "Performance" };

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

    // Performance
    astralay::ui::PerformancePad performancePad;

    std::vector<std::unique_ptr<SliderRow>> sliderRows;
    std::unique_ptr<FocusOutline> focusOutline;
    juce::TooltipWindow tooltipWindow { this, 700 };
    astralay::ui::Announcer announcer { *this };
    astralay::ui::ContextMenuHint contextMenuHint { announcer };

    std::unique_ptr<juce::FileChooser> fileChooser;
    static constexpr int fromFileItemId = 10000;

    std::unique_ptr<juce::ParameterAttachment> syncWatcher, freezeWatcher;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> enabledWatchers;

    int selectedTap = 0;
    bool synced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AstralayEditor)
};
