#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "state/Presets.h"
#include "ui/HelpText.h"

using namespace astralay;

namespace
{
    constexpr int rowHeight = 32;
    constexpr int glitchRowHeight = 26;
    constexpr int gap = 6;

    struct GlitchRowSpec
    {
        const char* visibleLabel;
        const char* suffix;
        const char* syncSuffix = nullptr;
    };

    struct GlitchSpec
    {
        const char* title;
        std::vector<GlitchRowSpec> rows;
    };

    /** The nine glitch sub-groups in processing order, each with probability first and every
        minimum before its maximum.
    */
    const std::vector<GlitchSpec>& glitchSpecs()
    {
        using namespace params::tap;

        static const std::vector<GlitchSpec> specs
        {
            { "Reverse",              { { "Probability", reverseProb } } },
            { "Stutter",              { { "Probability", stutterProb },
                                        { "Min slice", stutterMin, stutterSyncMin },
                                        { "Max slice", stutterMax, stutterSyncMax } } },
            { "Granularize",          { { "Probability", grainProb },
                                        { "Min grain", grainSizeMin },
                                        { "Max grain", grainSizeMax },
                                        { "Min density", grainDensMin },
                                        { "Max density", grainDensMax } } },
            { "Pitch",                { { "Probability", pitchProb },
                                        { "Minimum", pitchMin },
                                        { "Maximum", pitchMax },
                                        { "Min speed", pitchSpeedMin },
                                        { "Max speed", pitchSpeedMax } } },
            { "LPC formant",          { { "Probability", lpcProb },
                                        { "Min shift", lpcMin },
                                        { "Max shift", lpcMax } } },
            { "Cepstral formant",     { { "Probability", cepsProb },
                                        { "Min shift", cepsMin },
                                        { "Max shift", cepsMax } } },
            { "Ring modulation",      { { "Probability", ringProb },
                                        { "Min frequency", ringMin },
                                        { "Max frequency", ringMax } } },
            { "Frequency modulation", { { "Probability", fmProb },
                                        { "Min ratio", fmRatioMin },
                                        { "Max ratio", fmRatioMax },
                                        { "Min index", fmIndexMin },
                                        { "Max index", fmIndexMax } } },
            { "Bit crusher",          { { "Probability", crushProb },
                                        { "Min bits", crushBitsMin },
                                        { "Max bits", crushBitsMax },
                                        { "Min reduction", crushRateMin },
                                        { "Max reduction", crushRateMax } } },
        };

        return specs;
    }

}

//==============================================================================
/** Draws a thick outline around the focused control, for low-vision users. Invisible to mouse
    and screen readers.
*/
class AstralayEditor::FocusOutline final : public juce::Component
{
public:
    explicit FocusOutline (juce::Component& contentToTrack)
        : trackedContent (contentToTrack)
    {
        setInterceptsMouseClicks (false, false);
        setAccessible (false);
        setAlwaysOnTop (true);
    }

    void setTarget (juce::Component* newTarget)
    {
        target = newTarget;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (target == nullptr || ! target->isShowing() || ! trackedContent.isParentOf (target))
            return;

        const auto area = trackedContent.getLocalArea (target, target->getLocalBounds()).expanded (3);
        g.setColour (ui::colours::accent);
        g.drawRect (area, 3);
    }

private:
    juce::Component& trackedContent;
    juce::Component::SafePointer<juce::Component> target;
};

//==============================================================================
AstralayEditor::AstralayEditor (AstralayProcessor& p)
    : AudioProcessorEditor (p), processor (p), state (p.getState())
{
    setLookAndFeel (&lookAndFeel);
    synced = state.getRawParameterValue (params::global::sync)->load() >= 0.5f;

    addAndMakeVisible (content);
    content.addAndMakeVisible (mainGroup);
    content.addAndMakeVisible (tapGroup);
    content.addAndMakeVisible (macrosGroup);
    content.addAndMakeVisible (globalGroup);
    content.addAndMakeVisible (performanceGroup);

    mainGroup.setExplicitFocusOrder (1);
    tapGroup.setExplicitFocusOrder (2);
    macrosGroup.setExplicitFocusOrder (3);
    globalGroup.setExplicitFocusOrder (4);
    performanceGroup.setExplicitFocusOrder (5);

    buildMainGroup();
    buildTapGroup();
    buildMacrosGroup();
    buildGlobalGroup();
    buildPerformanceGroup();
    setupMidiControls();

    focusOutline = std::make_unique<FocusOutline> (content);
    content.addAndMakeVisible (*focusOutline);

    // Watch host sync and each tap's on/off state, now that every control exists.
    syncWatcher = std::make_unique<juce::ParameterAttachment> (*state.getParameter (params::global::sync),
                                                               [this] (float v) { setSynced (v >= 0.5f); });

    freezeWatcher = std::make_unique<juce::ParameterAttachment> (*state.getParameter (params::global::freeze),
                                                                 [this] (float v) { performancePad.setFrozen (v >= 0.5f); });
    freezeWatcher->sendInitialUpdate();

    for (int t = 0; t < params::numTaps; ++t)
    {
        auto* enabled = state.getParameter (params::tapId (t, params::tap::enabled));
        enabledWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*enabled, [this, t] (float value)
        {
            refreshTapName (t, value >= 0.5f);
            performancePad.setTapEnabled (t, value >= 0.5f);
        }));
    }

    selectTap (processor.getSelectedTap());

    for (auto& watcher : enabledWatchers)
        watcher->sendInitialUpdate();

    juce::Desktop::getInstance().addFocusChangeListener (this);
    state.state.addListener (this);
    processor.getMacroChanges().addChangeListener (this);

    const auto ratio = (double) ui::sizes::baseWidth / ui::sizes::baseHeight;
    setResizable (true, true);
    setResizeLimits (ui::sizes::baseWidth * 3 / 4, ui::sizes::baseHeight * 3 / 4,
                     ui::sizes::baseWidth * 2, ui::sizes::baseHeight * 2);
    getConstrainer()->setFixedAspectRatio (ratio);
    setSize (ui::sizes::baseWidth, ui::sizes::baseHeight);
}

AstralayEditor::~AstralayEditor()
{
    stopTimer();
    processor.getMidiMappings().cancelLearn();
    processor.getMidiMappings().setAnnouncer ({});
    if (midiKeyTarget != nullptr) midiKeyTarget->removeKeyListener (this);
    for (auto& window : midiMenuKeyTargets)
        if (window != nullptr) window->removeKeyListener (this);
    // Ends a held freeze while there is still an editor to end it.
    performancePad.releaseHeldKeys();
    keyLayer.close();
    closePrompt (false);

    processor.getMacroChanges().removeChangeListener (this);
    state.state.removeListener (this);
    cancelPendingUpdate();
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void AstralayEditor::setUpLabel (juce::Label& label, const juce::String& text)
{
    // Visible only: screen readers get the full name from the control itself.
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setAccessible (false);
    label.setInterceptsMouseClicks (false, false);
}

AstralayEditor::SliderRow& AstralayEditor::addSliderRow (ui::AccessibleGroup& group, std::vector<LayoutItem>& items,
                                                         const juce::String& visibleLabel, const char* suffix,
                                                         const char* syncSuffix, bool perTap)
{
    auto& row = *sliderRows.emplace_back (std::make_unique<SliderRow>());
    row.suffix = suffix;
    row.syncSuffix = syncSuffix != nullptr ? juce::String (syncSuffix) : juce::String();
    row.perTap = perTap;

    setUpLabel (row.label, visibleLabel);
    group.addAndMakeVisible (row.label);
    group.addInOrder (row.slider);
    items.push_back ({ &row.label, &row.slider });

    if (! perTap)
        bindRow (row);

    return row;
}

void AstralayEditor::addToggle (ui::AccessibleGroup& group, std::vector<LayoutItem>& items,
                                ui::ParameterToggle& toggle, const char* parameterId)
{
    toggle.bind (*state.getParameter (parameterId), parameterId);
    group.addInOrder (toggle);
    items.push_back ({ nullptr, &toggle });
}

void AstralayEditor::buildMainGroup()
{
    ui::describe (mainMenuButton, "Main menu", "MIDI mapping options.");
    ui::describe (midiLearnButton, "MIDI learn", "Select a sound control, then move a MIDI controller. Activate again to cancel.");
    mainMenuButton.onClick = [this] { showMainMenu(); };
    midiLearnButton.onClick = [this] { toggleMidiLearn(); };
    ui::describe (undoButton, "Undo", ui::helpFor (ui::helpKeys::undo));
    ui::describe (redoButton, "Redo", ui::helpFor (ui::helpKeys::redo));
    ui::describe (saveButton, "Save", ui::helpFor (ui::helpKeys::save));
    ui::describe (loadButton, "Load", ui::helpFor (ui::helpKeys::load));

    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    saveButton.onClick = [this] { showSaveDialog(); };
    loadButton.onClick = [this] { showLoadMenu(); };

    for (auto* button : { &mainMenuButton, &midiLearnButton, &undoButton, &redoButton, &saveButton, &loadButton })
    {
        button->setWantsKeyboardFocus (true);
        mainGroup.addInOrder (*button);
    }

    // Read-only, but focusable so Tab and group navigation can reach it.
    refreshPresetName();
    presetName.setTooltip (ui::helpFor (ui::helpKeys::presetName));
    presetName.setWantsKeyboardFocus (true);
    presetName.setJustificationType (juce::Justification::centredLeft);
    mainGroup.addInOrder (presetName);
}

void AstralayEditor::buildTapGroup()
{
    using namespace params::tap;

    setUpLabel (tapSelectorLabel, "Tap");
    tapGroup.addAndMakeVisible (tapSelectorLabel);

    for (int t = 0; t < params::numTaps; ++t)
        tapSelector.addItem ("Tap " + juce::String (t + 1), t + 1);

    ui::describe (tapSelector, "Selected tap", ui::helpFor (ui::helpKeys::tapSelector));
    tapSelector.onChange = [this] { selectTap (tapSelector.getSelectedId() - 1); };
    tapGroup.addInOrder (tapSelector);
    tapGroup.addInOrder (tapEnabled);

    addSliderRow (tapGroup, tapBasics, "Time", params::tap::time, timeSync, true);
    addSliderRow (tapGroup, tapBasics, "Volume", volume, nullptr, true);
    addSliderRow (tapGroup, tapBasics, "Pan", pan, nullptr, true);
    addSliderRow (tapGroup, tapBasics, "Feedback", feedback, nullptr, true);
    addSliderRow (tapGroup, tapBasics, "Low cut", lowCut, nullptr, true);
    addSliderRow (tapGroup, tapBasics, "High cut", highCut, nullptr, true);

    for (const auto& spec : glitchSpecs())
    {
        auto& section = glitchSections.emplace_back();
        section.group = std::make_unique<ui::AccessibleGroup> (spec.title);
        tapGroup.addInOrder (*section.group);

        for (const auto& rowSpec : spec.rows)
        {
            auto& slider = addSliderRow (*section.group, section.items, rowSpec.visibleLabel, rowSpec.suffix, rowSpec.syncSuffix, true).slider;

            if (juce::String (rowSpec.suffix) == pitchProb)
                slider.setContextMenu ([this, &slider] { showPitchModeMenu (slider); });
        }
    }
}

void AstralayEditor::buildMacrosGroup()
{
    macros = processor.getMacros();

    for (int m = 0; m < params::numMacros; ++m)
    {
        auto& controls = *macroControls.emplace_back (std::make_unique<MacroControls> (astralay::state::macroName (macros, m)));

        controls.arm.setWantsKeyboardFocus (true);
        controls.arm.onClick = [this, m] { armMacro (armedMacro == m ? -1 : m); };

        // The value slider gets the menu too, so that a right-click on it opens it.
        controls.group.setContextMenu ([this, m] { showMacroMenu (m); });
        controls.value.setContextMenu ([this, m] { showMacroMenu (m, true); });
        controls.arm.setContextMenu ([this, m] { showMacroMenu (m); });

        controls.group.addInOrder (controls.arm);
        controls.group.addInOrder (controls.value);
        macrosGroup.addInOrder (controls.group);

        describeMacro (m);
    }
}

void AstralayEditor::buildGlobalGroup()
{
    using namespace params::global;

    for (auto* group : { &timingGroup, &engineGroup, &outputGroup })
        globalGroup.addInOrder (*group);

    addToggle (timingGroup, timingItems, syncToggle, sync);
    addSliderRow (timingGroup, timingItems, "Glide time", glide, nullptr, false);
    addToggle (timingGroup, timingItems, freezeToggle, freeze);
    addToggle (timingGroup, timingItems, sustainToggle, freezeSustain);

    addSliderRow (engineGroup, engineItems, "Threshold", threshold, nullptr, false);

    setUpLabel (placementLabel, "Placement");
    engineGroup.addAndMakeVisible (placementLabel);
    placementChoice.bind (*state.getParameter (placement), placement);
    engineGroup.addInOrder (placementChoice);
    engineItems.push_back ({ &placementLabel, &placementChoice });

    addSliderRow (engineGroup, engineItems, "Buffer size", bufferSize, bufferSync, false);
    addSliderRow (engineGroup, engineItems, "Max glitches", maxGlitches, nullptr, false);
    addSliderRow (engineGroup, engineItems, "Min length", lengthMin, nullptr, false);
    addSliderRow (engineGroup, engineItems, "Max length", lengthMax, nullptr, false);
    addToggle (engineGroup, engineItems, reproducibleToggle, reproducible);
    addSliderRow (engineGroup, engineItems, "Seed", seed, nullptr, false);

    addSliderRow (outputGroup, outputItems, "Smear amount", smearAmount, nullptr, false);
    addSliderRow (outputGroup, outputItems, "Smear size", smearSize, nullptr, false);
    addSliderRow (outputGroup, outputItems, "Mix", mix, nullptr, false);
    auto& gain = addSliderRow (outputGroup, outputItems, "Output gain", outputGain, nullptr, false).slider;
    gain.setContextMenu ([this, &gain] { showOutputClipMenu (gain); });
}

void AstralayEditor::buildPerformanceGroup()
{
    ui::describe (performancePad, "Performance area", ui::helpFor (ui::helpKeys::performance));
    performancePad.setSelection (processor.getPerformanceSelection());

    performancePad.onSelectionChanged = [this] (juce::uint32 selection) { processor.setPerformanceSelection (selection); };
    performancePad.onStepTimes = [this] (int direction, bool continuing) { processor.stepSelectedTapTimes (direction, continuing); };
    performancePad.onStepSmear = [this] (bool amount, int direction, bool continuing) { processor.stepSmear (amount, direction, continuing); };
    performancePad.onHoldFreeze = [this] (bool held) { holdFreeze (held); };
    performancePad.onToggleFreeze = [this] { toggleAndAnnounce (params::global::freeze, "Freeze"); };

    performanceGroup.addInOrder (performancePad);
}

//==============================================================================
juce::String AstralayEditor::parameterIdFor (const SliderRow& row) const
{
    const auto key = synced && row.syncSuffix.isNotEmpty() ? row.syncSuffix : row.suffix;
    return row.perTap ? params::tapId (selectedTap, key.toRawUTF8()) : key;
}

void AstralayEditor::bindRow (SliderRow& row)
{
    const auto useSync = synced && row.syncSuffix.isNotEmpty();
    const auto key = useSync ? row.syncSuffix : row.suffix;
    const auto id = parameterIdFor (row);

    auto* parameter = state.getParameter (id);
    jassert (parameter != nullptr);

    if (parameter == nullptr)
        return;

    row.slider.bind (*parameter, useSync ? Unit::plain : params::unitFor (key), key, useSync, 0);

    // While a macro is armed, the sliders it can move set how far it moves them. A note value
    // sets the amount of the time it stands in for, as a percentage.
    const auto target = params::modulationTarget (id);

    if (armedMacro >= 0 && target.isNotEmpty())
    {
        const auto scale = target == id ? 1.0 : percentPerUnit (target);

        row.slider.editModulation (astralay::state::macroName (macros, armedMacro), armedAmountFor (id), [this, target, scale] (double amount)
        {
            if (armedMacro >= 0)
                processor.setModulation (armedMacro, target, (float) (amount / scale));
        }, target != id);
    }
}

//==============================================================================
double AstralayEditor::percentPerUnit (const juce::String& parameterId) const
{
    const auto* parameter = state.getParameter (parameterId);
    return parameter != nullptr ? 100.0 / ui::modulationLimit (*parameter) : 1.0;
}

double AstralayEditor::armedAmountFor (const juce::String& parameterId) const
{
    const auto target = params::modulationTarget (parameterId);

    if (armedMacro >= 0 && target.isNotEmpty())
        for (const auto& modulation : macros[(size_t) armedMacro].modulations)
            if (modulation.parameterId == target)
                return (double) modulation.amount * (target == parameterId ? 1.0 : percentPerUnit (target));

    return 0.0;
}

void AstralayEditor::armMacro (int macroIndex)
{
    processor.getMidiMappings().cancelLearn();
    if (macroIndex == armedMacro)
        return;

    const auto previous = std::exchange (armedMacro, macroIndex);

    for (auto m : { previous, armedMacro })
        if (m >= 0)
            describeMacro (m);

    for (auto& row : sliderRows)
        if (params::modulationTarget (parameterIdFor (*row)).isNotEmpty())
            bindRow (*row);

    announce (armedMacro >= 0 ? astralay::state::macroName (macros, armedMacro) + " armed"
                              : astralay::state::macroName (macros, previous) + " disarmed");
}

bool AstralayEditor::handleArmKey (const juce::KeyPress& key)
{
    const auto digit = ui::digitForKey (key);

    if (digit < 0 || digit > params::numMacros)
        return false;

    if (digit == 0 && armedMacro < 0)
        announce ("No macro was armed");
    else
        armMacro (digit == 0 || digit - 1 == armedMacro ? -1 : digit - 1);

    return true;
}

void AstralayEditor::describeMacro (int macroIndex)
{
    auto& controls = *macroControls[(size_t) macroIndex];
    const auto name = astralay::state::macroName (macros, macroIndex);
    const auto armed = macroIndex == armedMacro;

    controls.group.setTitle (name);
    controls.group.repaint();

    // The button says what pressing it does, so its name is also how its state is read.
    controls.arm.setButtonText (armed ? "Disarm" : "Arm");
    ui::describe (controls.arm, (armed ? "Disarm " : "Arm ") + name, ui::helpFor (ui::helpKeys::macroArm));

    if (auto* parameter = state.getParameter (params::macroId (macroIndex)))
    {
        controls.value.bind (*parameter, Unit::macro, ui::helpKeys::macroValue);
        ui::describe (controls.value, name + " value", ui::helpFor (ui::helpKeys::macroValue));
    }
}

void AstralayEditor::refreshMacros()
{
    const auto previous = std::exchange (macros, processor.getMacros());

    for (int m = 0; m < params::numMacros; ++m)
    {
        const auto& before = previous[(size_t) m];
        const auto& after = macros[(size_t) m];

        if (before.name != after.name || before.bipolar != after.bipolar)
            describeMacro (m);
    }

    if (armedMacro < 0)
        return;

    const auto renamed = previous[(size_t) armedMacro].name != macros[(size_t) armedMacro].name;

    for (auto& row : sliderRows)
    {
        // A slider whose amount changed may be in the middle of changing it, so it is only
        // rebound when its name has to change.
        if (renamed && row->slider.isEditingModulation())
            bindRow (*row);
        else
            row->slider.setModulationAmount (armedAmountFor (parameterIdFor (*row)));
    }
}

void AstralayEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshMacros();
}

void AstralayEditor::showMacroMenu (int macroIndex, bool fromValue)
{
    constexpr int renameItem = 1, bipolarItem = 2, firstModulationItem = 100;

    const auto& macro = macros[(size_t) macroIndex];
    auto& controls = *macroControls[(size_t) macroIndex];

    // Type-in fields give focus back to the control the menu was opened from.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    SafePointer<juce::Component> origin (focused != nullptr && controls.group.isParentOf (focused) ? focused : &controls.arm);

    juce::PopupMenu menu;
    menu.addItem (renameItem, "Rename...");

    juce::StringArray targets;

    if (! macro.modulations.empty())
    {
        juce::PopupMenu modulations;

        for (const auto& modulation : macro.modulations)
        {
            const auto item = firstModulationItem + 2 * targets.size();
            const auto* parameter = state.getParameter (modulation.parameterId);
            const auto* shown = shownParameterFor (modulation.parameterId);
            targets.add (modulation.parameterId);

            juce::PopupMenu actions;
            actions.addItem (item, "Edit...");
            actions.addItem (item + 1, "Clear");

            // As the control's slider is showing it: a synced note value's amount is a percentage.
            auto label = modulation.parameterId;

            if (parameter != nullptr && shown != nullptr)
                label = shown->getName (128) + ", "
                            + (shown == parameter ? ui::formatModulation (*parameter, modulation.amount)
                                                  : ui::formatModulationPercent (modulation.amount * percentPerUnit (modulation.parameterId)));

            modulations.addSubMenu (label, actions);
        }

        menu.addSubMenu ("Modulations", modulations);
    }

    menu.addItem (bipolarItem, "Bipolar", true, macro.bipolar);
    const auto midiId = appendMidiMenu (menu, fromValue ? static_cast<juce::Component&> (controls.value)
                                                      : static_cast<juce::Component&> (controls.arm));

    showMenu (menu, controls.group, [safeThis = SafePointer<AstralayEditor> (this), macroIndex, targets, origin,
                                     bipolar = macro.bipolar, midiId] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (safeThis->midiMenuResult (result, midiId)) return;
        if (result == renameItem)
        {
            safeThis->showRenamePrompt (macroIndex, origin.getComponent());
        }
        else if (result == bipolarItem)
        {
            safeThis->processor.setMacroBipolar (macroIndex, ! bipolar);
            safeThis->announce (bipolar ? "Bipolar off" : "Bipolar on");
        }
        else if (const auto index = (result - firstModulationItem) / 2; juce::isPositiveAndBelow (index, targets.size()))
        {
            const auto id = targets[index];

            if ((result - firstModulationItem) % 2 == 0)
            {
                safeThis->showAmountPrompt (macroIndex, id, origin.getComponent());
                return;
            }

            const auto* parameter = safeThis->shownParameterFor (id);
            safeThis->processor.setModulation (macroIndex, id, 0.0f);
            safeThis->announce ("Cleared " + (parameter != nullptr ? parameter->getName (128) : id));
        }
    });
}

void AstralayEditor::showRenamePrompt (int macroIndex, juce::Component* focusAfterwards)
{
    const auto name = astralay::state::macroName (macros, macroIndex);

    showPrompt (macroControls[(size_t) macroIndex]->value, focusAfterwards, "Rename " + name,
                "Type a name and press Enter, or press Escape to cancel. An empty name restores "
                    + params::defaultMacroName (macroIndex) + ".",
                name, [this, macroIndex] (const juce::String& text)
                {
                    processor.renameMacro (macroIndex, text);
                    return juce::String();
                });
}

void AstralayEditor::showAmountPrompt (int macroIndex, const juce::String& parameterId, juce::Component* focusAfterwards)
{
    const auto& modulations = macros[(size_t) macroIndex].modulations;
    const auto found = std::find_if (modulations.begin(), modulations.end(),
                                     [&parameterId] (const auto& m) { return m.parameterId == parameterId; });
    const auto* parameter = state.getParameter (parameterId);
    const auto* shown = shownParameterFor (parameterId);

    if (found == modulations.end() || parameter == nullptr || shown == nullptr)
        return;

    // As the control's slider is showing it: a synced note value's amount is a percentage.
    const auto asPercentage = shown != parameter;
    const auto scale = asPercentage ? percentPerUnit (parameterId) : 1.0;
    const auto limit = ui::modulationLimit (*parameter) * scale;

    const auto format = [parameter, asPercentage] (double amount)
    {
        return asPercentage ? ui::formatModulationPercent (amount) : ui::formatModulation (*parameter, amount);
    };

    const auto range = format (-limit) + " to " + format (limit);

    showPrompt (macroControls[(size_t) macroIndex]->value, focusAfterwards,
                shown->getName (128) + ", " + astralay::state::macroName (macros, macroIndex) + " amount, type a value",
                "Type a value and press Enter, or press Escape to cancel. Accepted range: " + range + ". 0 removes it.",
                format (found->amount * scale),
                [this, macroIndex, parameterId, parameter, asPercentage, scale, limit, range] (const juce::String& text) -> juce::String
                {
                    if (text.trim().isEmpty())
                        return "Enter a value from " + range + ".";

                    const auto amount = asPercentage ? ui::parseModulationPercent (text) : ui::parseModulation (*parameter, text);

                    if (! amount.has_value())
                        return "Not understood. Enter a value from " + range + ".";

                    if (std::abs (*amount) > limit * (1.0 + 1.0e-6))
                        return "Out of range, " + range + ".";

                    // Whole numbers for parameters that step in them.
                    const auto whole = parameter->getNormalisableRange().interval >= 1.0f;
                    const auto stored = *amount / scale;
                    processor.setModulation (macroIndex, parameterId, (float) (whole ? std::round (stored) : stored));
                    return {};
                });
}

juce::RangedAudioParameter* AstralayEditor::shownParameterFor (const juce::String& parameterId) const
{
    const auto counterpart = params::syncedCounterpart (parameterId);
    return state.getParameter (synced && counterpart.isNotEmpty() ? counterpart : parameterId);
}

void AstralayEditor::showPrompt (juce::Component& over, juce::Component* focusAfterwards, const juce::String& title,
                                 const juce::String& help, const juce::String& text,
                                 std::function<juce::String (const juce::String&)> commit)
{
    closePrompt (false);
    promptFocus = focusAfterwards;
    prompt = ui::TypeInField::show (over, title, help, text);

    if (prompt == nullptr)
        return;

    prompt->onCommit = std::move (commit);
    prompt->onClose = [this] (bool refocus) { closePrompt (refocus); };
}

void AstralayEditor::closePrompt (bool refocus)
{
    if (prompt == nullptr)
        return;

    ui::TypeInField::dismiss (prompt);

    if (refocus && promptFocus != nullptr)
        promptFocus->grabKeyboardFocus();
}

void AstralayEditor::selectTap (int tapIndex)
{
    selectedTap = juce::jlimit (0, params::numTaps - 1, tapIndex);
    processor.setSelectedTap (selectedTap);

    tapGroup.setTitle ("Tap " + juce::String (selectedTap + 1));
    tapGroup.repaint();

    tapSelector.setSelectedId (selectedTap + 1, juce::dontSendNotification);
    tapEnabled.bind (*state.getParameter (params::tapId (selectedTap, params::tap::enabled)), params::tap::enabled);

    for (auto& row : sliderRows)
        if (row->perTap)
            bindRow (*row);
}

void AstralayEditor::setSynced (bool shouldBeSynced)
{
    if (shouldBeSynced == synced)
        return;

    synced = shouldBeSynced;

    for (auto& row : sliderRows)
        if (row->syncSuffix.isNotEmpty())
            bindRow (*row);
}

void AstralayEditor::refreshTapName (int tapIndex, bool on)
{
    // on comes from the change notification: the parameter store's copy of the value may not have
    // been updated yet, since JUCE notifies the most recently added listeners first.
    tapSelector.changeItemText (tapIndex + 1, "Tap " + juce::String (tapIndex + 1) + (on ? ", on" : ", off"));

    // changeItemText doesn't refresh the displayed text of the selected item.
    if (tapIndex == selectedTap)
    {
        tapSelector.setSelectedId (0, juce::dontSendNotification);
        tapSelector.setSelectedId (tapIndex + 1, juce::dontSendNotification);
    }
}

//==============================================================================
bool AstralayEditor::keyPressed (const juce::KeyPress& key)
{
    if (handleMidiKey (key)) return true;
    const auto mods = key.getModifiers();

    if (keyLayer.handleKey (key))
        return true;

   #if JUCE_MAC
    if (juce::Component::getCurrentlyFocusedComponent() == nullptr && handleKeyWithoutFocus (key))
        return true;

    // Hosts keep Cmd+comma and Cmd+period for themselves, so the group keys use Option.
    const auto groupModifier = mods.isAltDown() && ! mods.isCommandDown() && ! mods.isCtrlDown() && ! mods.isShiftDown();
    const auto shortcutModifier = mods.isCommandDown() && ! mods.isAltDown() && ! mods.isShiftDown();
   #else
    const auto groupModifier = mods.isAltDown() && ! mods.isCtrlDown() && ! mods.isShiftDown();
    const auto shortcutModifier = groupModifier;
   #endif

    if (groupModifier)
    {
        const auto code = key.getKeyCode();
        const auto character = key.getTextCharacter();

        if (code == '.' || character == '.')
        {
            jumpToGroup (1);
            return true;
        }

        if (code == ',' || character == ',')
        {
            jumpToGroup (-1);
            return true;
        }
    }

    // Ctrl on Windows, Cmd on macOS.
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    if (key == juce::KeyPress ('z', command, 0))         { undo();           return true; }
    if (key == juce::KeyPress ('z', command | shift, 0)) { redo();           return true; }
    if (key == juce::KeyPress ('s', command, 0))         { showSaveDialog(); return true; }
    if (key == juce::KeyPress ('o', command, 0))         { showLoadMenu();   return true; }

    if (key == juce::KeyPress ('y', command, 0))
    {
        toggleAndAnnounce (params::global::sync, "Host sync");
        return true;
    }

    // Stands in for the applications key, which JUCE doesn't reliably receive on Windows.
    if (! mods.isCommandDown() && (key.getTextCharacter() == ']' || (key.getKeyCode() == ']' && ! mods.isAnyModifierKeyDown())))
        return showContextMenuForFocus();

    // The performance area has its own meanings for the remaining keys.
    if (performancePad.hasKeyboardFocus (true))
        return false;

    // Not while typing, since arming rebinds the sliders.
    if (shortcutModifier && (key.getKeyCode() == 'm' || key.getKeyCode() == 'M')
        && dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) == nullptr)
    {
        processor.getMidiMappings().cancelLearn();
        keyLayer.open (key, "Arm?", [this] (const juce::KeyPress& next) { return handleArmKey (next); });
        return true;
    }

    if (shortcutModifier && (key.getKeyCode() == 'f' || key.getKeyCode() == 'F'))
    {
        toggleAndAnnounce (params::global::freeze, "Freeze");
        return true;
    }

    if (handleClipboardKey (key))
        return true;

    if (const auto tap = ui::tapIndexForKey (key); tap >= 0)
    {
        switchToTap (tap);
        return true;
    }

    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return false;

    const auto code = key.getKeyCode();

    if (code == '-' || code == '_')
    {
        switchToTap ((selectedTap + params::numTaps - 1) % params::numTaps);
        return true;
    }

    if (code == '=' || code == '+')
    {
        switchToTap ((selectedTap + 1) % params::numTaps);
        return true;
    }

    if (code == juce::KeyPress::backspaceKey && ! mods.isShiftDown())
    {
        toggleSelectedTap();
        return true;
    }

    return false;
}

void AstralayEditor::switchToTap (int tapIndex)
{
    if (tapIndex == selectedTap)
        return;

    const auto onSelector = tapSelector.hasKeyboardFocus (true);

   #if JUCE_MAC
    selectTap (tapIndex);

    if (onSelector)
        if (auto* handler = tapSelector.getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);

    // VoiceOver doesn't queue an announcement behind the focused control's value change: it drops
    // one or the other, and often both. So the number and the focused control's value for the new
    // tap go out as a single announcement, once the value change has been sent, interrupting
    // whatever VoiceOver made of that.
    const auto text = onSelector ? tapSelector.getText()
                                 : "Tap " + juce::String (selectedTap + 1) + focusedTapControlValue();
    const auto id = ++tapSwitchAnnouncement;

    juce::Timer::callAfterDelay (100, [safeThis = juce::Component::SafePointer<AstralayEditor> (this), text, id]
    {
        // Only the last of a run of quick switches is spoken.
        if (safeThis != nullptr && safeThis->tapSwitchAnnouncement == id)
            safeThis->announcer.announce (text);
    });
   #else
    // The tap selector reads out its own new value, so the number is only announced elsewhere.
    // It doesn't interrupt, so the focused control's value for the new tap follows it.
    if (! onSelector)
        announcer.announce ("Tap " + juce::String (tapIndex + 1), false);

    selectTap (tapIndex);

    if (onSelector)
        if (auto* handler = tapSelector.getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
   #endif
}

#if JUCE_MAC
juce::String AstralayEditor::focusedTapControlValue() const
{
    if (tapEnabled.hasKeyboardFocus (true))
        return tapEnabled.getToggleState() ? ", on" : ", off";

    for (auto& row : sliderRows)
        if (row->perTap && row->slider.hasKeyboardFocus (true))
            return ", " + row->slider.getTextFromValue (row->slider.getValue());

    return {};
}
#endif

void AstralayEditor::toggleSelectedTap()
{
    auto* enabled = state.getParameter (params::tapId (selectedTap, params::tap::enabled));
    const auto on = enabled->getValue() < 0.5f;

    enabled->beginChangeGesture();
    enabled->setValueNotifyingHost (on ? 1.0f : 0.0f);
    enabled->endChangeGesture();

    announce ("Tap " + juce::String (selectedTap + 1) + (on ? " on" : " off"));
}

void AstralayEditor::toggleAndAnnounce (const char* parameterId, const juce::String& name)
{
    auto* parameter = state.getParameter (parameterId);
    const auto on = parameter->getValue() < 0.5f;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (on ? 1.0f : 0.0f);
    parameter->endChangeGesture();

    announce (name + (on ? " on" : " off"));
}

void AstralayEditor::holdFreeze (bool held)
{
    // One gesture from press to release, so the host can record it, kept out of the undo history.
    const astralay::state::History::ScopedSuspend suspend (processor.getHistory());
    auto* freeze = state.getParameter (params::global::freeze);

    if (held)
    {
        freeze->beginChangeGesture();
        freeze->setValueNotifyingHost (1.0f);
    }
    else
    {
        freeze->setValueNotifyingHost (0.0f);
        freeze->endChangeGesture();
    }
}

bool AstralayEditor::handleClipboardKey (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier;
    const auto copy = key == juce::KeyPress ('c', command, 0);
    const auto paste = key == juce::KeyPress ('v', command, 0);
    const auto pasteToAll = key == juce::KeyPress ('v', command | juce::ModifierKeys::shiftModifier, 0);

    if (! (copy || paste || pasteToAll))
        return false;

    // The tap selector and on/off toggle stand for the whole tap; a slider for its own setting.
    juce::String suffix, name = "Tap";

    if (! (tapSelector.hasKeyboardFocus (true) || tapEnabled.hasKeyboardFocus (true)))
    {
        for (const auto& row : sliderRows)
            if (row->perTap && row->slider.hasKeyboardFocus (true))
                suffix = synced && row->syncSuffix.isNotEmpty() ? row->syncSuffix : row->suffix;

        if (suffix.isEmpty())
            return false;

        name = params::tapParameterName (selectedTap, suffix.toRawUTF8())
                   .fromFirstOccurrenceOf ("Tap " + juce::String (selectedTap + 1) + " ", false, false);
    }

    if (copy)
    {
        processor.copyTapSettings (selectedTap, suffix);
        announce (name + " copied");
    }
    else if (processor.pasteTapSettings (pasteToAll ? AstralayProcessor::allTaps : selectedTap, suffix))
    {
        announce (name + (pasteToAll ? " pasted to all" : " pasted"));
    }
    else
    {
        announce ("Can't paste");
    }

    return true;
}

void AstralayEditor::undo()
{
    processor.getMidiMappings().flushCapture();
    announce (processor.getHistory().undo());
}

void AstralayEditor::redo()
{
    processor.getMidiMappings().flushCapture();
    announce (processor.getHistory().redo());
}

void AstralayEditor::showSaveDialog()
{
    const auto folder = astralay::state::Presets::userFolder();
    folder.createDirectory();

    const auto suggestion = folder.getChildFile (juce::File::createLegalFileName (processor.getPresetName()))
                                  .withFileExtension (astralay::state::Presets::fileExtension);

    fileChooser = std::make_unique<juce::FileChooser> ("Save preset", suggestion,
                                                       juce::String ("*") + astralay::state::Presets::fileExtension, true);

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync (chooserFlags, [safeThis = SafePointer<AstralayEditor> (this)] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (safeThis == nullptr || file == juce::File())
            return;

        safeThis->announce (safeThis->processor.savePresetFile (file.withFileExtension (astralay::state::Presets::fileExtension)));
    });
}

void AstralayEditor::showLoadMenu()
{
    processor.getMidiMappings().cancelLearn();
    juce::PopupMenu factoryMenu;
    const auto& factory = astralay::state::Presets::factory();

    for (int i = 0; i < (int) factory.size(); ++i)
        factoryMenu.addItem (i + 1, factory[(size_t) i].name);

    juce::PopupMenu menu;
    menu.addSubMenu ("Factory presets", factoryMenu);
    menu.addItem (fromFileItemId, "From file...");

    showMenu (menu, loadButton, [safeThis = SafePointer<AstralayEditor> (this)] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (result == fromFileItemId)
            safeThis->showLoadDialog();
        else
            safeThis->announce (safeThis->processor.loadFactoryPreset (result - 1));
    });
}

void AstralayEditor::showLoadDialog()
{
    processor.getMidiMappings().cancelLearn();
    const auto folder = astralay::state::Presets::userFolder();
    folder.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser> ("Load preset", folder,
                                                       juce::String ("*") + astralay::state::Presets::fileExtension, true);

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (chooserFlags, [safeThis = SafePointer<AstralayEditor> (this)] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (safeThis == nullptr || file == juce::File())
            return;

        safeThis->announce (safeThis->processor.loadPresetFile (file));
    });
}

bool AstralayEditor::showContextMenuForFocus()
{
    if (auto* target = ui::findContextMenu (juce::Component::getCurrentlyFocusedComponent()))
    {
        target->showContextMenu();
        return true;
    }

    return false;
}

void AstralayEditor::showOutputClipMenu (juce::Component& target)
{
    // In params::OutputClip order.
    static const std::array<const char*, 3> items { "Clip at +18 dBFS", "Clip at 0 dBFS", "No clipping" };

    const auto current = (int) processor.getOutputClip();

    juce::PopupMenu menu;

    for (int i = 0; i < (int) items.size(); ++i)
        menu.addItem (i + 1, items[(size_t) i], true, i == current);
    const auto midiId = appendMidiMenu (menu, target);

    showMenu (menu, target, [safeThis = SafePointer<AstralayEditor> (this), midiId] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (safeThis->midiMenuResult (result, midiId)) return;
        safeThis->processor.setOutputClip ((params::OutputClip) (result - 1));
        safeThis->announce (items[(size_t) (result - 1)]);
    });
}

void AstralayEditor::showPitchModeMenu (juce::Component& target)
{
    // For the tap selected when the menu opened, even if another is selected before a choice is made.
    auto* parameter = state.getParameter (params::tapId (selectedTap, params::tap::pitchMode));
    const auto choices = parameter->getAllValueStrings();
    const auto current = juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));

    juce::PopupMenu menu;

    for (int i = 0; i < choices.size(); ++i)
        menu.addItem (i + 1, choices[i], true, i == current);
    const auto midiId = appendMidiMenu (menu, target);

    showMenu (menu, target, [safeThis = SafePointer<AstralayEditor> (this), parameter, choices, current, midiId] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (safeThis->midiMenuResult (result, midiId)) return;
        if (result - 1 != current)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) (result - 1)));
            parameter->endChangeGesture();
        }

        safeThis->announce (choices[result - 1]);
    });
}

void AstralayEditor::refreshPresetName()
{
    auto text = "Preset: " + processor.getPresetName();

    // A word rather than an asterisk, which screen readers often skip.
    if (processor.isPresetModified())
        text << ", modified";

    presetName.setText (text, juce::dontSendNotification);
}

void AstralayEditor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property.toString().startsWith ("preset"))
    {
        if (juce::MessageManager::existsAndIsCurrentThread())
            refreshPresetName();
        else
            triggerAsyncUpdate();
    }
}

void AstralayEditor::valueTreeRedirected (juce::ValueTree& tree)
{
    if (tree == state.state)
        triggerAsyncUpdate();
}

void AstralayEditor::handleAsyncUpdate()
{
    // A host can replace state off the message thread. Defer the UI work until after the
    // notification, letting parameter attachments reconnect as well, and read the latest state
    // so a series of restores cannot leave an older preset or tap on screen.
    const auto restoredTap = processor.getSelectedTap();
    if (restoredTap != selectedTap)
        selectTap (restoredTap);

    refreshPresetName();
}

void AstralayEditor::jumpToGroup (int direction)
{
    const std::array<juce::Component*, 5> groups { &mainGroup, &tapGroup, &macrosGroup, &globalGroup, &performanceGroup };
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();

    int current = -1;

    for (int i = 0; i < (int) groups.size(); ++i)
        if (focused != nullptr && groups[(size_t) i]->isParentOf (focused))
            current = i;

    const auto count = (int) groups.size();
    const auto next = current < 0 ? (direction > 0 ? 0 : count - 1)
                                  : (current + direction + count) % count;

    juce::KeyboardFocusTraverser traverser;

    if (auto* first = traverser.getDefaultComponent (groups[(size_t) next]))
        first->grabKeyboardFocus();
}

#if JUCE_MAC
bool AstralayEditor::handleKeyWithoutFocus (const juce::KeyPress& key)
{
    auto* peer = getPeer();

    if (peer == nullptr)
        return false;

    const auto mods = key.getModifiers();
    const auto isTab = key.isKeyCode (juce::KeyPress::tabKey) && ! mods.isCommandDown() && ! mods.isCtrlDown() && ! mods.isAltDown();
    const auto forwards = ! mods.isShiftDown();

    auto* last = peer->getLastFocusedSubcomponent();

    if (last == nullptr || last == this || ! isParentOf (last) || ! last->getWantsKeyboardFocus())
    {
        // Nothing has had focus yet, so Tab starts from the nearest end.
        if (! isTab)
            return false;

        const auto stops = juce::KeyboardFocusTraverser().getAllComponents (this);

        if (stops.empty())
            return false;

        (forwards ? stops.front() : stops.back())->grabKeyboardFocus();
        return true;
    }

    last->grabKeyboardFocus();

    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    if (focused == nullptr || ! isParentOf (focused))
        return false;

    if (isTab)
    {
        focused->moveKeyboardFocusToSibling (forwards);
        return true;
    }

    return focused->keyPressed (key);
}
#endif

void AstralayEditor::announce (const juce::String& text)
{
    announcer.announce (text);
}

void AstralayEditor::globalFocusChanged (juce::Component* focused)
{
    if (midiKeyTarget != nullptr) midiKeyTarget->removeKeyListener (this);
    midiKeyTarget = focused != nullptr && isParentOf (focused) ? focused : nullptr;
    if (midiKeyTarget != nullptr) midiKeyTarget->addKeyListener (this);
    if (focusOutline != nullptr)
        focusOutline->setTarget (focused);
}

//==============================================================================
void AstralayEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);
}

void AstralayEditor::layoutColumn (juce::Rectangle<int> area, const std::vector<LayoutItem>& items,
                                   int height, int spacing, float labelFraction)
{
    for (const auto& item : items)
    {
        auto row = area.removeFromTop (height);
        area.removeFromTop (spacing);

        if (item.label != nullptr)
            item.label->setBounds (row.removeFromLeft ((int) ((float) row.getWidth() * labelFraction)));

        item.control->setBounds (row);
    }
}

void AstralayEditor::resized()
{
    using namespace ui::sizes;

    content.setBounds (0, 0, baseWidth, baseHeight);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseWidth));

    auto area = juce::Rectangle<int> (0, 0, baseWidth, baseHeight).reduced (10);

    // Performance: a strip along the bottom.
    performanceGroup.setBounds (area.removeFromBottom (86));
    performancePad.setBounds (performanceGroup.getContentBounds());
    area.removeFromBottom (10);

    // Main
    mainGroup.setBounds (area.removeFromTop (70));
    area.removeFromTop (10);
    {
        auto row = mainGroup.getContentBounds();

        for (auto* button : { &mainMenuButton, &midiLearnButton, &undoButton, &redoButton, &saveButton, &loadButton })
        {
            button->setBounds (row.removeFromLeft (110));
            row.removeFromLeft (gap);
        }

        presetName.setBounds (row.withTrimmedLeft (10));
    }

    globalGroup.setBounds (area.removeFromRight (380));
    area.removeFromRight (10);
    macrosGroup.setBounds (area.removeFromRight (230));
    area.removeFromRight (10);
    tapGroup.setBounds (area);

    // Macros: one group each, stacked, with the Arm button beside the value slider.
    {
        auto inner = macrosGroup.getContentBounds();
        const auto count = (int) macroControls.size();
        const auto groupHeight = (inner.getHeight() - (count - 1) * gap) / juce::jmax (1, count);

        for (auto& controls : macroControls)
        {
            controls->group.setBounds (inner.removeFromTop (groupHeight));
            inner.removeFromTop (gap);

            auto row = controls->group.getContentBounds().removeFromTop (rowHeight);
            controls->arm.setBounds (row.removeFromLeft (76));
            row.removeFromLeft (gap);
            controls->value.setBounds (row);
        }
    }

    // Tap: selector row, the basic controls in two columns, then the glitch sections in a 3 x 3 grid.
    {
        auto inner = tapGroup.getContentBounds();

        auto selectorRow = inner.removeFromTop (34);
        tapSelectorLabel.setBounds (selectorRow.removeFromLeft (50));
        tapSelector.setBounds (selectorRow.removeFromLeft (200));
        selectorRow.removeFromLeft (20);
        tapEnabled.setBounds (selectorRow.removeFromLeft (200));
        inner.removeFromTop (8);

        auto basics = inner.removeFromTop (3 * rowHeight + 2 * gap);
        const auto columnWidth = (basics.getWidth() - 20) / 2;
        std::vector<LayoutItem> left, right;

        for (size_t i = 0; i < tapBasics.size(); ++i)
            (i % 2 == 0 ? left : right).push_back (tapBasics[i]);

        layoutColumn (basics.removeFromLeft (columnWidth), left, rowHeight, gap, 0.35f);
        layoutColumn (basics.removeFromRight (columnWidth), right, rowHeight, gap, 0.35f);
        inner.removeFromTop (10);

        constexpr int columns = 3, spacing = 8;
        const auto cellWidth = (inner.getWidth() - (columns - 1) * spacing) / columns;
        const auto cellHeight = (inner.getHeight() - (columns - 1) * spacing) / columns;

        for (size_t i = 0; i < glitchSections.size(); ++i)
        {
            const auto column = (int) i % columns;
            const auto rowIndex = (int) i / columns;
            auto& section = glitchSections[i];

            section.group->setBounds (inner.getX() + column * (cellWidth + spacing),
                                      inner.getY() + rowIndex * (cellHeight + spacing),
                                      cellWidth, cellHeight);
            layoutColumn (section.group->getContentBounds(), section.items, glitchRowHeight, 3, 0.48f);
        }
    }

    // Global: the three sub-groups stacked.
    {
        auto inner = globalGroup.getContentBounds();
        const auto groupHeight = [] (size_t rows) { return 8 + (int) headingHeight + 6 + (int) rows * (rowHeight + 5) + 8; };

        for (auto [group, items] : { std::pair { &timingGroup, &timingItems },
                                     std::pair { &engineGroup, &engineItems },
                                     std::pair { &outputGroup, &outputItems } })
        {
            group->setBounds (inner.removeFromTop (groupHeight (items->size())));
            inner.removeFromTop (8);
            layoutColumn (group->getContentBounds(), *items, rowHeight, 5, 0.42f);
        }
    }

    if (focusOutline != nullptr)
        focusOutline->setBounds (content.getLocalBounds());
}
