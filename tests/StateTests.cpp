#include "PluginProcessor.h"
#include "params/Parameters.h"
#include "state/Presets.h"

namespace
{
    using namespace astralay;

    juce::RangedAudioParameter& parameter (AstralayProcessor& processor, const juce::String& id)
    {
        auto* p = processor.getState().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    /** Sets a parameter the way the editor does: as one complete gesture. */
    void edit (juce::RangedAudioParameter& p, float plainValue)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
        p.endChangeGesture();
    }

    float plain (juce::RangedAudioParameter& p)
    {
        return p.convertFrom0to1 (p.getValue());
    }
}

class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("Presets and undo", "Astralay") {}

    void runTest() override
    {
        using namespace params;

        beginTest ("An edit can be undone and redone, with an announcement");
        {
            AstralayProcessor processor;
            auto& feedback = parameter (processor, tapId (0, tap::feedback));

            edit (feedback, 70.0f);
            expect (processor.getHistory().canUndo());

            const auto undone = processor.getHistory().undo();
            expectWithinAbsoluteError (plain (feedback), 40.0f, 0.01f);
            expectEquals (undone, juce::String ("Undo Tap 1 Feedback, 40%"));

            const auto redone = processor.getHistory().redo();
            expectWithinAbsoluteError (plain (feedback), 70.0f, 0.01f);
            expectEquals (redone, juce::String ("Redo Tap 1 Feedback, 70%"));

            expectEquals (processor.getHistory().redo(), juce::String ("Nothing to redo"));
        }

        beginTest ("A quick run of edits to one parameter is one undo step");
        {
            AstralayProcessor processor;
            auto& feedback = parameter (processor, tapId (0, tap::feedback));

            edit (feedback, 41.0f);
            edit (feedback, 42.0f);
            edit (feedback, 43.0f);

            processor.getHistory().undo();
            expectWithinAbsoluteError (plain (feedback), 40.0f, 0.01f);
            expect (! processor.getHistory().canUndo());
        }

        beginTest ("Edits to different parameters are separate steps");
        {
            AstralayProcessor processor;
            auto& feedback = parameter (processor, tapId (0, tap::feedback));
            auto& mix = parameter (processor, global::mix);

            edit (feedback, 60.0f);
            edit (mix, 80.0f);

            processor.getHistory().undo();
            expectWithinAbsoluteError (plain (mix), 50.0f, 0.01f);
            expectWithinAbsoluteError (plain (feedback), 60.0f, 0.01f);

            processor.getHistory().undo();
            expectWithinAbsoluteError (plain (feedback), 40.0f, 0.01f);
        }

        beginTest ("Host automation is not recorded and doesn't mark the preset modified");
        {
            AstralayProcessor processor;
            auto& feedback = parameter (processor, tapId (0, tap::feedback));

            feedback.setValueNotifyingHost (feedback.convertTo0to1 (90.0f));
            expect (! processor.getHistory().canUndo());
            expect (! processor.isPresetModified());
        }

        beginTest ("An edit marks the preset modified");
        {
            AstralayProcessor processor;
            expectEquals (processor.getPresetName(), juce::String ("Init"));
            expect (! processor.isPresetModified());

            edit (parameter (processor, global::mix), 70.0f);
            expect (processor.isPresetModified());
        }

        beginTest ("Every factory preset setting parses to exactly the value written");
        {
            AstralayProcessor processor;

            for (const auto& preset : state::Presets::factory())
            {
                for (const auto& [id, text] : preset.settings)
                {
                    auto* p = processor.getState().getParameter (id);
                    expect (p != nullptr, preset.name + ": unknown parameter " + id);

                    if (p != nullptr)
                        expectEquals (p->getText (p->getValueForText (text), 128), text, preset.name + ": " + id);
                }
            }
        }

        beginTest ("Loading a factory preset is one undoable step that restores the old name");
        {
            AstralayProcessor processor;
            auto& time = parameter (processor, tapId (0, tap::time));
            edit (parameter (processor, global::mix), 70.0f);

            const auto& factory = state::Presets::factory();
            const auto slapback = (int) std::distance (factory.begin(), std::find_if (factory.begin(), factory.end(),
                                                                                       [] (auto& p) { return p.name == "Slapback"; }));

            expectEquals (processor.loadFactoryPreset (slapback), juce::String ("Loaded Slapback"));
            expectEquals (processor.getPresetName(), juce::String ("Slapback"));
            expect (! processor.isPresetModified());
            expectWithinAbsoluteError (plain (time), 120.0f, 0.01f);
            expectWithinAbsoluteError (plain (parameter (processor, global::mix)), 30.0f, 0.01f);

            processor.getHistory().undo();
            expectEquals (processor.getPresetName(), juce::String ("Init"));
            expect (processor.isPresetModified());
            expectWithinAbsoluteError (plain (time), 500.0f, 0.01f);
            expectWithinAbsoluteError (plain (parameter (processor, global::mix)), 70.0f, 0.01f);
        }

        beginTest ("Saving and loading a preset file restores every value and the name");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("Astralay test preset.astralay");

            AstralayProcessor processor;
            edit (parameter (processor, tapId (4, tap::pan)), -35.0f);
            edit (parameter (processor, global::sync), 1.0f);
            edit (parameter (processor, global::freezeSustain), 1.0f);

            expectEquals (processor.savePresetFile (file), juce::String ("Saved Astralay test preset"));
            expect (! processor.isPresetModified());

            AstralayProcessor other;
            expectEquals (other.loadPresetFile (file), juce::String ("Loaded Astralay test preset"));
            expectWithinAbsoluteError (plain (parameter (other, tapId (4, tap::pan))), -35.0f, 0.01f);
            expectWithinAbsoluteError (plain (parameter (other, global::sync)), 1.0f, 0.01f);
            expectEquals (plain (parameter (other, global::freezeSustain)), 1.0f);
            expectEquals (other.getPresetName(), juce::String ("Astralay test preset"));

            file.deleteFile();
        }

        beginTest ("A parameter missing from a preset file takes its default");
        {
            AstralayProcessor processor;
            edit (parameter (processor, global::mix), 90.0f);

            auto xml = state::Presets::toXml (processor, "Partial");

            for (auto* child : xml->getChildIterator())
            {
                if (child->getStringAttribute ("id") == global::mix)
                {
                    xml->removeChildElement (child, true);
                    break;
                }
            }

            state::History::Snapshot loaded;
            expect (state::Presets::fromXml (*xml, processor, loaded));

            auto& mix = parameter (processor, global::mix);
            expectWithinAbsoluteError (loaded.values[global::mix], mix.getDefaultValue(), 1.0e-6f);
        }

        beginTest ("Freeze sustain is undoable and older sessions restore it off");
        {
            AstralayProcessor processor;
            auto& sustain = parameter (processor, global::freezeSustain);
            expectEquals (plain (sustain), 0.0f);
            edit (sustain, 1.0f);
            processor.getHistory().undo();
            expectEquals (plain (sustain), 0.0f);
            processor.getHistory().redo();
            expectEquals (plain (sustain), 1.0f);

            juce::MemoryBlock saved;
            processor.getStateInformation (saved);
            AstralayProcessor other;
            other.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (plain (parameter (other, global::freezeSustain)), 1.0f);

            auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            for (auto* child : xml->getChildIterator())
                if (child->getStringAttribute ("id") == global::freezeSustain)
                {
                    xml->removeChildElement (child, true);
                    break;
                }
            juce::AudioProcessor::copyXmlToBinary (*xml, saved);
            other.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (plain (parameter (other, global::freezeSustain)), 0.0f);

            auto preset = state::Presets::toXml (processor, "Older");
            for (auto* child : preset->getChildIterator())
                if (child->getStringAttribute ("id") == global::freezeSustain)
                {
                    preset->removeChildElement (child, true);
                    break;
                }
            state::History::Snapshot loaded;
            expect (state::Presets::fromXml (*preset, processor, loaded));
            expectEquals (loaded.values[global::freezeSustain], 0.0f);
        }

        beginTest ("Something that isn't a preset is refused");
        {
            AstralayProcessor processor;
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("not a preset.astralay");
            file.replaceWithText ("<SomethingElse/>");

            expect (processor.loadPresetFile (file).startsWith ("Could not load"));
            expectEquals (processor.getPresetName(), juce::String ("Init"));
            file.deleteFile();
        }

        beginTest ("Restoring a session clears the undo history");
        {
            AstralayProcessor processor;
            juce::MemoryBlock saved;
            processor.getStateInformation (saved);

            edit (parameter (processor, global::mix), 80.0f);
            expect (processor.getHistory().canUndo());

            processor.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (! processor.getHistory().canUndo());
        }
    }
};

static StateTests stateTests;
