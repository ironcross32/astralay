#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/Parameters.h"
#include <thread>

namespace
{
    juce::String titleOf (juce::Component& c)
    {
        if (auto* handler = c.getAccessibilityHandler())
            return handler->getTitle();

        return {};
    }

    /** Every keyboard stop in Tab order. */
    std::vector<juce::Component*> tabOrder (juce::Component& editor)
    {
        return juce::KeyboardFocusTraverser().getAllComponents (&editor);
    }

    juce::Component* findByTitle (juce::Component& editor, const juce::String& title)
    {
        for (auto* c : tabOrder (editor))
            if (titleOf (*c) == title)
                return c;

        return nullptr;
    }

    /** Titles of the accessible groups enclosing c, innermost first. */
    juce::StringArray groupPath (juce::Component& c)
    {
        juce::StringArray path;

        for (auto* handler = c.getAccessibilityHandler(); handler != nullptr; handler = handler->getParent())
            if (handler->getRole() == juce::AccessibilityRole::group)
                path.add (handler->getTitle());

        return path;
    }

    float valueOf (AstralayProcessor& processor, const juce::String& id)
    {
        return processor.getState().getRawParameterValue (id)->load();
    }
}

class EditorTests final : public juce::UnitTest
{
public:
    EditorTests() : juce::UnitTest ("Editor accessibility", "Astralay") {}

    void runTest() override
    {
        using namespace astralay::params;

        AstralayProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto& ed = *editor;

        // Accessibility handlers only exist for components on a native window. It stays hidden.
        ed.addToDesktop (juce::ComponentPeer::windowIsTemporary);

        beginTest ("Tab visits every control once, in spec order");
        {
            const auto stops = tabOrder (ed);
            juce::StringArray titles;

            for (auto* c : stops)
                titles.add (titleOf (*c));

            // Main 7, tap 2 + 6 basics + 33 glitch controls (synced stutter slices share rows), two for
            // each of the 8 macros, global 16, performance 1.
            expectEquals ((int) stops.size(), 81);

            if (stops.size() != 81)
                logMessage ("Tab order: " + titles.joinIntoString (" | "));

            const juce::StringArray expectedStart { "Main menu", "MIDI learn", "Undo", "Redo", "Save", "Load", "Preset: Init",
                                                    "Selected tap", "Tap 1 Enabled", "Tap 1 Time",
                                                    "Tap 1 Volume", "Tap 1 Pan", "Tap 1 Feedback",
                                                    "Tap 1 Low Cut", "Tap 1 High Cut",
                                                    "Tap 1 Reverse Probability", "Tap 1 Stutter Probability",
                                                    "Tap 1 Stutter Minimum Slice", "Tap 1 Stutter Maximum Slice" };

            for (int i = 0; i < expectedStart.size(); ++i)
                expectEquals (titles[i], expectedStart[i]);

            const juce::StringArray expectedEnd { "Host Sync", "Glide Time", "Freeze", "Freeze Sustain",
                                                  "Glitch Threshold", "Glitch Placement", "Buffer Size",
                                                  "Maximum Simultaneous Glitches", "Minimum Glitch Length",
                                                  "Maximum Glitch Length", "Reproducible Randomness", "Seed",
                                                  "Smear Amount", "Smear Size", "Mix", "Output Gain",
                                                  "Performance area" };

            for (int i = 0; i < expectedEnd.size(); ++i)
                expectEquals (titles[titles.size() - expectedEnd.size() + i], expectedEnd[i]);

            expectEquals (titles.indexOf ("Tap 1 Bit Crusher Maximum Rate Reduction"), 47);
        }

        beginTest ("Controls sit inside named groups for VoiceOver");
        {
            if (auto* feedback = findByTitle (ed, "Tap 1 Feedback"))
                expectEquals (groupPath (*feedback).joinIntoString ("/"), juce::String ("Tap 1"));
            else
                expect (false, "Feedback slider missing");

            if (auto* stutter = findByTitle (ed, "Tap 1 Stutter Probability"))
                expectEquals (groupPath (*stutter).joinIntoString ("/"), juce::String ("Stutter/Tap 1"));
            else
                expect (false, "Stutter slider missing");

            if (auto* glide = findByTitle (ed, "Glide Time"))
                expectEquals (groupPath (*glide).joinIntoString ("/"), juce::String ("Timing/Global"));
            else
                expect (false, "Glide slider missing");

            if (auto* undo = findByTitle (ed, "Undo"))
                expectEquals (groupPath (*undo).joinIntoString ("/"), juce::String ("Main"));
            else
                expect (false, "Undo button missing");

            if (auto* pad = findByTitle (ed, "Performance area"))
                expectEquals (groupPath (*pad).joinIntoString ("/"), juce::String ("Performance"));
            else
                expect (false, "Performance area missing");
        }

        beginTest ("Freeze sustain is accessible, operable and fits inside Global");
        {
            auto* control = findByTitle (ed, "Freeze Sustain");
            expect (control != nullptr);
            if (control != nullptr)
            {
                expectEquals (groupPath (*control).joinIntoString ("/"), juce::String ("Timing/Global"));
                expectEquals (valueOf (processor, global::freezeSustain), 0.0f);
                auto* button = dynamic_cast<juce::Button*> (control);
                expect (button != nullptr);
                if (button != nullptr)
                {
                    button->setToggleState (true, juce::sendNotificationSync);
                    expectEquals (valueOf (processor, global::freezeSustain), 1.0f);
                    button->setToggleState (false, juce::sendNotificationSync);
                }
            }
            for (auto* c : tabOrder (ed))
                if (groupPath (*c).contains ("Global"))
                    expect (c->getParentComponent()->getLocalBounds().contains (c->getBounds()), titleOf (*c) + " is clipped");
        }

        beginTest ("An open editor follows host state replacement, including restores from another thread");
        {
            AstralayProcessor savedProcessor, restoredProcessor;
            savedProcessor.setSelectedTap (5);
            savedProcessor.getState().state.setProperty ("presetName", "Restored session", nullptr);
            auto* feedback = savedProcessor.getState().getParameter (tapId (5, tap::feedback));
            feedback->setValueNotifyingHost (feedback->convertTo0to1 (76.0f));

            juce::MemoryBlock saved;
            savedProcessor.getStateInformation (saved);
            std::unique_ptr<juce::AudioProcessorEditor> openEditor (restoredProcessor.createEditor());
            openEditor->addToDesktop (juce::ComponentPeer::windowIsTemporary);

            const auto dispatch = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); };
            const auto checkRestored = [&]
            {
                expect (findByTitle (*openEditor, "Preset: Restored session") != nullptr, "Preset name stayed stale");
                expectEquals (restoredProcessor.getSelectedTap(), 5);
                expect (findByTitle (*openEditor, "Tap 6 Enabled") != nullptr, "Tap controls stayed bound to the old tap");
                auto* slider = dynamic_cast<astralay::ui::ParameterSlider*> (findByTitle (*openEditor, "Tap 6 Feedback"));
                expect (slider != nullptr);
                if (slider != nullptr)
                {
                    expect (slider->getParameter() == restoredProcessor.getState().getParameter (tapId (5, tap::feedback)));
                    expectWithinAbsoluteError (slider->getValue(), 76.0, 0.01);
                    expectEquals (groupPath (*slider).joinIntoString ("/"), juce::String ("Tap 6"));
                    slider->setValue (31.0, juce::sendNotificationSync);
                    expectWithinAbsoluteError (valueOf (restoredProcessor, tapId (5, tap::feedback)), 31.0f, 0.01f);
                    expectWithinAbsoluteError (valueOf (restoredProcessor, tapId (0, tap::feedback)), 40.0f, 0.01f);
                }
            };

            restoredProcessor.setStateInformation (saved.getData(), (int) saved.getSize());
            dispatch();
            checkRestored();

            // Change the visible tap, then restore from a non-message host thread.
            openEditor->keyPressed (juce::KeyPress ('1'));
            std::thread restore ([&] { restoredProcessor.setStateInformation (saved.getData(), (int) saved.getSize()); });
            restore.join();
            dispatch();
            checkRestored();

            // More than one replacement before the message loop runs must display the newest state.
            savedProcessor.setSelectedTap (9);
            savedProcessor.getState().state.setProperty ("presetName", "Latest session", nullptr);
            juce::MemoryBlock latest;
            savedProcessor.getStateInformation (latest);
            restoredProcessor.setStateInformation (saved.getData(), (int) saved.getSize());
            restoredProcessor.setStateInformation (latest.getData(), (int) latest.getSize());
            dispatch();
            expect (findByTitle (*openEditor, "Preset: Latest session") != nullptr);
            expect (findByTitle (*openEditor, "Tap 10 Feedback") != nullptr);
            expectEquals (restoredProcessor.getSelectedTap(), 9);

            // A new preset name still refreshes when its selected tap is unchanged.
            savedProcessor.getState().state.setProperty ("presetName", "Same tap session", nullptr);
            savedProcessor.getStateInformation (latest);
            restoredProcessor.setStateInformation (latest.getData(), (int) latest.getSize());
            dispatch();
            expect (findByTitle (*openEditor, "Preset: Same tap session") != nullptr);
            expect (findByTitle (*openEditor, "Tap 10 Feedback") != nullptr);

            // Closing the editor before a queued refresh is delivered must be safe.
            restoredProcessor.setStateInformation (saved.getData(), (int) saved.getSize());
            openEditor.reset();
            dispatch();
        }

        beginTest ("Every control has a help tag");
        {
            for (auto* c : tabOrder (ed))
                if (auto* handler = c->getAccessibilityHandler())
                    expect (handler->getHelp().isNotEmpty(), "No help for " + titleOf (*c));
        }

        beginTest ("Help tags don't mention context menus");
        {
            for (auto* c : tabOrder (ed))
                if (auto* handler = c->getAccessibilityHandler())
                    expect (! handler->getHelp().containsIgnoreCase ("menu"), "Help mentions a menu: " + titleOf (*c));
        }

        beginTest ("Selecting a tap renames the group and rebinds its controls");
        {
            auto* selector = dynamic_cast<juce::ComboBox*> (findByTitle (ed, "Selected tap"));
            expect (selector != nullptr);

            if (selector != nullptr)
            {
                selector->setSelectedId (3, juce::sendNotificationSync);

                expect (findByTitle (ed, "Tap 3 Feedback") != nullptr);
                expect (findByTitle (ed, "Tap 1 Feedback") == nullptr);
                expectEquals (processor.getSelectedTap(), 2);

                if (auto* stutter = findByTitle (ed, "Tap 3 Stutter Probability"))
                    expectEquals (groupPath (*stutter).joinIntoString ("/"), juce::String ("Stutter/Tap 3"));

                // Tap 3 is off by default; tap 1 is on.
                expectEquals (selector->getItemText (0), juce::String ("Tap 1, on"));
                expectEquals (selector->getItemText (2), juce::String ("Tap 3, off"));
                expectEquals (selector->getText(), juce::String ("Tap 3, off"));

                // Turning the selected tap on and off updates both its item and the displayed text.
                auto* enabled = processor.getState().getParameter (tapId (2, tap::enabled));
                enabled->setValueNotifyingHost (1.0f);
                expectEquals (selector->getItemText (2), juce::String ("Tap 3, on"));
                expectEquals (selector->getText(), juce::String ("Tap 3, on"));

                enabled->setValueNotifyingHost (0.0f);
                expectEquals (selector->getItemText (2), juce::String ("Tap 3, off"));
                expectEquals (selector->getText(), juce::String ("Tap 3, off"));

                selector->setSelectedId (1, juce::sendNotificationSync);
            }
        }

        beginTest ("Host sync switches time controls to note values");
        {
            auto* sync = processor.getState().getParameter (global::sync);
            sync->setValueNotifyingHost (1.0f);

            expect (findByTitle (ed, "Tap 1 Synced Time") != nullptr);
            expect (findByTitle (ed, "Synced Buffer Size") != nullptr);
            expect (findByTitle (ed, "Tap 1 Stutter Minimum Synced Slice") != nullptr);

            sync->setValueNotifyingHost (0.0f);
            expect (findByTitle (ed, "Tap 1 Time") != nullptr);
        }

        beginTest ("Slider keys follow the spec");
        {
            auto* time = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Time"));
            expect (time != nullptr);

            if (time != nullptr)
            {
                const auto id = tapId (0, tap::time);

                time->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                expectWithinAbsoluteError (valueOf (processor, id), 510.0f, 0.01f);

                time->keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
                expectWithinAbsoluteError (valueOf (processor, id), 511.0f, 0.01f);

                time->keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::commandModifier, 0));
                expectWithinAbsoluteError (valueOf (processor, id), 500.0f, 0.01f);

                time->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
                expectWithinAbsoluteError (valueOf (processor, id), 5000.0f, 0.01f);

                time->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
                expectWithinAbsoluteError (valueOf (processor, id), 1.0f, 0.01f);

                time->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
                expectWithinAbsoluteError (valueOf (processor, id), 500.0f, 0.01f);

                // Backspace belongs to the editor, which turns the tap on or off with it.
                expect (! time->keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey)));
            }
        }

        beginTest ("Tap keys switch taps and turn them on and off");
        {
            const auto shift = juce::ModifierKeys::shiftModifier;

            expect (ed.keyPressed (juce::KeyPress ('5')));
            expectEquals (processor.getSelectedTap(), 4);
            expect (findByTitle (ed, "Tap 5 Feedback") != nullptr);

            ed.keyPressed (juce::KeyPress ('0'));
            expectEquals (processor.getSelectedTap(), 9);

            ed.keyPressed (juce::KeyPress ('1', shift, '!'));
            expectEquals (processor.getSelectedTap(), 10);

            ed.keyPressed (juce::KeyPress ('6', shift, '^'));
            expectEquals (processor.getSelectedTap(), 15);

            // Shift+7 and up name no tap.
            expect (! ed.keyPressed (juce::KeyPress ('7', shift, '&')));
            expectEquals (processor.getSelectedTap(), 15);

            // Minus and equals wrap in both directions.
            ed.keyPressed (juce::KeyPress ('='));
            expectEquals (processor.getSelectedTap(), 0);

            ed.keyPressed (juce::KeyPress ('-'));
            expectEquals (processor.getSelectedTap(), 15);

            // Backspace toggles the selected tap as an undoable edit.
            const auto id = tapId (15, tap::enabled);
            expect (valueOf (processor, id) < 0.5f);

            ed.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
            expect (valueOf (processor, id) >= 0.5f);

            processor.getHistory().undo();
            expect (valueOf (processor, id) < 0.5f);

            ed.keyPressed (juce::KeyPress ('1'));
            expectEquals (processor.getSelectedTap(), 0);
        }

        beginTest ("Freeze and host sync have shortcuts");
        {
            const auto command = juce::ModifierKeys::commandModifier;

           #if JUCE_MAC
            const auto freezeModifier = juce::ModifierKeys::commandModifier;
           #else
            const auto freezeModifier = juce::ModifierKeys::altModifier;
           #endif

            ed.keyPressed (juce::KeyPress ('f', freezeModifier, 0));
            expect (valueOf (processor, global::freeze) >= 0.5f);

            ed.keyPressed (juce::KeyPress ('f', freezeModifier, 0));
            expect (valueOf (processor, global::freeze) < 0.5f);

            ed.keyPressed (juce::KeyPress ('y', command, 0));
            expect (valueOf (processor, global::sync) >= 0.5f);

            ed.keyPressed (juce::KeyPress ('y', command, 0));
            expect (valueOf (processor, global::sync) < 0.5f);
        }

        beginTest ("The arming shortcut takes a number for the macro");
        {
           #if JUCE_MAC
            const juce::KeyPress armKey ('m', juce::ModifierKeys::commandModifier, 0);
           #else
            const juce::KeyPress armKey ('m', juce::ModifierKeys::altModifier, 0);
           #endif

            const auto armed = [&ed] (int number) { return findByTitle (ed, "Disarm Macro " + juce::String (number)) != nullptr; };
            const auto tapBefore = processor.getSelectedTap();

            expect (ed.keyPressed (armKey));
            expect (ed.keyPressed (juce::KeyPress ('3')));
            expect (armed (3));

            // Arming another disarms the first, and a held shortcut's repeats aren't the next key.
            ed.keyPressed (armKey);
            ed.keyPressed (armKey);
            ed.keyPressed (juce::KeyPress (juce::KeyPress::numberPad5));
            expect (armed (5) && ! armed (3));

            // The same number disarms it.
            ed.keyPressed (armKey);
            ed.keyPressed (juce::KeyPress ('5'));
            expect (! armed (5));

            // 0 disarms whichever is armed, and does nothing when none is.
            ed.keyPressed (armKey);
            ed.keyPressed (juce::KeyPress ('8'));
            expect (armed (8));

            ed.keyPressed (armKey);
            ed.keyPressed (juce::KeyPress ('0'));
            expect (! armed (8));

            ed.keyPressed (armKey);
            expect (ed.keyPressed (juce::KeyPress ('0')));

            // Any other key cancels and is used up, and the key after it means what it usually does.
            ed.keyPressed (armKey);
            expect (ed.keyPressed (juce::KeyPress ('9')));
            ed.keyPressed (armKey);
            expect (ed.keyPressed (juce::KeyPress (juce::KeyPress::tabKey)));

            for (int m = 1; m <= numMacros; ++m)
                expect (! armed (m));

            expectEquals (processor.getSelectedTap(), tapBefore);

            ed.keyPressed (juce::KeyPress ('2'));
            expectEquals (processor.getSelectedTap(), 1);
            expect (! armed (2));

            ed.keyPressed (juce::KeyPress ('1'));
        }

        beginTest ("A key layer takes one key and then closes");
        {
            astralay::ui::Announcer layerAnnouncer (ed);
            astralay::ui::KeyLayer layer (layerAnnouncer);
            const juce::KeyPress opener ('k', juce::ModifierKeys::altModifier, 0);
            juce::Array<int> taken;

            const auto open = [&]
            {
                layer.open (opener, "Which?", [&taken] (const juce::KeyPress& key)
                {
                    taken.add (key.getKeyCode());
                    return key.getKeyCode() == 'a';
                });
            };

            expect (! layer.isOpen());
            expect (! layer.handleKey (juce::KeyPress ('a')));

            open();
            expect (layer.isOpen());
            expect (layer.handleKey (opener));
            expect (layer.isOpen());

            expect (layer.handleKey (juce::KeyPress ('a')));
            expect (! layer.isOpen());

            // A key that isn't the layer's is used up all the same.
            open();
            expect (layer.handleKey (juce::KeyPress ('b')));
            expect (! layer.isOpen());
            expect (taken == juce::Array<int> { 'a', 'b' });

            open();
            layer.close();
            expect (! layer.handleKey (juce::KeyPress ('a')));

            using astralay::ui::digitForKey;
            expectEquals (digitForKey (juce::KeyPress ('0')), 0);
            expectEquals (digitForKey (juce::KeyPress ('9')), 9);
            expectEquals (digitForKey (juce::KeyPress (juce::KeyPress::numberPad7)), 7);
            expectEquals (digitForKey (juce::KeyPress ('4', juce::ModifierKeys::shiftModifier, '$')), -1);
            expectEquals (digitForKey (juce::KeyPress ('a')), -1);
        }

        beginTest ("Output gain has a context menu, offered to screen readers as an action");
        {
            const auto showMenu = juce::AccessibilityActionType::showMenu;
            auto* gain = findByTitle (ed, "Output Gain");
            auto* mix = findByTitle (ed, "Mix");
            expect (gain != nullptr && mix != nullptr);

            if (gain != nullptr && mix != nullptr)
            {
                auto* gainMenu = dynamic_cast<astralay::ui::ContextMenuTarget*> (gain);
                auto* mixMenu = dynamic_cast<astralay::ui::ContextMenuTarget*> (mix);

                expect (gainMenu != nullptr && gainMenu->hasContextMenu());
                expect (mixMenu != nullptr && mixMenu->hasContextMenu());

                expect (gain->getAccessibilityHandler()->getActions().contains (showMenu));
                expect (mix->getAccessibilityHandler()->getActions().contains (showMenu));

                // What the right bracket key opens.
                expect (astralay::ui::findContextMenu (gain) == gainMenu);
                expect (astralay::ui::findContextMenu (mix) == mixMenu);
                expect (astralay::ui::findContextMenu (nullptr) == nullptr);
            }

            // Pitch probability's menu chooses the pitch mode, a parameter of the tap.
            auto* pitch = findByTitle (ed, "Tap 1 Pitch Probability");
            auto* pitchMenu = dynamic_cast<astralay::ui::ContextMenuTarget*> (pitch);
            expect (pitchMenu != nullptr && pitchMenu->hasContextMenu());

            if (pitch != nullptr)
                expect (pitch->getAccessibilityHandler()->getActions().contains (showMenu));

            auto* mode = processor.getState().getParameter (tapId (0, tap::pitchMode));
            expect (mode != nullptr);

            if (mode != nullptr)
            {
                expectEquals (mode->getName (100), juce::String ("Tap 1 Pitch Mode"));
                expectEquals (mode->getCurrentValueAsText(), juce::String ("Sweep"));
                expectEquals (mode->getAllValueStrings().joinIntoString (","), juce::String ("Sweep,Varispeed"));
            }

            // The output clip is saved with the session, but a preset leaves it alone.
            expect (processor.getOutputClip() == OutputClip::plus18);
            processor.setOutputClip (OutputClip::zero);

            juce::MemoryBlock saved;
            processor.getStateInformation (saved);

            AstralayProcessor restored;
            restored.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (restored.getOutputClip() == OutputClip::zero);

            restored.loadFactoryPreset (0);
            expect (restored.getOutputClip() == OutputClip::zero);

            processor.setOutputClip (OutputClip::plus18);
        }

        beginTest ("The performance area selects taps, moves their times and freezes");
        {
            auto* pad = findByTitle (ed, "Performance area");
            expect (pad != nullptr);

            if (pad != nullptr)
            {
                const auto shift = juce::ModifierKeys::shiftModifier;
                const auto backspace = juce::KeyPress::backspaceKey;
                const auto release = [pad] { pad->keyStateChanged (false); };

                expectEquals ((int) processor.getPerformanceSelection(), 0xffff);

                pad->keyPressed (juce::KeyPress ('3'));
                expectEquals ((int) processor.getPerformanceSelection(), 0xfffb);

                pad->keyPressed (juce::KeyPress ('3'));
                pad->keyPressed (juce::KeyPress ('6', shift, '^'));
                expectEquals ((int) processor.getPerformanceSelection(), 0x7fff);

                // Backspace: all, or none once all are selected. Shift+Backspace: even, then odd.
                pad->keyPressed (juce::KeyPress (backspace));
                expectEquals ((int) processor.getPerformanceSelection(), 0xffff);

                pad->keyPressed (juce::KeyPress (backspace));
                expectEquals ((int) processor.getPerformanceSelection(), 0);

                pad->keyPressed (juce::KeyPress (backspace, shift, 0));
                expectEquals ((int) processor.getPerformanceSelection(), 0xaaaa);

                pad->keyPressed (juce::KeyPress (backspace, shift, 0));
                expectEquals ((int) processor.getPerformanceSelection(), 0x5555);

                pad->keyPressed (juce::KeyPress (backspace));
                expectEquals ((int) processor.getPerformanceSelection(), 0xffff);

                // The selected tap is untouched by all of that.
                expectEquals (processor.getSelectedTap(), 0);

                // Arrows scale the taps that are selected and on: tap 1 here, and tap 2 once it is on.
                const auto time1 = tapId (0, tap::time), time2 = tapId (1, tap::time), time3 = tapId (2, tap::time);
                auto* enabled2 = processor.getState().getParameter (tapId (1, tap::enabled));
                auto* time2Parameter = processor.getState().getParameter (time2);

                enabled2->setValueNotifyingHost (1.0f);
                time2Parameter->setValueNotifyingHost (time2Parameter->convertTo0to1 (250.0f));
                processor.getHistory().clear();

                pad->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                release();
                expectWithinAbsoluteError (valueOf (processor, time1), 550.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, time2), 275.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, time3), 500.0f, 0.01f);

                // One undo step for all the taps, and one for a held key's repeats.
                processor.getHistory().undo();
                expectWithinAbsoluteError (valueOf (processor, time1), 500.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, time2), 250.0f, 0.01f);
                expect (! processor.getHistory().canUndo());

                pad->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
                pad->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
                pad->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
                release();
                expectWithinAbsoluteError (valueOf (processor, time1), 500.0f / 1.331f, 0.01f);

                processor.getHistory().undo();
                expectWithinAbsoluteError (valueOf (processor, time1), 500.0f, 0.01f);
                expect (! processor.getHistory().canUndo());

                // A tap at its limit stops the whole selection.
                time2Parameter->setValueNotifyingHost (1.0f);
                pad->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                release();
                expectWithinAbsoluteError (valueOf (processor, time1), 500.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, time2), 5000.0f, 0.01f);

                // Unselected, it no longer holds the others back.
                pad->keyPressed (juce::KeyPress ('2'));
                pad->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                release();
                expectWithinAbsoluteError (valueOf (processor, time1), 550.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, time2), 5000.0f, 0.01f);

                // With host sync on, the arrows step through note values.
                auto* sync = processor.getState().getParameter (global::sync);
                const auto synced1 = tapId (0, tap::timeSync);
                const auto before = valueOf (processor, synced1);

                sync->setValueNotifyingHost (1.0f);
                pad->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                release();
                expectWithinAbsoluteError (valueOf (processor, synced1), before + 1.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, time1), 550.0f, 0.01f);
                sync->setValueNotifyingHost (0.0f);

                pad->keyPressed (juce::KeyPress ('2'));
                enabled2->setValueNotifyingHost (0.0f);
                processor.getHistory().clear();

                // Left and right move the smear size; with Shift, the smear amount.
                const auto left = juce::KeyPress::leftKey, right = juce::KeyPress::rightKey;

                pad->keyPressed (juce::KeyPress (right));
                pad->keyPressed (juce::KeyPress (right));
                release();
                expectWithinAbsoluteError (valueOf (processor, global::smearSize), 220.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, global::smearAmount), 10.0f, 0.01f);

                pad->keyPressed (juce::KeyPress (left, shift, 0));
                release();
                expectWithinAbsoluteError (valueOf (processor, global::smearAmount), 5.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, global::smearSize), 220.0f, 0.01f);

                // They stop at the limits, where a press adds no undo step.
                pad->keyPressed (juce::KeyPress (left, shift, 0));
                release();
                pad->keyPressed (juce::KeyPress (left, shift, 0));
                release();
                expectWithinAbsoluteError (valueOf (processor, global::smearAmount), 0.0f, 0.01f);

                // A held key's repeats are one undo step.
                processor.getHistory().undo();
                processor.getHistory().undo();
                expectWithinAbsoluteError (valueOf (processor, global::smearAmount), 10.0f, 0.01f);
                expectWithinAbsoluteError (valueOf (processor, global::smearSize), 220.0f, 0.01f);

                processor.getHistory().undo();
                expectWithinAbsoluteError (valueOf (processor, global::smearSize), 200.0f, 0.01f);
                expect (! processor.getHistory().canUndo());

                // F freezes while held, without an undo step; from a latched freeze it ends off.
                pad->keyPressed (juce::KeyPress ('f'));
                expect (valueOf (processor, global::freeze) >= 0.5f);
                release();
                expect (valueOf (processor, global::freeze) < 0.5f);
                expect (! processor.getHistory().canUndo());

                pad->keyPressed (juce::KeyPress ('f', shift, 'F'));
                release();
                expect (valueOf (processor, global::freeze) >= 0.5f);

                pad->keyPressed (juce::KeyPress ('f'));
                expect (valueOf (processor, global::freeze) >= 0.5f);
                release();
                expect (valueOf (processor, global::freeze) < 0.5f);

                // The selection outlives the editor window.
                pad->keyPressed (juce::KeyPress ('4'));
                expectEquals ((int) processor.getPerformanceSelection(), 0xfff7);

                {
                    std::unique_ptr<juce::AudioProcessorEditor> second (processor.createEditor());
                    second->addToDesktop (juce::ComponentPeer::windowIsTemporary);

                    auto* secondPad = findByTitle (*second, "Performance area");
                    expect (secondPad != nullptr);

                    if (secondPad != nullptr)
                    {
                        secondPad->keyPressed (juce::KeyPress ('4'));
                        expectEquals ((int) processor.getPerformanceSelection(), 0xffff);
                    }
                }

                processor.setPerformanceSelection (AstralayProcessor::allTapsSelected);
            }
        }

        beginTest ("A tap or one of its settings can be copied and pasted");
        {
            auto& state = processor.getState();
            const auto set = [&state] (const juce::String& id, float plainValue)
            {
                auto* p = state.getParameter (id);
                p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
            };

            expect (! processor.pasteTapSettings (1, {}), "Nothing copied yet");

            set (tapId (0, tap::feedback), 70.0f);
            set (tapId (0, tap::pan), -30.0f);
            set (tapId (0, tap::pitchMax), 7.0f);
            processor.getHistory().clear();

            // A whole tap: everything but whether it is on.
            processor.copyTapSettings (0, {});
            expect (! processor.pasteTapSettings (2, tap::feedback), "A tap doesn't paste onto one setting");
            expect (processor.pasteTapSettings (2, {}));

            expectWithinAbsoluteError (valueOf (processor, tapId (2, tap::feedback)), 70.0f, 0.01f);
            expectWithinAbsoluteError (valueOf (processor, tapId (2, tap::pan)), -30.0f, 0.01f);
            expectWithinAbsoluteError (valueOf (processor, tapId (2, tap::pitchMax)), 7.0f, 0.01f);
            expect (valueOf (processor, tapId (2, tap::enabled)) < 0.5f);
            expect (processor.isPresetModified());

            expectEquals (processor.getHistory().undo(), juce::String ("Undo paste tap"));
            expectWithinAbsoluteError (valueOf (processor, tapId (2, tap::feedback)), 40.0f, 0.01f);
            expectWithinAbsoluteError (valueOf (processor, tapId (2, tap::pan)), 0.0f, 0.01f);

            // One setting: pastes only onto the same setting, on one tap or all of them.
            processor.copyTapSettings (0, tap::feedback);
            expect (! processor.pasteTapSettings (2, tap::pan));
            expect (! processor.pasteTapSettings (2, {}));
            expect (processor.pasteTapSettings (2, tap::feedback));
            expectWithinAbsoluteError (valueOf (processor, tapId (2, tap::feedback)), 70.0f, 0.01f);
            expectWithinAbsoluteError (valueOf (processor, tapId (2, tap::pan)), 0.0f, 0.01f);

            expect (processor.pasteTapSettings (AstralayProcessor::allTaps, tap::feedback));

            for (int t = 0; t < numTaps; ++t)
                expectWithinAbsoluteError (valueOf (processor, tapId (t, tap::feedback)), 70.0f, 0.01f);

            expectEquals (processor.getHistory().undo(), juce::String ("Undo paste Feedback to all"));
            expectWithinAbsoluteError (valueOf (processor, tapId (5, tap::feedback)), 40.0f, 0.01f);
            expectWithinAbsoluteError (valueOf (processor, tapId (2, tap::feedback)), 70.0f, 0.01f);

            // Pasting a whole tap onto all of them leaves each tap on or off as it was.
            processor.copyTapSettings (0, {});
            expect (processor.pasteTapSettings (AstralayProcessor::allTaps, {}));
            expectWithinAbsoluteError (valueOf (processor, tapId (15, tap::pan)), -30.0f, 0.01f);
            expect (valueOf (processor, tapId (0, tap::enabled)) >= 0.5f);
            expect (valueOf (processor, tapId (15, tap::enabled)) < 0.5f);

            processor.getHistory().undo();
            set (tapId (0, tap::feedback), 40.0f);
            set (tapId (2, tap::feedback), 40.0f);
            set (tapId (0, tap::pan), 0.0f);
        }

        beginTest ("Typed values are accepted in range and rejected out of range");
        {
            auto* time = findByTitle (ed, "Tap 1 Time");
            auto* value = time != nullptr ? time->getAccessibilityHandler()->getValueInterface() : nullptr;
            expect (value != nullptr);

            if (value != nullptr)
            {
                const auto id = tapId (0, tap::time);

                value->setValueAsString ("1.5 s");
                expectWithinAbsoluteError (valueOf (processor, id), 1500.0f, 0.01f);

                value->setValueAsString ("9 s");
                expectWithinAbsoluteError (valueOf (processor, id), 1500.0f, 0.01f);

                value->setValueAsString ("nonsense");
                expectWithinAbsoluteError (valueOf (processor, id), 1500.0f, 0.01f);

                expectEquals (value->getCurrentValueAsString(), juce::String ("1.5 s"));
            }

            if (auto* volume = findByTitle (ed, "Tap 1 Volume"))
            {
                volume->getAccessibilityHandler()->getValueInterface()->setValueAsString ("-inf");
                expectWithinAbsoluteError (valueOf (processor, tapId (0, tap::volume)), volumeFloorDb, 0.01f);
            }
        }
    }
};

static EditorTests editorTests;
