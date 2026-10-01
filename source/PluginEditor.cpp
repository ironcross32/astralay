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
    content.addAndMakeVisible (globalGroup);
    content.addAndMakeVisible (performanceGroup);

    mainGroup.setExplicitFocusOrder (1);
    tapGroup.setExplicitFocusOrder (2);
    globalGroup.setExplicitFocusOrder (3);
    performanceGroup.setExplicitFocusOrder (4);

    buildMainGroup();
    buildTapGroup();
    buildGlobalGroup();
    buildPerformanceGroup();

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

    const auto ratio = (double) ui::sizes::baseWidth / ui::sizes::baseHeight;
    setResizable (true, true);
    setResizeLimits (ui::sizes::baseWidth * 3 / 4, ui::sizes::baseHeight * 3 / 4,
                     ui::sizes::baseWidth * 2, ui::sizes::baseHeight * 2);
    getConstrainer()->setFixedAspectRatio (ratio);
    setSize (ui::sizes::baseWidth, ui::sizes::baseHeight);
}

AstralayEditor::~AstralayEditor()
{
    // Ends a held freeze while there is still an editor to end it.
    performancePad.releaseHeldKeys();

    state.state.removeListener (this);
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
    ui::describe (undoButton, "Undo", ui::helpFor (ui::helpKeys::undo));
    ui::describe (redoButton, "Redo", ui::helpFor (ui::helpKeys::redo));
    ui::describe (saveButton, "Save", ui::helpFor (ui::helpKeys::save));
    ui::describe (loadButton, "Load", ui::helpFor (ui::helpKeys::load));

    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    saveButton.onClick = [this] { showSaveDialog(); };
    loadButton.onClick = [this] { showLoadMenu(); };

    for (auto* button : { &undoButton, &redoButton, &saveButton, &loadButton })
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

void AstralayEditor::buildGlobalGroup()
{
    using namespace params::global;

    for (auto* group : { &timingGroup, &engineGroup, &outputGroup })
        globalGroup.addInOrder (*group);

    addToggle (timingGroup, timingItems, syncToggle, sync);
    addSliderRow (timingGroup, timingItems, "Glide time", glide, nullptr, false);
    addToggle (timingGroup, timingItems, freezeToggle, freeze);

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
    performancePad.onHoldFreeze = [this] (bool held) { holdFreeze (held); };
    performancePad.onToggleFreeze = [this] { toggleAndAnnounce (params::global::freeze, "Freeze"); };

    performanceGroup.addInOrder (performancePad);
}

//==============================================================================
void AstralayEditor::bindRow (SliderRow& row)
{
    const auto useSync = synced && row.syncSuffix.isNotEmpty();
    const auto key = useSync ? row.syncSuffix : row.suffix;
    const auto id = row.perTap ? params::tapId (selectedTap, key.toRawUTF8()) : key;

    auto* parameter = state.getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        row.slider.bind (*parameter, useSync ? Unit::plain : params::unitFor (key), key, useSync, 0);
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
    const auto mods = key.getModifiers();

   #if JUCE_MAC
    const auto groupModifier = mods.isCommandDown() && ! mods.isAltDown() && ! mods.isShiftDown();
   #else
    const auto groupModifier = mods.isAltDown() && ! mods.isCtrlDown() && ! mods.isShiftDown();
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

    if (groupModifier && (key.getKeyCode() == 'f' || key.getKeyCode() == 'F'))
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

    // The tap selector reads out its own new value, so the number is only announced elsewhere.
    // It doesn't interrupt, so the focused control's value for the new tap follows it.
    const auto onSelector = tapSelector.hasKeyboardFocus (true);

    if (! onSelector)
        announcer.announce ("Tap " + juce::String (tapIndex + 1), false);

    selectTap (tapIndex);

    if (onSelector)
        if (auto* handler = tapSelector.getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

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
    announce (processor.getHistory().undo());
}

void AstralayEditor::redo()
{
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
    juce::PopupMenu factoryMenu;
    const auto& factory = astralay::state::Presets::factory();

    for (int i = 0; i < (int) factory.size(); ++i)
        factoryMenu.addItem (i + 1, factory[(size_t) i].name);

    juce::PopupMenu menu;
    menu.addSubMenu ("Factory presets", factoryMenu);
    menu.addItem (fromFileItemId, "From file...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton),
                        [safeThis = SafePointer<AstralayEditor> (this)] (int result)
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
    for (auto* c = juce::Component::getCurrentlyFocusedComponent(); c != nullptr && c != this; c = c->getParentComponent())
    {
        if (auto* target = dynamic_cast<ui::ContextMenuTarget*> (c); target != nullptr && target->hasContextMenu())
        {
            target->showContextMenu();
            return true;
        }
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

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [safeThis = SafePointer<AstralayEditor> (this)] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

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

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [safeThis = SafePointer<AstralayEditor> (this), parameter, choices, current] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

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
        refreshPresetName();
}

void AstralayEditor::jumpToGroup (int direction)
{
    const std::array<juce::Component*, 4> groups { &mainGroup, &tapGroup, &globalGroup, &performanceGroup };
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

void AstralayEditor::announce (const juce::String& text)
{
    announcer.announce (text);
}

void AstralayEditor::globalFocusChanged (juce::Component* focused)
{
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

        for (auto* button : { &undoButton, &redoButton, &saveButton, &loadButton })
        {
            button->setBounds (row.removeFromLeft (110));
            row.removeFromLeft (gap);
        }

        presetName.setBounds (row.withTrimmedLeft (10));
    }

    globalGroup.setBounds (area.removeFromRight (380));
    area.removeFromRight (10);
    tapGroup.setBounds (area);

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
