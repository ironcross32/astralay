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
            const auto original = processor.getPresetName();
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
            expectEquals (processor.getPresetName(), original);
            expect (processor.isPresetModified());
            expectWithinAbsoluteError (plain (time), 500.0f, 0.01f);
            expectWithinAbsoluteError (plain (parameter (processor, global::mix)), 70.0f, 0.01f);

            processor.getHistory().redo();
            expectEquals (processor.getPresetName(), juce::String ("Slapback"));
        }

        beginTest ("A random name is two or three lower case words joined by hyphens");
        {
            juce::Random random (1234);
            bool two = false, three = false;

            for (int i = 0; i < 200; ++i)
            {
                const auto made = state::Presets::randomName (random);
                const auto words = juce::StringArray::fromTokens (made, "-", {});

                two = two || words.size() == 2;
                three = three || words.size() == 3;
                expect (words.size() == 2 || words.size() == 3, made);
                expect (made.containsOnly ("abcdefghijklmnopqrstuvwxyz-") && ! words.contains (juce::String()), made);
                expectEquals (state::Presets::legalName (made), made);
            }

            expect (two && three);
        }

        beginTest ("A new instance and the Init preset get random names; other factory presets keep theirs");
        {
            AstralayProcessor processor;
            const auto first = processor.getPresetName();
            expect (juce::StringArray::fromTokens (first, "-", {}).size() >= 2, first);
            expect (! processor.isPresetModified());

            const auto announced = processor.loadFactoryPreset (state::Presets::initIndex);
            const auto second = processor.getPresetName();
            expect (second != "Init" && second != first, second);
            expectEquals (announced, "Loaded " + second);

            processor.loadFactoryPreset (1);
            expectEquals (processor.getPresetName(), state::Presets::factory()[1].name);

            // A session keeps the name it was saved with.
            processor.setPresetName ("Kept by the session");
            juce::MemoryBlock saved;
            processor.getStateInformation (saved);

            AstralayProcessor other;
            other.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (other.getPresetName(), juce::String ("Kept by the session"));
        }

        beginTest ("A typed or random name isn't an undo step, and only undoing a load changes the name");
        {
            AstralayProcessor processor;
            processor.setPresetName ("Typed");
            expect (! processor.getHistory().canUndo());
            expect (! processor.isPresetModified());

            const auto random = processor.randomisePresetName();
            expectEquals (processor.getPresetName(), random);
            expect (! processor.getHistory().canUndo());
            expect (! processor.isPresetModified());

            // Undoing and redoing edits, of either kind, leaves a name typed since alone.
            edit (parameter (processor, global::mix), 70.0f);
            processor.renameMacro (0, "Sweep");
            processor.setPresetName ("Typed later");

            processor.getHistory().undo();
            processor.getHistory().undo();
            expectEquals (processor.getPresetName(), juce::String ("Typed later"));
            processor.getHistory().redo();
            processor.getHistory().redo();
            expectEquals (processor.getPresetName(), juce::String ("Typed later"));

            // Undoing a load brings back the name there was when it was loaded.
            processor.loadFactoryPreset (1);
            processor.setPresetName ("My slapback");
            processor.getHistory().undo();
            expectEquals (processor.getPresetName(), juce::String ("Typed later"));
        }

        beginTest ("Names are made legal for files");
        {
            using state::Presets::legalName;

            expectEquals (legalName ("  My preset  "), juce::String ("My preset"));
            expectEquals (legalName ("a/b\\c:d*e?f\"g<h>i|j"), juce::String ("abcdefghij"));
            expectEquals (legalName ("...hidden. ."), juce::String ("hidden"));
            expectEquals (legalName ("Version 1.5"), juce::String ("Version 1.5"));
            expectEquals (legalName ("Cafe #2, wet & dry"), juce::String ("Cafe #2, wet & dry"));
            expectEquals (legalName (juce::String::repeatedString ("x", 100)).length(), state::Presets::maxNameLength);

            for (const auto* refused : { "", "   ", "???", "CON", "nul", "com1", "LPT9.old", ". ." })
                expect (legalName (refused).isEmpty(), refused);

            expectEquals (legalName ("Console"), juce::String ("Console"));
            expectEquals (legalName ("COM10"), juce::String ("COM10"));
        }

        beginTest ("Save writes the named preset to the user folder, replacing one of the same name");
        {
            const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("AstralayPresetTests", "", false);
            const auto folder = root.getChildFile ("Presets");
            const auto names = [&]
            {
                juce::StringArray found;

                for (const auto& file : state::Presets::userPresets (folder))
                    found.add (file.getFileNameWithoutExtension());

                return found.joinIntoString ("|");
            };

            {
                AstralayProcessor processor (root.getChildFile ("MIDI Mappings"), folder);
                expect (state::Presets::userPresets (folder).isEmpty());

                processor.setPresetName ("  ");
                expectEquals (processor.savePreset(), juce::String ("Enter a preset name"));
                processor.setPresetName ("CON");
                expectEquals (processor.savePreset(), juce::String ("CON can't be used as a preset name"));
                expect (! folder.exists());

                processor.setPresetName ("zebra");
                edit (parameter (processor, global::mix), 70.0f);
                expectEquals (processor.savePreset(), juce::String ("Saved zebra"));
                expect (! processor.isPresetModified());

                edit (parameter (processor, global::mix), 80.0f);
                expectEquals (processor.savePreset(), juce::String ("Replaced zebra"));
                expectEquals (names(), juce::String ("zebra"));

                // A changed name is a new preset, with the characters a file can't have left out.
                processor.setPresetName (" Apple: pie? ");
                edit (parameter (processor, global::mix), 90.0f);
                expectEquals (processor.savePreset(), juce::String ("Saved Apple pie"));
                expectEquals (processor.getPresetName(), juce::String ("Apple pie"));
                expectEquals (names(), juce::String ("Apple pie|zebra"));

                // Only the case differs, so it is the same preset under the name as typed.
                processor.setPresetName ("ZEBRA");
                expectEquals (processor.savePreset(), juce::String ("Replaced ZEBRA"));
                expectEquals (names(), juce::String ("Apple pie|ZEBRA"));

                // Folders inside the folder, and other files, aren't presets.
                expect (folder.getChildFile ("Pads").createDirectory());
                expect (folder.getChildFile ("Pads").getChildFile ("inner.astralay").replaceWithText ("<AstralayPreset/>"));
                expect (folder.getChildFile ("notes.txt").replaceWithText ("x"));
                expectEquals (names(), juce::String ("Apple pie|ZEBRA"));

                // A random name is never one that is taken.
                for (int i = 0; i < 50; ++i)
                    expect (! state::Presets::fileFor (folder, processor.randomisePresetName()).existsAsFile());
            }

            AstralayProcessor other (root.getChildFile ("MIDI Mappings"), folder);
            expectEquals (other.loadPresetFile (state::Presets::fileFor (folder, "Apple pie")), juce::String ("Loaded Apple pie"));
            expectWithinAbsoluteError (plain (parameter (other, global::mix)), 90.0f, 0.01f);
            expectEquals (other.getPresetName(), juce::String ("Apple pie"));

            expect (root.deleteRecursively());
        }

        beginTest ("Saving and loading a preset file restores every value and the name");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("Astralay test preset.astralay");

            AstralayProcessor processor;
            edit (parameter (processor, tapId (4, tap::pan)), -35.0f);
            edit (parameter (processor, global::sync), 1.0f);
            edit (parameter (processor, global::freezeSustain), 1.0f);

            file.deleteFile();
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
            const auto original = processor.getPresetName();
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("not a preset.astralay");
            file.replaceWithText ("<SomethingElse/>");

            expect (processor.loadPresetFile (file).startsWith ("Could not load"));
            expectEquals (processor.getPresetName(), original);
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
