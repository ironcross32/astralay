#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/Parameters.h"

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

            // Main 5, tap 2 + 6 basics + 31 glitch controls (synced stutter slices share rows), global 15.
            expectEquals ((int) stops.size(), 59);

            if (stops.size() != 59)
                logMessage ("Tab order: " + titles.joinIntoString (" | "));

            const juce::StringArray expectedStart { "Undo", "Redo", "Save", "Load", "Preset: Init",
                                                    "Selected tap", "Tap 1 Enabled", "Tap 1 Time",
                                                    "Tap 1 Volume", "Tap 1 Pan", "Tap 1 Feedback",
                                                    "Tap 1 Low Cut", "Tap 1 High Cut",
                                                    "Tap 1 Reverse Probability", "Tap 1 Stutter Probability",
                                                    "Tap 1 Stutter Minimum Slice", "Tap 1 Stutter Maximum Slice" };

            for (int i = 0; i < expectedStart.size(); ++i)
                expectEquals (titles[i], expectedStart[i]);

            const juce::StringArray expectedEnd { "Host Sync", "Glide Time", "Freeze",
                                                  "Glitch Threshold", "Glitch Placement", "Buffer Size",
                                                  "Maximum Simultaneous Glitches", "Minimum Glitch Length",
                                                  "Maximum Glitch Length", "Reproducible Randomness", "Seed",
                                                  "Smear Amount", "Smear Size", "Mix", "Output Gain" };

            for (int i = 0; i < expectedEnd.size(); ++i)
                expectEquals (titles[titles.size() - expectedEnd.size() + i], expectedEnd[i]);

            expectEquals (titles.indexOf ("Tap 1 Bit Crusher Maximum Rate Reduction"), 43);
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
        }

        beginTest ("Every control has a help tag");
        {
            for (auto* c : tabOrder (ed))
                if (auto* handler = c->getAccessibilityHandler())
                    expect (handler->getHelp().isNotEmpty(), "No help for " + titleOf (*c));
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
            }
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
