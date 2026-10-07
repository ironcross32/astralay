#include "PluginEditor.h"
#include "PluginProcessor.h"

using astralay::state::MidiMappings;

namespace
{
constexpr int learnItem = 20000, removeItem = 20001, cancelItem = 20002;
}

void AstralayEditor::showMenu (juce::PopupMenu& menu, juce::Component& target, std::function<void (int)> callback)
{
    auto* previous = juce::Component::getCurrentlyModalComponent();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), std::move (callback));
    if (auto* window = juce::Component::getCurrentlyModalComponent(); window != nullptr && window != previous)
    {
        // Keep keyboard and accessibility highlighting in step from the first item.
        window->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        window->addKeyListener (this);
        std::erase_if (midiMenuKeyTargets, [] (const auto& item) { return item == nullptr; });
        midiMenuKeyTargets.emplace_back (window);
    }
}

bool AstralayEditor::keyPressed (const juce::KeyPress& key, juce::Component* origin)
{
    if (origin != nullptr && ! isParentOf (origin) && key.isKeyCode (juce::KeyPress::rightKey))
    {
        const auto used = origin->keyPressed (key);
        timerCallback(); // A keyboard-opened submenu needs the shortcut listener immediately.
        return used;
    }
    // A popup retains its owner's keyboard focus. Enter there selects the menu item, not the
    // underlying control as a learn target.
    if (key.isKeyCode (juce::KeyPress::returnKey) && origin != nullptr && ! isParentOf (origin)) return false;
    if (handleMidiKey (key))
    {
        if (key.isKeyCode (juce::KeyPress::escapeKey) && origin != nullptr && ! isParentOf (origin))
            juce::PopupMenu::dismissAllActiveMenus();
        return true;
    }
    return false;
}

void AstralayEditor::timerCallback()
{
    // JUCE submenus are separate desktop windows. Track the modal stack while one of our
    // menus is open so Escape and the learn shortcut also work inside nested menus.
    if (std::none_of (midiMenuKeyTargets.begin(), midiMenuKeyTargets.end(),
                     [] (const auto& item) { return item != nullptr && item->isCurrentlyModal (false); })) return;
    for (int i = 0; i < juce::Component::getNumCurrentlyModalComponents(); ++i)
    {
        auto* window = juce::Component::getCurrentlyModalComponent (i);
        auto* handler = window != nullptr ? window->getAccessibilityHandler() : nullptr;
        if (handler == nullptr || handler->getRole() != juce::AccessibilityRole::popupMenu) continue;
        if (std::none_of (midiMenuKeyTargets.begin(), midiMenuKeyTargets.end(),
                         [window] (const auto& item) { return item.getComponent() == window; }))
        {
            window->addKeyListener (this);
            midiMenuKeyTargets.emplace_back (window);
        }
    }
}

juce::String AstralayEditor::midiTarget (juce::Component* control) const
{
    using namespace astralay::params;
    for (const auto& row : sliderRows)
        if (control == &row->slider) return row->perTap ? tapId (selectedTap, row->suffix.toRawUTF8()) : row->suffix;
    for (int m = 0; m < (int) macroControls.size(); ++m)
        if (control == &macroControls[(size_t) m]->value) return macroId (m);
    if (control == &tapEnabled) return tapId (selectedTap, tap::enabled);
    if (control == &syncToggle) return global::sync;
    if (control == &freezeToggle) return global::freeze;
    if (control == &sustainToggle) return global::freezeSustain;
    if (control == &reproducibleToggle) return global::reproducible;
    if (control == &tapeStopToggle) return global::tapeStop;
    if (control == &placementChoice) return global::placement;
    return {};
}

void AstralayEditor::setupMidiControls()
{
    startTimer (20);
    processor.getMidiMappings().setAnnouncer ([safe = SafePointer<AstralayEditor> (this)] (const juce::String& text)
    {
        if (safe == nullptr) return;
        if (juce::MessageManager::existsAndIsCurrentThread()) safe->announce (text);
        else juce::MessageManager::callAsync ([safe, text] { if (safe != nullptr) safe->announce (text); });
    });
    const auto configure = [this] (auto& control)
    {
        control.selectMidiTarget = [this, &control] { return selectMidiTarget (control); };
        if (! control.hasContextMenu()) control.setContextMenu ([this, &control] { showControlMenu (control); });
    };
    for (auto& row : sliderRows) configure (row->slider);
    for (auto& macro : macroControls) { configure (macro->value); configure (macro->arm); }
    for (auto* toggle : { &tapEnabled, &syncToggle, &freezeToggle, &sustainToggle, &reproducibleToggle, &tapeStopToggle }) configure (*toggle);
    configure (placementChoice);
    configure (tapSelector);
    configure (presetName);
    for (auto* button : { &mainMenuButton, &midiLearnButton, &undoButton, &redoButton, &saveButton, &loadButton }) configure (*button);
    performancePad.setContextMenu ([this] { showControlMenu (performancePad); });
}

void AstralayEditor::startMidiLearn (const juce::String& target)
{
    auto& midi = processor.getMidiMappings();
    if (! midi.eligible (target)) { announce ("This control cannot be MIDI learned"); return; }
    keyLayer.close();
    const auto disarmed = armedMacro >= 0;
    if (armedMacro >= 0) armMacro (-1);
    midi.learn (target);
    announcer.announce ("MIDI learn", ! disarmed);
}
void AstralayEditor::toggleMidiLearn()
{
    auto& midi = processor.getMidiMappings();
    midi.flushCapture();
    if (midi.learnState() != MidiMappings::Learn::inactive) { midi.cancelLearn(); return; }
    keyLayer.close();
    const auto disarmed = armedMacro >= 0;
    if (armedMacro >= 0) armMacro (-1);
    midi.waitForTarget();
    announcer.announce ("Waiting for target", ! disarmed);
}
bool AstralayEditor::selectMidiTarget (juce::Component& control)
{
    if (processor.getMidiMappings().learnState() != MidiMappings::Learn::target) return false;
    const auto target = midiTarget (&control);
    if (target.isEmpty()) return false;
    control.grabKeyboardFocus();
    startMidiLearn (target);
    return true;
}
bool AstralayEditor::handleMidiKey (const juce::KeyPress& key)
{
    auto& midi = processor.getMidiMappings();
   #if JUCE_MAC
    const auto modifier = juce::ModifierKeys::commandModifier;
   #else
    const auto modifier = juce::ModifierKeys::altModifier;
   #endif
    if (key == juce::KeyPress ('l', modifier, 0))
    {
        midi.flushCapture();
        if (midi.learnState() != MidiMappings::Learn::inactive) midi.cancelLearn();
        else startMidiLearn (midiTarget (juce::Component::getCurrentlyFocusedComponent()));
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::escapeKey) && midi.learnState() != MidiMappings::Learn::inactive)
    {
        midi.cancelLearn();
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::returnKey))
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent()) return selectMidiTarget (*focused);
    return false;
}
juce::String AstralayEditor::appendMidiMenu (juce::PopupMenu& menu, juce::Component& owner)
{
    auto& midi = processor.getMidiMappings();
    midi.flushCapture();
    const auto target = midiTarget (&owner);
    const auto active = midi.learnState() != MidiMappings::Learn::inactive;
    if (active || target.isNotEmpty())
    {
        if (menu.getNumItems() > 0) menu.addSeparator();
        menu.addItem (active ? cancelItem : learnItem, active ? "Cancel MIDI learn" : "MIDI learn");
    }
    if (target.isNotEmpty() && midi.bound (target)) menu.addItem (removeItem, "Remove MIDI mapping");
    return target;
}
bool AstralayEditor::midiMenuResult (int result, const juce::String& target)
{
    auto& midi = processor.getMidiMappings();
    if (result == cancelItem) { midi.cancelLearn(); return true; }
    if (result == learnItem)
    {
        if (midi.learnState() != MidiMappings::Learn::inactive) midi.cancelLearn();
        else startMidiLearn (target);
        return true;
    }
    if (result == removeItem)
    {
        midi.remove (target);
        announce ("MIDI mapping removed");
        return true;
    }
    return false;
}
void AstralayEditor::showControlMenu (juce::Component& control)
{
    juce::PopupMenu menu;
    const auto id = appendMidiMenu (menu, control);
    if (menu.getNumItems() == 0) menu.addItem (1, "This control cannot be MIDI learned", false);
    showMenu (menu, control, [safe = SafePointer<AstralayEditor> (this), id] (int result)
    { if (safe != nullptr) safe->midiMenuResult (result, id); });
}
void AstralayEditor::confirmMidi (const juce::String& title, const juce::String& message,
                                 const juce::StringArray& buttons, std::function<void (int)> callback)
{
    auto* alert = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon, this);
    for (int i = 0; i < buttons.size(); ++i)
        alert->addButton (buttons[i], i == buttons.size() - 1 ? 0 : i + 1,
                          juce::KeyPress (i == buttons.size() - 1 ? juce::KeyPress::escapeKey
                                                                : i == 0 ? juce::KeyPress::returnKey : 0));
    alert->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = SafePointer<AstralayEditor> (this), callback = std::move (callback)] (int result)
        { if (safe != nullptr) callback (result); }), true);
}
void AstralayEditor::showMainMenu()
{
    auto& midi = processor.getMidiMappings();
    const auto current = midi.current();
    const auto files = midi.scan();
    const auto defaultFile = midi.defaultFile();
    juce::PopupMenu menu, sub;
    juce::PopupMenu smoothingMenu;
    for (int i = 0; i < 5; ++i)
    {
        const auto mode = (astralay::state::MidiSmoothing::Mode) i;
        smoothingMenu.addItem (10 + i, astralay::state::MidiSmoothing::label (mode), true,
                              midi.smoothingMode() == mode);
    }
    sub.addSubMenu ("Smoothing", smoothingMenu);
    sub.addItem (2, "Save MIDI mapping", ! current.bindings.empty());
    sub.addItem (3, "Save MIDI mapping as...", ! current.bindings.empty());
    sub.addItem (4, "Set current mapping as default");
    sub.addItem (5, "Clear default MIDI mapping");
    sub.addItem (6, "Clear mapping", ! current.bindings.empty());
    if (midi.startupStatus().isNotEmpty()) sub.addItem (7, midi.startupStatus());
    if (! files.empty()) sub.addSeparator();
    for (size_t i = 0; i < files.size(); ++i)
    {
        auto name = files[i].getFileNameWithoutExtension();
        if (files[i] == current.file) name += ", current";
        if (files[i] == defaultFile) name += ", default";
        sub.addItem (100 + (int) i, name);
    }
    menu.addSubMenu ("MIDI", sub);
    menu.addItem (1, "Accessibility settings...");
    showMenu (menu, mainMenuButton, [safe = SafePointer<AstralayEditor> (this), files] (int result)
    {
        if (safe == nullptr || result == 0) return;
        auto& mappings = safe->processor.getMidiMappings();
        if (result == 1) safe->showAccessibilitySettings();
        else if (result >= 10 && result <= 14)
        {
            const auto mode = (astralay::state::MidiSmoothing::Mode) (result - 10);
            const auto status = mappings.setSmoothingMode (mode);
            safe->announce (status.wasOk() ? "MIDI smoothing " + astralay::state::MidiSmoothing::label (mode)
                                          : status.getErrorMessage());
        }
        else if (result == 2 || result == 3) safe->saveMidiMapping (result == 3);
        else if (result == 4) safe->defaultMidiMapping();
        else if (result == 5)
        {
            const auto status = mappings.clearDefault();
            safe->announce (status.wasOk() ? "Default MIDI mapping cleared" : status.getErrorMessage());
        }
        else if (result == 6)
        {
            mappings.cancelLearn();
            safe->confirmMidi ("Clear mapping", "Discard all current MIDI bindings, including unsaved mapping edits?",
                               { "Clear", "Cancel" }, [safe] (int answer)
            {
                if (answer == 1) { safe->processor.getMidiMappings().clear(); safe->announce ("MIDI mapping cleared"); }
            });
        }
        else if (result == 7) safe->announce (mappings.startupStatus());
        else if (juce::isPositiveAndBelow (result - 100, (int) files.size())) safe->loadMidiMapping (files[(size_t) (result - 100)]);
    });
}
void AstralayEditor::saveMidiMapping (bool saveAs, std::function<void()> after)
{
    auto& midi = processor.getMidiMappings();
    midi.cancelLearn();
    const auto current = midi.current();
    if (current.bindings.empty()) { announce ("An empty mapping cannot be saved"); return; }
    if (! saveAs && current.file != juce::File()) { writeMidiMapping (current.file, std::move (after)); return; }
    showPrompt (mainMenuButton, &mainMenuButton, "Save MIDI mapping, name",
                "Type a portable filename and press Enter, or Escape to cancel.",
                current.file == juce::File() ? juce::String() : current.file.getFileNameWithoutExtension(),
                [safe = SafePointer<AstralayEditor> (this), after] (const juce::String& text) -> juce::String
    {
        juce::String filename;
        const auto error = MidiMappings::filename (text, filename);
        if (error.isNotEmpty()) return error;
        if (safe != nullptr)
        {
            const auto file = safe->processor.getMidiMappings().folder().getChildFile (filename);
            // Let the type-in close before opening any overwrite confirmation.
            juce::MessageManager::callAsync ([safe, file, after] { if (safe != nullptr) safe->writeMidiMapping (file, after); });
        }
        return {};
    });
}
void AstralayEditor::writeMidiMapping (const juce::File& file, std::function<void()> after)
{
    auto& midi = processor.getMidiMappings();
    const auto current = midi.current();
    const auto write = [safe = SafePointer<AstralayEditor> (this), file, after] (bool overwrite)
    {
        if (safe == nullptr) return;
        const auto result = safe->processor.getMidiMappings().save (file, overwrite);
        safe->announce (result.wasOk() ? "Saved MIDI mapping " + file.getFileNameWithoutExtension() : result.getErrorMessage());
        if (result.wasOk() && after) after();
    };
    if (file.existsAsFile() && (file != current.file || midi.externallyChanged()))
    {
        confirmMidi ("Overwrite MIDI mapping", file == current.file ? "The file changed externally. Replace it with the current mapping?"
                                                                   : "Replace " + file.getFileName() + "?",
                     { "Replace", "Cancel" }, [write] (int answer) { if (answer == 1) write (true); });
    }
    else write (false);
}
void AstralayEditor::loadMidiMapping (const juce::File& file)
{
    auto& midi = processor.getMidiMappings();
    midi.cancelLearn();
    const auto load = [safe = SafePointer<AstralayEditor> (this), file]
    {
        if (safe == nullptr) return;
        const auto status = safe->processor.getMidiMappings().load (file);
        safe->announce (status.wasOk() ? "Loaded MIDI mapping " + file.getFileNameWithoutExtension() : status.getErrorMessage());
    };
    const auto current = midi.current();
    if (! current.bindings.empty() && current.dirty)
        confirmMidi ("Unsaved MIDI mapping", "Save the current MIDI mapping before loading another?", { "Save", "Discard", "Cancel" },
                     [safe = SafePointer<AstralayEditor> (this), load] (int answer)
        {
            if (answer == 1) safe->saveMidiMapping (false, load);
            else if (answer == 2) load();
        });
    else load();
}
void AstralayEditor::defaultMidiMapping()
{
    auto& midi = processor.getMidiMappings();
    const auto current = midi.current();
    const auto setDefault = [safe = SafePointer<AstralayEditor> (this)]
    {
        if (safe == nullptr) return;
        const auto result = safe->processor.getMidiMappings().setDefault();
        safe->announce (result.wasOk() ? "Default MIDI mapping set" : result.getErrorMessage());
    };
    if (current.bindings.empty()) { setDefault(); return; }
    if (! current.file.existsAsFile()) { saveMidiMapping (true, setDefault); return; }
    if (current.dirty)
        confirmMidi ("Set default MIDI mapping", "The current mapping must be saved first. Save it and set it as the default?", { "Yes", "No" },
                     [safe = SafePointer<AstralayEditor> (this), setDefault] (int answer)
        { if (answer == 1) safe->saveMidiMapping (false, setDefault); });
    else if (midi.externallyChanged()) saveMidiMapping (false, setDefault);
    else setDefault();
}
