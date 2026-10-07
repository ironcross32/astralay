#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "state/Macros.h"
#include "ui/AccessibilitySettings.h"
#include "ui/AccessibleGroup.h"
#include "ui/Announcer.h"
#include "ui/ContextMenu.h"
#include "ui/KeyLayer.h"
#include "ui/ParameterControls.h"
#include "ui/PerformancePad.h"
#include "ui/Theme.h"

class AstralayProcessor;

/** The plugin window: five top-level groups (Main, the selected tap, Macros, Global, Performance)
    laid out at a fixed base size and scaled with the window.

    Keyboard: Tab moves through every control in order and wraps; Alt+period and Alt+comma
    (Cmd on macOS) jump to the first control of the next or previous group. Outside the
    performance area, the number keys, minus and equals switch taps without moving focus,
    Backspace turns the selected tap on or off, and Ctrl+C and Ctrl+V copy and paste a tap or one
    of its settings. The right bracket key opens the focused control's context menu, if it has one.

    Macros: each has a group named after it, holding an Arm button and a value slider, with a
    context menu for renaming it, editing or clearing what it moves, and making it bipolar. While
    a macro is armed, the sliders it can move set how far it moves them instead of their own
    values. Only one macro is armed at a time, and none once the window closes. Alt+M (Cmd+M on
    macOS) asks "Arm?" and takes the next key: 1 to 8 arms that macro, or disarms it if it is
    armed, and 0 disarms whichever is.
*/
class AstralayEditor final : public juce::AudioProcessorEditor,
                             public astralay::ui::AnnouncementTarget,
                             private juce::FocusChangeListener,
                             private juce::ValueTree::Listener,
                             private juce::ChangeListener,
                             private juce::KeyListener,
                             private juce::Timer,
                             private juce::AsyncUpdater
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

    struct MacroControls
    {
        explicit MacroControls (const juce::String& title) : group (title) {}

        astralay::ui::AccessibleGroup group;
        astralay::ui::MenuControl<juce::TextButton> arm;
        astralay::ui::ParameterSlider value;
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
    void buildMacrosGroup();
    void buildGlobalGroup();
    void buildPerformanceGroup();

    /** The ID of the parameter a row is showing. */
    juce::String parameterIdFor (const SliderRow& row) const;
    void bindRow (SliderRow& row);

    /** Arms a macro, disarming whichever was armed, or disarms them all with -1. */
    void armMacro (int macroIndex);

    /** Handles the key that follows the arming shortcut. Returns false if it isn't 0 to 8. */
    bool handleArmKey (const juce::KeyPress& key);

    /** The amount a slider showing this parameter has for the armed macro: in the parameter's
        unit, or for a synced note value, the amount of the time it stands in for as a percentage.
    */
    double armedAmountFor (const juce::String& parameterId) const;

    /** What an amount for this parameter is multiplied by to give a percentage of its range. */
    double percentPerUnit (const juce::String& parameterId) const;

    /** The parameter whose slider is showing in place of this one, which is its synced note value
        while host sync is on and it has one, and otherwise itself.
    */
    juce::RangedAudioParameter* shownParameterFor (const juce::String& parameterId) const;

    /** Names a macro's controls after it and binds its value slider, whose range depends on
        whether the macro is bipolar.
    */
    void describeMacro (int macroIndex);

    /** Brings the macro controls, and the sliders setting the armed macro's amounts, up to date
        with the processor.
    */
    void refreshMacros();
    void showMacroMenu (int macroIndex, bool fromValue = false);
    void showRenamePrompt (int macroIndex, juce::Component* focusAfterwards);
    void showAmountPrompt (int macroIndex, const juce::String& parameterId, juce::Component* focusAfterwards);

    /** Opens a type-in field over a control. commit is given the typed text and returns an empty
        string to accept it, or the message to announce.
    */
    void showPrompt (juce::Component& over, juce::Component* focusAfterwards, const juce::String& title,
                     const juce::String& help, const juce::String& text,
                     std::function<juce::String (const juce::String&)> commit);
    void closePrompt (bool refocus);
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

    /** Switches a bool parameter on while a key is held and off when it is let go. */
    void holdSwitch (const char* parameterId, bool held);
    void stopGlitches (bool stopped);

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
    void setupMidiControls();
    juce::String midiTarget (juce::Component*) const;
    bool selectMidiTarget (juce::Component&);
    void startMidiLearn (const juce::String& target);
    void toggleMidiLearn();
    bool handleMidiKey (const juce::KeyPress&);
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void showMenu (juce::PopupMenu&, juce::Component&, std::function<void (int)>);
    void timerCallback() override;
    void showControlMenu (juce::Component&);
    juce::String appendMidiMenu (juce::PopupMenu&, juce::Component&);
    bool midiMenuResult (int, const juce::String&);
    void showMainMenu();

    /** Lays the accessibility settings over the editor, hiding the controls behind them from
        screen readers until they close.
    */
    void showAccessibilitySettings();
    void closeAccessibilitySettings();
    void saveMidiMapping (bool saveAs, std::function<void()> after = {});
    void writeMidiMapping (const juce::File&, std::function<void()> after);
    void loadMidiMapping (const juce::File&);
    void defaultMidiMapping();
    void confirmMidi (const juce::String&, const juce::String&, const juce::StringArray&, std::function<void (int)>);
    juce::Component::SafePointer<juce::Component> midiKeyTarget;
    std::vector<juce::Component::SafePointer<juce::Component>> midiMenuKeyTargets;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
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

    astralay::ui::AccessibleGroup mainGroup { "Main" }, tapGroup { "Tap 1" }, macrosGroup { "Macros" },
                                  globalGroup { "Global" }, performanceGroup { "Performance" };

    // Main
    astralay::ui::MenuControl<juce::TextButton> mainMenuButton { "Main menu" }, midiLearnButton { "MIDI learn" },
        undoButton { "Undo" }, redoButton { "Redo" }, saveButton { "Save" }, loadButton { "Load" };
    astralay::ui::MenuControl<juce::Label> presetName;

    // Tap
    juce::Label tapSelectorLabel;
    astralay::ui::MenuControl<juce::ComboBox> tapSelector;
    astralay::ui::ParameterToggle tapEnabled { "Enabled" };
    std::vector<LayoutItem> tapBasics;
    std::vector<GlitchSection> glitchSections;

    // Macros
    std::vector<std::unique_ptr<MacroControls>> macroControls;
    astralay::state::MacroSettings macros;   // As the controls last showed them.
    int armedMacro = -1;
    std::unique_ptr<astralay::ui::TypeInField> prompt;
    juce::Component::SafePointer<juce::Component> promptFocus;

    // Global
    astralay::ui::AccessibleGroup timingGroup { "Timing" }, engineGroup { "Glitch engine" }, outputGroup { "Output" },
                                  tapeStopGroup { "Tape stop" };
    std::vector<LayoutItem> timingItems, engineItems, outputItems, tapeStopItems;
    astralay::ui::ParameterToggle syncToggle { "Host sync" }, freezeToggle { "Freeze" }, sustainToggle { "Freeze sustain" },
                                  reproducibleToggle { "Reproducible randomness" }, tapeStopToggle { "Tape stop" };
    juce::Label placementLabel;
    astralay::ui::ParameterChoice placementChoice;

    // Performance
    astralay::ui::PerformancePad performancePad;

    std::vector<std::unique_ptr<SliderRow>> sliderRows;
    std::unique_ptr<FocusOutline> focusOutline;
    std::unique_ptr<astralay::ui::AccessibilitySettings> accessibilitySettings;
    astralay::ui::TooltipWindow tooltipWindow { this, 700 };
    astralay::ui::Announcer announcer { *this };
    astralay::ui::KeyLayer keyLayer { announcer };

    std::unique_ptr<juce::FileChooser> fileChooser;
    static constexpr int fromFileItemId = 10000;

    std::unique_ptr<juce::ParameterAttachment> syncWatcher, freezeWatcher, tapeStopWatcher;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> enabledWatchers;

    int selectedTap = 0;
    bool synced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AstralayEditor)
};
