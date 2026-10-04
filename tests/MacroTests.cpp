#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/NoteValues.h"
#include "params/Parameters.h"

namespace
{
    using namespace astralay;

    juce::RangedAudioParameter& parameter (AstralayProcessor& processor, const juce::String& id)
    {
        auto* p = processor.getState().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    float plain (juce::RangedAudioParameter& p)
    {
        return p.convertFrom0to1 (p.getValue());
    }

    void edit (juce::RangedAudioParameter& p, float plainValue)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
        p.endChangeGesture();
    }

    juce::String titleOf (juce::Component& c)
    {
        if (auto* handler = c.getAccessibilityHandler())
            return handler->getTitle();

        return {};
    }

    juce::Component* findByTitle (juce::Component& editor, const juce::String& title)
    {
        for (auto* c : juce::KeyboardFocusTraverser().getAllComponents (&editor))
            if (titleOf (*c) == title)
                return c;

        return nullptr;
    }

    /** The level a processor settles at when fed a constant 0.5. */
    float settledOutput (AstralayProcessor& processor)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int block = 0; block < 40; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), 0.5f, buffer.getNumSamples());

            processor.processBlock (buffer, midi);
        }

        return buffer.getSample (0, buffer.getNumSamples() - 1);
    }
}

class MacroTests final : public juce::UnitTest
{
public:
    MacroTests() : juce::UnitTest ("Macros", "Astralay") {}

    void runTest() override
    {
        using namespace params;

        const auto feedbackId = tapId (0, tap::feedback);

        beginTest ("Macros can move the sliders, but not the glitch engine or the output gain");
        {
            expect (canModulate (feedbackId));
            expect (canModulate (tapId (15, tap::time)));
            expect (canModulate (tapId (3, tap::crushBitsMin)));
            expect (canModulate (global::mix));
            expect (canModulate (global::glide));
            expect (canModulate (global::smearSize));

            expect (! canModulate (global::outputGain));
            expect (! canModulate (global::threshold));
            expect (! canModulate (global::bufferSize));
            expect (! canModulate (global::seed));
            expect (! canModulate (global::sync));
            expect (! canModulate (global::freeze));
            expect (! canModulate (tapId (0, tap::enabled)));
            expect (! canModulate (tapId (0, tap::pitchMode)));
            expect (! canModulate (macroId (0)));

            // A synced note value follows the modulation of the time it stands in for.
            expect (! canModulate (tapId (15, tap::timeSync)));
            expectEquals (modulationTarget (tapId (15, tap::timeSync)), tapId (15, tap::time));
            expectEquals (modulationTarget (tapId (2, tap::stutterSyncMax)), tapId (2, tap::stutterMax));
            expectEquals (modulationTarget (feedbackId), feedbackId);
            expect (modulationTarget (global::outputGain).isEmpty());
            expectEquals (syncedCounterpart (tapId (15, tap::time)), tapId (15, tap::timeSync));
            expect (syncedCounterpart (feedbackId).isEmpty());
        }

        beginTest ("Modulations are added, changed and removed as undoable steps");
        {
            AstralayProcessor processor;

            processor.setModulation (0, feedbackId, 25.0f);
            processor.setModulation (0, global::mix, -50.0f);
            processor.setModulation (0, global::outputGain, 6.0f);

            auto macros = processor.getMacros();
            expectEquals ((int) macros[0].modulations.size(), 2);
            expectEquals (macros[0].modulations[0].parameterId, feedbackId);
            expectWithinAbsoluteError (macros[0].modulations[0].amount, 25.0f, 1.0e-4f);
            expect (processor.isPresetModified());

            // The parameter itself is left alone.
            expectWithinAbsoluteError (plain (parameter (processor, feedbackId)), 40.0f, 0.01f);

            processor.setModulation (0, feedbackId, 0.0f);
            macros = processor.getMacros();
            expectEquals ((int) macros[0].modulations.size(), 1);
            expectEquals (macros[0].modulations[0].parameterId, juce::String (global::mix));

            expectEquals (processor.getHistory().undo(), juce::String ("Undo modulation of Tap 1 Feedback by Macro 1"));
            expectEquals ((int) processor.getMacros()[0].modulations.size(), 2);

            processor.getHistory().redo();
            expectEquals ((int) processor.getMacros()[0].modulations.size(), 1);
        }

        beginTest ("A quick run of changes to one modulation is one undo step");
        {
            AstralayProcessor processor;

            processor.setModulation (1, feedbackId, 1.0f);
            processor.setModulation (1, feedbackId, 2.0f);
            processor.setModulation (1, feedbackId, 3.0f);
            processor.setModulation (1, global::mix, 50.0f);

            processor.getHistory().undo();
            expectEquals ((int) processor.getMacros()[1].modulations.size(), 1);

            processor.getHistory().undo();
            expect (processor.getMacros()[1].modulations.empty());
            expect (! processor.getHistory().canUndo());
        }

        beginTest ("A bipolar macro runs from -1 to 1 and keeps its value when switched");
        {
            AstralayProcessor processor;
            auto& macro = parameter (processor, macroId (2));

            expectEquals (macro.getName (100), juce::String ("Macro 3"));
            expectWithinAbsoluteError (plain (macro), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (macro.getNormalisableRange().start, 0.0f, 1.0e-6f);

            edit (macro, 0.5f);
            processor.setMacroBipolar (2, true);

            expect (processor.getMacros()[2].bipolar);
            expectWithinAbsoluteError (macro.getNormalisableRange().start, -1.0f, 1.0e-6f);
            expectWithinAbsoluteError (plain (macro), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (macro.getValue(), 0.75f, 1.0e-6f);
            expectWithinAbsoluteError (macro.convertFrom0to1 (macro.getDefaultValue()), 0.0f, 1.0e-6f);
            expectEquals (macro.getCurrentValueAsText(), juce::String ("0.5"));

            // A negative value comes up to 0 when the macro goes back to unipolar.
            edit (macro, -0.4f);
            expectEquals (macro.getCurrentValueAsText(), juce::String ("-0.4"));

            processor.setMacroBipolar (2, false);
            expectWithinAbsoluteError (plain (macro), 0.0f, 1.0e-6f);

            expectEquals (processor.getHistory().undo(), juce::String ("Undo Macro 3 unipolar"));
            expect (processor.getMacros()[2].bipolar);
            expectWithinAbsoluteError (plain (macro), -0.4f, 1.0e-6f);
        }

        beginTest ("Renaming a macro renames its parameter, and an empty name restores the default");
        {
            AstralayProcessor processor;
            auto& macro = parameter (processor, macroId (0));

            processor.renameMacro (0, "  Sweep ");
            expectEquals (state::macroName (processor.getMacros(), 0), juce::String ("Sweep"));
            expectEquals (macro.getName (100), juce::String ("Sweep"));

            processor.renameMacro (0, {});
            expectEquals (macro.getName (100), juce::String ("Macro 1"));
            expect (processor.getMacros()[0].name.isEmpty());

            processor.getHistory().undo();
            expectEquals (macro.getName (100), juce::String ("Sweep"));
        }

        beginTest ("A macro moves what it modulates in the audio");
        {
            AstralayProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            // The tap is off, so there is nothing wet: only the dry level changes with the mix.
            edit (parameter (processor, tapId (0, tap::enabled)), 0.0f);
            edit (parameter (processor, global::smearAmount), 0.0f);
            edit (parameter (processor, global::outputGain), 0.0f);

            const auto atHalf = settledOutput (processor);
            expectWithinAbsoluteError (atHalf, 0.5f * std::sqrt (0.5f), 0.01f);

            // 50% down from 50%, the mix is fully dry.
            processor.setModulation (0, global::mix, -50.0f);
            expectWithinAbsoluteError (settledOutput (processor), atHalf, 1.0e-4f);

            edit (parameter (processor, macroId (0)), 1.0f);
            expectWithinAbsoluteError (settledOutput (processor), 0.5f, 0.01f);

            edit (parameter (processor, macroId (0)), 0.5f);
            const auto partWay = settledOutput (processor);
            expect (partWay > atHalf + 0.01f && partWay < 0.49f);

            // Two macros on one parameter add up.
            processor.setModulation (1, global::mix, 50.0f);
            edit (parameter (processor, macroId (1)), 0.5f);
            expectWithinAbsoluteError (settledOutput (processor), atHalf, 1.0e-3f);

            // The parameter still reads 50% throughout.
            expectWithinAbsoluteError (plain (parameter (processor, global::mix)), 50.0f, 0.01f);
        }

        beginTest ("Macro settings are saved with the session");
        {
            AstralayProcessor processor;
            processor.renameMacro (1, "Wobble");
            processor.setMacroBipolar (1, true);
            processor.setModulation (1, feedbackId, -30.0f);
            edit (parameter (processor, macroId (1)), -0.5f);

            juce::MemoryBlock saved;
            processor.getStateInformation (saved);

            AstralayProcessor restored;
            restored.setStateInformation (saved.getData(), (int) saved.getSize());

            const auto macros = restored.getMacros();
            expectEquals (macros[1].name, juce::String ("Wobble"));
            expect (macros[1].bipolar);
            expectEquals ((int) macros[1].modulations.size(), 1);
            expectWithinAbsoluteError (macros[1].modulations[0].amount, -30.0f, 1.0e-4f);
            expectWithinAbsoluteError (plain (parameter (restored, macroId (1))), -0.5f, 1.0e-5f);
            expect (macros[0].modulations.empty() && ! macros[0].bipolar);

            // Saving again doesn't pile up copies of the settings.
            juce::MemoryBlock savedAgain;
            restored.getStateInformation (savedAgain);

            AstralayProcessor third;
            third.setStateInformation (savedAgain.getData(), (int) savedAgain.getSize());
            expectEquals ((int) third.getMacros()[1].modulations.size(), 1);
            expectWithinAbsoluteError (plain (parameter (third, macroId (1))), -0.5f, 1.0e-5f);

            // A session saved before there were macros restores the defaults.
            AstralayProcessor old;
            juce::MemoryBlock oldState;
            old.getStateInformation (oldState);
            restored.setStateInformation (oldState.getData(), (int) oldState.getSize());
            expect (restored.getMacros()[1].modulations.empty());
            expect (! restored.getMacros()[1].bipolar);
            expectWithinAbsoluteError (plain (parameter (restored, macroId (1))), 0.0f, 1.0e-5f);
        }

        beginTest ("Macro settings are part of a preset, and loading one is undoable");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("Astralay macro preset.astralay");

            AstralayProcessor processor;
            processor.renameMacro (3, "Depth");
            processor.setMacroBipolar (3, true);
            processor.setModulation (3, global::mix, 40.0f);
            edit (parameter (processor, macroId (3)), -0.25f);
            processor.savePresetFile (file);

            AstralayProcessor other;
            other.loadPresetFile (file);

            auto macros = other.getMacros();
            expectEquals (macros[3].name, juce::String ("Depth"));
            expect (macros[3].bipolar);
            expectEquals ((int) macros[3].modulations.size(), 1);
            expectWithinAbsoluteError (plain (parameter (other, macroId (3))), -0.25f, 1.0e-5f);

            // A factory preset has no macro settings; undoing the load brings them back.
            other.loadFactoryPreset (0);
            expect (other.getMacros()[3].modulations.empty());
            expect (! other.getMacros()[3].bipolar);
            expectWithinAbsoluteError (plain (parameter (other, macroId (3))), 0.0f, 1.0e-5f);

            other.getHistory().undo();
            macros = other.getMacros();
            expectEquals (macros[3].name, juce::String ("Depth"));
            expect (macros[3].bipolar);
            expectWithinAbsoluteError (plain (parameter (other, macroId (3))), -0.25f, 1.0e-5f);

            file.deleteFile();
        }

        beginTest ("The Macros group sits before Global, each macro in a group with a menu");
        {
            AstralayProcessor processor;
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto& ed = *editor;
            ed.addToDesktop (juce::ComponentPeer::windowIsTemporary);

            juce::StringArray titles;

            for (auto* c : juce::KeyboardFocusTraverser().getAllComponents (&ed))
                titles.add (titleOf (*c));

            const auto arm = titles.indexOf ("Arm Macro 1");
            expect (arm > titles.indexOf ("Tap 1 Bit Crusher Maximum Rate Reduction"));
            expectEquals (titles[arm + 1], juce::String ("Macro 1 value"));
            expectEquals (titles[arm + 2], juce::String ("Arm Macro 2"));
            expectEquals (titles.indexOf ("Host Sync"), arm + 2 * numMacros);

            auto* value = findByTitle (ed, "Macro 1 value");
            expect (value != nullptr);

            if (value != nullptr)
            {
                juce::StringArray groups;

                for (auto* handler = value->getAccessibilityHandler(); handler != nullptr; handler = handler->getParent())
                    if (handler->getRole() == juce::AccessibilityRole::group)
                        groups.add (handler->getTitle());

                expectEquals (groups.joinIntoString ("/"), juce::String ("Macro 1/Macros"));

                auto* group = value->getParentComponent();
                expect (dynamic_cast<ui::ContextMenuTarget*> (group) != nullptr);
                expect (group->getAccessibilityHandler()->getActions().contains (juce::AccessibilityActionType::showMenu));

                // The controls inside have no menu of their own to hint at, so they offer the macro's.
                expect (ui::findContextMenu (findByTitle (ed, "Arm Macro 1")) == dynamic_cast<ui::ContextMenuTarget*> (group));
            }

            // Renaming and making it bipolar show up on the controls.
            processor.renameMacro (0, "Sweep");
            expect (findByTitle (ed, "Arm Sweep") != nullptr);

            auto* slider = dynamic_cast<juce::Slider*> (findByTitle (ed, "Sweep value"));
            expect (slider != nullptr);

            if (slider != nullptr)
            {
                expectWithinAbsoluteError (slider->getMinimum(), 0.0, 1.0e-9);

                slider->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                expectWithinAbsoluteError (plain (parameter (processor, macroId (0))), 0.01f, 1.0e-5f);

                processor.setMacroBipolar (0, true);
                expectWithinAbsoluteError (slider->getMinimum(), -1.0, 1.0e-9);
                expectWithinAbsoluteError (slider->getValue(), 0.01, 1.0e-5);

                slider->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
                expectWithinAbsoluteError (plain (parameter (processor, macroId (0))), -1.0f, 1.0e-5f);

                slider->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
                expectWithinAbsoluteError (plain (parameter (processor, macroId (0))), 0.0f, 1.0e-5f);
            }
        }

        beginTest ("While a macro is armed, sliders set its amounts instead of their values");
        {
            AstralayProcessor processor;
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto& ed = *editor;
            ed.addToDesktop (juce::ComponentPeer::windowIsTemporary);

            auto* arm1 = dynamic_cast<juce::Button*> (findByTitle (ed, "Arm Macro 1"));
            auto* arm2 = dynamic_cast<juce::Button*> (findByTitle (ed, "Arm Macro 2"));
            expect (arm1 != nullptr && arm2 != nullptr);

            if (arm1 == nullptr || arm2 == nullptr)
                return;

            arm1->onClick();
            expectEquals (arm1->getButtonText(), juce::String ("Disarm"));
            expect (findByTitle (ed, "Disarm Macro 1") != nullptr);

            auto* feedback = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Feedback, Macro 1 amount"));
            expect (feedback != nullptr);
            expect (findByTitle (ed, "Tap 1 Feedback") == nullptr);

            // The output gain, the glitch engine and the macros' own values stay as they are.
            expect (findByTitle (ed, "Output Gain") != nullptr);
            expect (findByTitle (ed, "Glitch Threshold") != nullptr);
            expect (findByTitle (ed, "Macro 2 value") != nullptr);

            if (feedback != nullptr)
            {
                const auto amount = [&processor, &feedbackId] (int macro)
                {
                    const auto macros = processor.getMacros();

                    for (const auto& m : macros[(size_t) macro].modulations)
                        if (m.parameterId == feedbackId)
                            return m.amount;

                    return 0.0f;
                };

                expectWithinAbsoluteError (feedback->getValue(), 0.0, 1.0e-9);

                feedback->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                feedback->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                expectWithinAbsoluteError (amount (0), 2.0f, 1.0e-4f);
                expectWithinAbsoluteError (plain (parameter (processor, feedbackId)), 40.0f, 0.01f);
                expectEquals (feedback->getTextFromValue (feedback->getValue()), juce::String ("2%"));

                feedback->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
                expectWithinAbsoluteError (amount (0), 100.0f, 1.0e-4f);

                feedback->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
                expectWithinAbsoluteError (amount (0), -100.0f, 1.0e-4f);

                auto* typed = feedback->getAccessibilityHandler()->getValueInterface();
                typed->setValueAsString ("37.5%");
                expectWithinAbsoluteError (amount (0), 37.5f, 1.0e-4f);

                typed->setValueAsString ("150");
                expectWithinAbsoluteError (amount (0), 37.5f, 1.0e-4f);

                // Switching taps shows the other tap's amount, and coming back shows this one's.
                ed.keyPressed (juce::KeyPress ('2'));
                auto* other = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 2 Feedback, Macro 1 amount"));
                expect (other != nullptr);

                if (other != nullptr)
                    expectWithinAbsoluteError (other->getValue(), 0.0, 1.0e-9);

                ed.keyPressed (juce::KeyPress ('1'));
                expectWithinAbsoluteError (feedback->getValue(), 37.5, 1.0e-3);

                // Undo is followed by the slider.
                processor.getHistory().undo();
                expectWithinAbsoluteError (feedback->getValue(), (double) amount (0), 1.0e-3);

                // Arming another macro disarms the first.
                arm2->onClick();
                expectEquals (arm1->getButtonText(), juce::String ("Arm"));
                expectEquals (arm2->getButtonText(), juce::String ("Disarm"));
                expect (findByTitle (ed, "Tap 1 Feedback, Macro 2 amount") == feedback);
                expectWithinAbsoluteError (feedback->getValue(), 0.0, 1.0e-9);

                feedback->keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0));
                expectWithinAbsoluteError (amount (1), 10.0f, 1.0e-4f);

                // Delete goes to 0, which removes the modulation.
                feedback->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
                expect (processor.getMacros()[1].modulations.empty());

                // Disarmed, the slider is the parameter's again.
                arm2->onClick();
                expect (findByTitle (ed, "Tap 1 Feedback") == feedback);
                expectWithinAbsoluteError (feedback->getValue(), 40.0, 0.01);

                feedback->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                expectWithinAbsoluteError (plain (parameter (processor, feedbackId)), 41.0f, 0.01f);
            }
        }

        beginTest ("Amounts are in each control's own unit");
        {
            AstralayProcessor processor;
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto& ed = *editor;
            ed.addToDesktop (juce::ComponentPeer::windowIsTemporary);

            const auto amountFor = [&processor] (const juce::String& id)
            {
                const auto macros = processor.getMacros();

                for (const auto& m : macros[0].modulations)
                    if (m.parameterId == id)
                        return m.amount;

                return 0.0f;
            };

            const auto text = [] (juce::Slider* slider) { return slider->getTextFromValue (slider->getValue()); };
            const auto type = [] (juce::Slider* slider, const char* typed)
            {
                slider->getAccessibilityHandler()->getValueInterface()->setValueAsString (typed);
            };

            if (auto* arm = dynamic_cast<juce::Button*> (findByTitle (ed, "Arm Macro 1")))
                arm->onClick();

            auto* time = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Time, Macro 1 amount"));
            auto* pan = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Pan, Macro 1 amount"));
            auto* volume = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Volume, Macro 1 amount"));
            auto* lowCut = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Low Cut, Macro 1 amount"));
            auto* pitch = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Pitch Minimum, Macro 1 amount"));
            auto* bits = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Bit Crusher Minimum Bit Depth, Macro 1 amount"));
            expect (time != nullptr && pan != nullptr && volume != nullptr && lowCut != nullptr && pitch != nullptr && bits != nullptr);

            if (time == nullptr || pan == nullptr || volume == nullptr || lowCut == nullptr || pitch == nullptr || bits == nullptr)
                return;

            // Milliseconds: the usual 10 ms steps, either way from 0, up to the width of the range.
            expectEquals (text (time), juce::String ("0 ms"));
            time->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            time->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            expectWithinAbsoluteError (amountFor (tapId (0, tap::time)), -20.0f, 1.0e-3f);
            expectEquals (text (time), juce::String ("-20 ms"));

            type (time, "1.5 s");
            expectWithinAbsoluteError (amountFor (tapId (0, tap::time)), 1500.0f, 1.0e-3f);
            expectEquals (text (time), juce::String ("1.5 s"));

            time->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
            expectWithinAbsoluteError (amountFor (tapId (0, tap::time)), -4999.0f, 1.0e-2f);

            // Pan: which way the control moves, and up to the whole width.
            type (pan, "150 left");
            expectWithinAbsoluteError (amountFor (tapId (0, tap::pan)), -150.0f, 1.0e-3f);
            expectEquals (text (pan), juce::String ("150 left"));

            pan->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
            expectEquals (text (pan), juce::String ("0"));

            // Decibels have no floor as an amount.
            volume->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
            expectEquals (text (volume), juce::String ("-66.0 dB"));

            // Frequencies step by 10 Hz as an amount, rather than by a semitone.
            lowCut->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
            expectWithinAbsoluteError (amountFor (tapId (0, tap::lowCut)), 10.0f, 1.0e-3f);
            expectEquals (text (lowCut), juce::String ("10 Hz"));

            pitch->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            expectEquals (text (pitch), juce::String ("-1 semitones"));

            // Whole numbers for a control that steps in them.
            type (bits, "2.6");
            expectWithinAbsoluteError (amountFor (tapId (0, tap::crushBitsMin)), 3.0f, 1.0e-6f);
            expectEquals (text (bits), juce::String ("3 bits"));

            // With host sync on, the time's slider is a note value. It shows the same modulation,
            // as a percentage of the range, rather than one of its own.
            type (time, "2499.5 ms");
            processor.getState().getParameter (global::sync)->setValueNotifyingHost (1.0f);

            auto* synced = dynamic_cast<juce::Slider*> (findByTitle (ed, "Tap 1 Synced Time, Macro 1 amount"));
            expect (synced != nullptr);

            if (synced != nullptr)
            {
                expectEquals (text (synced), juce::String ("50%"));

                synced->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
                expectEquals (text (synced), juce::String ("51%"));
                expectWithinAbsoluteError (amountFor (tapId (0, tap::time)), 4999.0f * 0.51f, 0.01f);

                type (synced, "71.43%");
                expectEquals (text (synced), juce::String ("71.43%"));
                expectWithinAbsoluteError (amountFor (tapId (0, tap::time)), 4999.0f * 0.7143f, 0.01f);

                synced->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
                expectEquals (text (synced), juce::String ("-100%"));

                type (synced, "120");
                expectEquals (text (synced), juce::String ("-100%"));

                // One modulation, on the time; none on the note value.
                expectEquals ((int) processor.getMacros()[0].modulations.size(), 5);
                expectWithinAbsoluteError (amountFor (tapId (0, tap::timeSync)), 0.0f, 1.0e-6f);

                // Back in milliseconds once host sync is off.
                processor.getState().getParameter (global::sync)->setValueNotifyingHost (0.0f);
                expectEquals (text (time), juce::String ("-5 s"));
            }
        }

        beginTest ("A time's modulation moves its note value by the same share of the range");
        {
            AstralayProcessor processor;

            edit (parameter (processor, tapId (0, tap::feedback)), 0.0f);
            edit (parameter (processor, global::glide), 0.0f);
            edit (parameter (processor, global::smearAmount), 0.0f);
            edit (parameter (processor, global::mix), 100.0f);

            // Where an impulse comes back, in samples.
            const auto delay = [&processor]
            {
                constexpr int blockSize = 512, blocks = 200;
                processor.setRateAndBufferSizeDetails (48000.0, blockSize);
                processor.prepareToPlay (48000.0, blockSize);

                juce::AudioBuffer<float> buffer (2, blockSize);
                juce::MidiBuffer midi;
                auto loudest = 0.0f;
                auto position = -1;

                for (int block = 0; block < blocks; ++block)
                {
                    buffer.clear();

                    if (block == 0)
                        for (int channel = 0; channel < 2; ++channel)
                            buffer.setSample (channel, 0, 1.0f);

                    processor.processBlock (buffer, midi);

                    for (int i = 0; i < blockSize; ++i)
                    {
                        if (const auto level = std::abs (buffer.getSample (0, i)); level > loudest)
                        {
                            loudest = level;
                            position = block * blockSize + i;
                        }
                    }
                }

                return position;
            };

            // A synced note value can't be given a modulation of its own.
            processor.setModulation (0, tapId (0, tap::timeSync), 3.0f);
            expect (processor.getMacros()[0].modulations.empty());

            // Unsynced: 250 ms on top of 500 ms.
            processor.setModulation (0, tapId (0, tap::time), 250.0f);
            edit (parameter (processor, macroId (0)), 1.0f);
            expectWithinAbsoluteError (delay(), 36000, 2);

            // Synced: two steps' share of the time's range moves the note value two steps on
            // from 1/4, at the 120 bpm used when there is no host.
            const auto& notes = NoteValues::all();
            const auto quarter = NoteValues::indexOf ("1/4");
            const auto share = 2.0f / (float) (notes.size() - 1);

            edit (parameter (processor, global::sync), 1.0f);
            processor.setModulation (0, tapId (0, tap::time), 4999.0f * share);

            const auto expected = notes[quarter + 2].lengthInQuarters (4.0) * 24000.0;
            expectWithinAbsoluteError (delay(), juce::roundToInt (expected), 2);

            // Part of the way there lands on a note value too, never between two.
            edit (parameter (processor, macroId (0)), 0.3f);
            const auto partWay = delay();
            const auto onNote = [&] (int index) { return std::abs (partWay - juce::roundToInt (notes[index].lengthInQuarters (4.0) * 24000.0)) <= 2; };
            expect (onNote (quarter) || onNote (quarter + 1));

            // The parameters themselves haven't moved.
            expectWithinAbsoluteError (plain (parameter (processor, tapId (0, tap::time))), 500.0f, 0.01f);
            expectEquals (parameter (processor, tapId (0, tap::timeSync)).getCurrentValueAsText(), juce::String ("1/4"));
        }

        beginTest ("Amounts are stored in the control's unit and held to the width of its range");
        {
            AstralayProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            edit (parameter (processor, tapId (0, tap::enabled)), 0.0f);
            edit (parameter (processor, global::smearAmount), 0.0f);
            edit (parameter (processor, global::outputGain), 0.0f);
            edit (parameter (processor, global::mix), 0.0f);

            // 2000 Hz less takes the high cut from 20 kHz to 18 kHz, skewed slider or not.
            processor.setModulation (0, tapId (0, tap::highCut), -2000.0f);
            expectWithinAbsoluteError (processor.getMacros()[0].modulations[0].amount, -2000.0f, 1.0e-3f);

            // Held to the width of the range.
            processor.setModulation (0, tapId (0, tap::highCut), -50000.0f);
            expectWithinAbsoluteError (processor.getMacros()[0].modulations[0].amount, -19000.0f, 1.0e-2f);

            // The audio keeps running with it in place.
            edit (parameter (processor, macroId (0)), 1.0f);
            expectWithinAbsoluteError (settledOutput (processor), 0.5f, 0.01f);
        }
    }
};

static MacroTests macroTests;
