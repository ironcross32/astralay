#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using namespace astralay;
using state::MidiMappings;

struct HostNotifications final : juce::AudioProcessorListener
{
    int values = 0, gestures = 0, mappingChanges = 0;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++values; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
    { if (details.nonParameterStateChanged) ++mappingChanges; }
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++gestures; }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++gestures; }
};

class MidiTests final : public juce::UnitTest
{
public:
    MidiTests() : UnitTest ("MIDI", "Astralay") {}
    void runTest() override
    {
        using namespace params;
        testFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile ("AstralayMidiSuite", "", false).getChildFile ("MIDI Mappings");
        AstralayProcessor p (testFolder);
        auto& midi = p.getMidiMappings();
        midi.clear();
        p.getHistory().clear();
        expect (! p.isPresetModified(), "Mapping edits must not mark a sound preset modified");
        auto* mix = p.getState().getParameter (global::mix);
        auto* gain = p.getState().getParameter (global::outputGain);
        auto* sync = p.getState().getParameter (global::sync);

        beginTest ("All CCs and all channels, exact matching, absolute endpoints");
        for (int channel = 1; channel <= 16; ++channel)
            for (int cc = 0; cc <= 127; ++cc)
            {
                midi.bind (global::mix, { channel, cc });
                midi.process (juce::MidiMessage::controllerEvent (channel, cc, 0));
                expectEquals (mix->getValue(), 0.0f);
                midi.process (juce::MidiMessage::controllerEvent (channel % 16 + 1, cc, 127));
                expectEquals (mix->getValue(), 0.0f);
                midi.process (juce::MidiMessage::controllerEvent (channel, cc, 127));
                expectEquals (mix->getValue(), 1.0f);
            }
        beginTest ("Pitch bend endpoints and exact centre; CC source replacement");
        midi.bind (global::mix, { 3, -1 });
        for (const auto [input, expected] : { std::pair { 0, 0.0f }, { 8192, 0.5f }, { 16383, 1.0f } })
        {
            midi.process (juce::MidiMessage::pitchWheel (3, input));
            expectWithinAbsoluteError (mix->getValue(), expected, 0.00001f);
        }
        midi.process (juce::MidiMessage::controllerEvent (16, 127, 0));
        expectEquals (mix->getValue(), 1.0f);

        beginTest ("Capture suppresses every target, then fans out before UI timer runs");
        midi.bind (global::mix, { 1, 7 });
        mix->setValueNotifyingHost (0.5f);
        gain->setValueNotifyingHost (0.5f);
        midi.learn (global::outputGain);
        midi.process (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
        expect (midi.learnState() == MidiMappings::Learn::midi);
        midi.process (juce::MidiMessage::controllerEvent (1, 7, 127));
        expectWithinAbsoluteError (mix->getValue(), 0.5f, 0.0001f);
        expectWithinAbsoluteError (gain->getValue(), 0.5f, 0.002f);
        midi.process (juce::MidiMessage::controllerEvent (1, 7, 0));
        expectEquals (mix->getValue(), 0.0f);
        expectEquals (gain->getValue(), 0.0f);
        midi.flushCapture();
        expect (midi.bound (global::outputGain));
        midi.waitForTarget();
        midi.process (juce::MidiMessage::controllerEvent (1, 7, 127));
        expectEquals (mix->getValue(), 1.0f);
        expect (midi.learnState() == MidiMappings::Learn::target);
        midi.cancelLearn();

        beginTest ("Logical times follow sync with fixed tap identity and stepped values");
        for (const auto& id : { tapId (2, tap::time), tapId (2, tap::stutterMin), tapId (2, tap::stutterMax), juce::String (global::bufferSize) })
        {
            midi.clear();
            midi.bind (id, { 2, 10 });
            const auto counterpart = id == global::bufferSize ? juce::String (global::bufferSync) : syncedCounterpart (id);
            expectEquals (midi.logicalTarget (counterpart), id);
            sync->setValueNotifyingHost (0);
            midi.process (juce::MidiMessage::controllerEvent (2, 10, 0));
            expectEquals (p.getState().getParameter (id)->getValue(), 0.0f);
            sync->setValueNotifyingHost (1);
            p.setSelectedTap (7);
            midi.process (juce::MidiMessage::controllerEvent (2, 10, 127));
            expectEquals (p.getState().getParameter (counterpart)->getValue(), 1.0f);
            expectEquals (p.getState().getParameter (id)->getValue(), 0.0f);
        }
        expect (! midi.eligible (tapId (0, tap::pitchMode)));
        expect (! midi.eligible (tapId (0, tap::timeSync)));
        expect (! midi.eligible ("outputClip"));
        expect (! midi.eligible ("selectedTap"));
        midi.bind (global::freeze, { 1, 1 });
        midi.process (juce::MidiMessage::controllerEvent (1, 1, 63));
        expectEquals (p.getState().getParameter (global::freeze)->getValue(), 0.0f);
        midi.process (juce::MidiMessage::controllerEvent (1, 1, 64));
        expectEquals (p.getState().getParameter (global::freeze)->getValue(), 1.0f);

        beginTest ("Macro bindings retain slot identity across renaming and bipolar changes");
        midi.bind (macroId (2), { 8, -1 });
        p.renameMacro (2, "Motion");
        p.setMacroBipolar (2, true);
        midi.process (juce::MidiMessage::pitchWheel (8, 8192));
        auto* macro = dynamic_cast<MacroParameter*> (p.getState().getParameter (macroId (2)));
        expect (macro != nullptr);
        if (macro != nullptr) expectEquals (macro->getMacroValue(), 0.0f);
        midi.process (juce::MidiMessage::pitchWheel (8, 0));
        if (macro != nullptr) expectEquals (macro->getMacroValue(), -1.0f);
        expect (midi.bound (macroId (2)));

        beginTest ("MIDI edits stored base values; sound dirty, mapping clean, no undo flood");
        p.loadFactoryPreset (0);
        midi.clear();
        midi.bind (global::mix, { 1, 7 });
        p.getHistory().clear();
        HostNotifications host;
        p.addListener (&host);
        midi.bind (global::mix, { 1, 7 });
        expectEquals (host.mappingChanges, 1);
        expectEquals (host.values, 0);
        expect (! p.isPresetModified());
        p.getHistory().clear();
        p.prepareToPlay (48000, 128);
        juce::AudioBuffer<float> audio (2, 128);
        audio.clear();
        juce::MidiBuffer events;
        events.addEvent (juce::MidiMessage::controllerEvent (1, 7, 0), 40);
        events.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 50);
        events.addEvent (juce::MidiMessage::pitchWheel (16, 3000), 60);
        p.processBlock (audio, events);
        expectEquals (mix->getValue(), 0.0f);
        expect (p.isPresetModified());
        expect (! p.getHistory().canUndo());
        expect (host.values > 0);
        expectEquals (host.gestures, 0);
        p.removeListener (&host);
        expectEquals (events.getNumEvents(), 3);
        expectEquals ((*events.begin()).samplePosition, 40);
        auto event = events.begin();
        expectEquals ((*event++).getMessage().getControllerValue(), 0);
        expectEquals ((*event++).getMessage().getNoteNumber(), 60);
        expectEquals ((*event).getMessage().getPitchWheelValue(), 3000);

        beginTest ("Project snapshot preserves unsaved mapping; sound presets leave it alone");
        juce::MemoryBlock block;
        midi.learn (global::glide);
        p.getStateInformation (block);
        AstralayProcessor restored (testFolder);
        restored.setStateInformation (block.getData(), (int) block.getSize());
        expect (restored.getMidiMappings().current().bindings == midi.current().bindings);
        expect (restored.getMidiMappings().current().dirty);
        expect (restored.getMidiMappings().learnState() == MidiMappings::Learn::inactive);
        restored.loadFactoryPreset (1);
        expect (restored.getMidiMappings().bound (global::mix));
        midi.cancelLearn();

        beginTest ("Mapping-only undo leaves intervening sound values intact");
        midi.clear();
        mix->setValueNotifyingHost (0.7f);
        p.getHistory().undo();
        expect (midi.bound (global::mix));

        expectWithinAbsoluteError (mix->getValue(), 0.7f, 0.0001f);
        p.getHistory().redo();
        expect (midi.current().bindings.empty());
        p.getHistory().undo();
        midi.remove (global::mix);
        expect (midi.current().bindings.empty() && midi.current().file == juce::File());
        p.getHistory().undo();
        expect (midi.bound (global::mix));

        midi.clear();
        p.getStateInformation (block);
        restored.getMidiMappings().bind (global::mix, { 1, 1 });
        restored.setStateInformation (block.getData(), (int) block.getSize());
        expect (restored.getMidiMappings().current().bindings.empty());

        files (p);
        timing();
        editor();
        smoothing();
        expect (testFolder.getParentDirectory().deleteRecursively());
    }

    void smoothing()
    {
        using namespace params;
        using Mode = state::MidiSmoothing::Mode;
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile ("AstralaySmoothingTests", "", false);
        const auto folder = root.getChildFile ("MIDI Mappings");
        const auto settings = root.getChildFile ("Settings.json");
        {
            AstralayProcessor p (folder), other (folder);
            auto& midi = p.getMidiMappings();
            auto* mix = p.getState().getParameter (global::mix);
            midi.bind (global::mix, { 1, 7 });
            const auto event = [&] (int value) { midi.process (juce::MidiMessage::controllerEvent (1, 7, value)); };
            const auto advance = [&] (int samples)
            {
                while (samples > 0)
                {
                    const auto count = juce::jmin (samples, midi.samplesUntilSmoothingUpdate());
                    midi.advanceSmoothing (count);
                    samples -= count;
                }
            };

            beginTest ("Shared preference, fixed menu labels, persistence independent of mapping/project");
            expect (midi.smoothingMode() == Mode::off);
            expect (midi.setSmoothingMode (Mode::linearFast).wasOk());
            expect (other.getMidiMappings().smoothingMode() == Mode::linearFast);
            expectEquals (state::MidiSmoothing::label (Mode::linearFast), juce::String ("Linear Fast"));
            expectEquals (state::MidiSmoothing::label (Mode::exponentialSlow), juce::String ("Exponential Slow"));
            expectEquals (juce::JSON::parse (settings)["midiSmoothing"].toString(), juce::String ("linearFast"));
            expect (! midi.serialise (midi.current()).contains ("Smoothing"));
            expect (! midi.projectState()->toString().contains ("Smoothing"));

            beginTest ("Linear duration and halfway value at multiple sample rates; duplicate events do not restart");
            for (const auto rate : { 44100.0, 48000.0, 96000.0 })
            {
                midi.prepare (rate);
                mix->setValueNotifyingHost (0);
                event (127);
                expectEquals (mix->getValue(), 0.0f);
                const auto duration = (int) std::round (rate * 0.020);
                advance (duration / 2);
                // Updates are bounded to approximately 1 ms, even at non-integer-ms sample rates.
                expectWithinAbsoluteError (mix->getValue(), 0.5f, 0.051f);
                event (127);
                advance (duration - duration / 2);
                expectEquals (mix->getValue(), 1.0f);
            }
            midi.prepare (48000);

            beginTest ("Linear retarget starts from current value; slow is 80 ms");
            mix->setValueNotifyingHost (0);
            event (127);
            advance (480);
            event (0);
            advance (480);
            expectWithinAbsoluteError (mix->getValue(), 0.25f, 0.00001f);
            advance (480);
            expectEquals (mix->getValue(), 0.0f);
            expect (midi.setSmoothingMode (Mode::linearSlow).wasOk());
            event (127);
            advance (1920);
            expectWithinAbsoluteError (mix->getValue(), 0.5f, 0.00001f);
            advance (1920);
            expectEquals (mix->getValue(), 1.0f);

            beginTest ("Changing speed preserves current value and uses the new duration");
            expect (midi.setSmoothingMode (Mode::linearFast).wasOk());
            mix->setValueNotifyingHost (0);
            event (127);
            advance (480);
            expect (midi.setSmoothingMode (Mode::linearSlow).wasOk());
            midi.refreshSmoothing();
            expectWithinAbsoluteError (mix->getValue(), 0.5f, 0.00001f);
            advance (1920);
            expectWithinAbsoluteError (mix->getValue(), 0.75f, 0.00001f);
            advance (1920);
            expectEquals (mix->getValue(), 1.0f);

            beginTest ("Exponential closes 99 percent in selected time and settles at exact endpoints");
            for (const auto mode : { Mode::exponentialFast, Mode::exponentialSlow })
            {
                expect (midi.setSmoothingMode (mode).wasOk());
                mix->setValueNotifyingHost (0);
                event (127);
                const auto duration = (int) std::round (48000 * state::MidiSmoothing::seconds (mode));
                advance (duration / 2);
                expectWithinAbsoluteError (mix->getValue(), 0.9f, 0.00001f);
                event (127);
                advance (duration / 2);
                expectWithinAbsoluteError (mix->getValue(), 0.99f, 0.00001f);
                advance (duration * 3);
                expectEquals (mix->getValue(), 1.0f);
                event (0);
                advance (duration * 4);
                expectEquals (mix->getValue(), 0.0f);
            }

            beginTest ("Mouse/automation edit cancels pending movement; next MIDI event resumes");
            expect (midi.setSmoothingMode (Mode::linearFast).wasOk());
            event (127);
            advance (240);
            mix->setValueNotifyingHost (0.3f);
            advance (1920);
            expectWithinAbsoluteError (mix->getValue(), 0.3f, 0.00001f);
            event (127);
            advance (480);
            expectWithinAbsoluteError (mix->getValue(), 0.65f, 0.00001f);
            expect (other.getMidiMappings().setSmoothingMode (Mode::off).wasOk());
            midi.refreshSmoothing();
            expectEquals (mix->getValue(), 1.0f);

            beginTest ("Pitch bend fan-out smooths macros and skewed parameters in normalized space");
            expect (midi.setSmoothingMode (Mode::linearFast).wasOk());
            const auto cutoffId = tapId (0, tap::highCut);
            for (const auto& id : { juce::String (global::outputGain), macroId (0), cutoffId })
            {
                midi.bind (id, { 1, -1 });
                p.getState().getParameter (id)->setValueNotifyingHost (0);
            }
            midi.process (juce::MidiMessage::pitchWheel (1, 8192));
            advance (480);
            for (const auto& id : { juce::String (global::outputGain), macroId (0), cutoffId })
                expectWithinAbsoluteError (p.getState().getParameter (id)->getValue(), 0.25f, 0.00001f);

            beginTest ("Learn capture cancels replaced movement and preserves subsequent ramp through UI publication");
            auto* gain = p.getState().getParameter (global::outputGain);
            midi.learn (global::outputGain);
            event (0);
            expectWithinAbsoluteError (gain->getValue(), 0.25f, 0.00001f);
            expectEquals (mix->getValue(), 1.0f);
            advance (960);
            expectWithinAbsoluteError (gain->getValue(), 0.25f, 0.00001f);
            event (0);
            midi.flushCapture();
            advance (960);
            expectEquals (gain->getValue(), 0.0f);
            expectEquals (mix->getValue(), 0.0f);

            beginTest ("Switches, integers, and synced note choices bypass smoothing");
            expect (midi.setSmoothingMode (Mode::linearSlow).wasOk());
            for (const auto& id : { juce::String (global::freeze), juce::String (global::maxGlitches) })
            {
                midi.bind (id, { 1, 8 });
                midi.process (juce::MidiMessage::controllerEvent (1, 8, 127));
                expectEquals (p.getState().getParameter (id)->getValue(), 1.0f);
            }
            const auto timeId = tapId (0, tap::time);
            midi.bind (timeId, { 1, 9 });
            p.getState().getParameter (timeId)->setValueNotifyingHost (0);
            midi.process (juce::MidiMessage::controllerEvent (1, 9, 127));
            advance (480);
            const auto timeBefore = p.getState().getParameter (timeId)->getValue();
            p.getState().getParameter (global::sync)->setValueNotifyingHost (1);
            advance (3840);
            expectEquals (p.getState().getParameter (timeId)->getValue(), timeBefore);
            midi.process (juce::MidiMessage::controllerEvent (1, 9, 127));
            expectEquals (p.getState().getParameter (syncedCounterpart (timeId))->getValue(), 1.0f);

            beginTest ("Binding removal and preset/project restore cancel pending movement");
            mix->setValueNotifyingHost (0);
            event (127);
            advance (480);
            midi.remove (global::mix);
            advance (3840);
            expectWithinAbsoluteError (mix->getValue(), 0.125f, 0.00001f);
            midi.bind (global::mix, { 1, 7 });
            event (127);
            p.loadFactoryPreset (0);
            const auto loaded = mix->getValue();
            advance (3840);
            expectEquals (mix->getValue(), loaded);
            juce::MemoryBlock saved;
            p.getStateInformation (saved);
            event (0);
            p.setStateInformation (saved.getData(), (int) saved.getSize());
            advance (3840);
            expectEquals (mix->getValue(), loaded);
            expect (midi.smoothingMode() == Mode::linearSlow);

            beginTest ("Audio blocks advance without MIDI; event offsets, notifications and passthrough preserved");
            midi.clear();
            midi.bind (global::mix, { 1, 7 });
            expect (midi.setSmoothingMode (Mode::linearFast).wasOk());
            p.prepareToPlay (48000, 128);
            mix->setValueNotifyingHost (0);
            HostNotifications host;
            p.addListener (&host);
            juce::AudioBuffer<float> audio (2, 128);
            juce::MidiBuffer messages;
            messages.addEvent (juce::MidiMessage::controllerEvent (1, 7, 127), 40);
            audio.clear();
            p.processBlock (audio, messages);
            expectWithinAbsoluteError (mix->getValue(), 0.05f, 0.00001f);
            expectEquals ((*messages.begin()).samplePosition, 40);
            expectEquals ((*messages.begin()).getMessage().getControllerValue(), 127);
            messages.clear();
            for (int i = 0; i < 7; ++i) { audio.clear(); p.processBlock (audio, messages); }
            expectEquals (mix->getValue(), 1.0f);
            expect (host.values > 1 && host.values <= 21);
            expectEquals (host.gestures, 0);
            p.removeListener (&host);
            expect (midi.setSmoothingMode (Mode::exponentialSlow).wasOk());
        }
        beginTest ("Preference reload, invalid fallback, unrelated settings preserved and write failure stays active");
        {
            auto preference = state::MidiSmoothing::shared (settings);
            expect (preference->get() == Mode::exponentialSlow);
        }
        expect (settings.replaceWithText ("{\"midiSmoothing\":\"unknown\",\"other\":42}"));
        {
            auto preference = state::MidiSmoothing::shared (settings);
            expect (preference->get() == Mode::off);
            expect (preference->set (Mode::linearFast).wasOk());
            expectEquals ((int) juce::JSON::parse (settings)["other"], 42);
        }
        const auto blocked = root.getChildFile ("blocked");
        expect (blocked.replaceWithText ("keep"));
        {
            auto preference = state::MidiSmoothing::shared (blocked.getChildFile ("Settings.json"));
            expect (preference->set (Mode::linearSlow).failed());
            expect (preference->get() == Mode::linearSlow);
        }
        expect (root.deleteRecursively());
    }

    void files (AstralayProcessor& p)
    {
        using namespace params;
        // Isolated preferences and mappings; never touch the user's default preference.
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("AstralayMidiTests", "", false);
        const auto folder = root.getChildFile ("MIDI Mappings");
        MidiMappings library (p.getState(), p.getHistory(), folder);
        beginTest ("Portable filenames preserve Unicode; reject paths and Windows device names");
        juce::String filename;
        expect (MidiMappings::filename (juce::String::fromUTF8 ("  Caf\xc3\xa9 control.txt  "), filename).isEmpty());
        expectEquals (filename, juce::String::fromUTF8 ("Caf\xc3\xa9 control.json"));
        for (const auto* invalidName : { "", " ", "../test", "test\\file", "CON.json", "LPT9", "aux.txt", "bad.", ".json", "a?.json", "x\ny" })
            expect (MidiMappings::filename (invalidName, filename).isNotEmpty(), invalidName);
        beginTest ("Full JSON validation, no partial loads, one-to-many permitted");
        library.bind (global::mix, { 1, 7 });
        library.bind (global::outputGain, { 1, 7 });
        const auto valid = library.serialise (library.current());
        MidiMappings::Mapping parsed;
        expect (library.parse (valid, parsed).wasOk());
        expectEquals ((int) parsed.bindings.size(), 2);
        for (const auto& invalid : { juce::String ("{}"), valid.replace ("\"version\": 1", "\"version\": 9"),
                                   valid.replace ("\"channel\": 1", "\"channel\": 17"),
                                   valid.replace ("\"controller\": 7", "\"controller\": 128"),
                                   valid.replace ("outputGain", "mix"), valid.replace ("outputGain", "t01_pitch_mode"),
                                   valid.replace ("\"cc\"", "\"notes\""), library.serialise ({}) })
            expect (library.parse (invalid, parsed).failed(), invalid);
        const auto file = folder.getChildFile ("Controller.json");
        expect (library.save (file, false).wasOk());
        expect (! library.current().dirty);
        const auto state = library.projectState();
        expect (library.setDefault().wasOk());
        library.process (juce::MidiMessage::controllerEvent (1, 7, 127));
        expect (! library.current().dirty);
        {
            MidiMappings next (p.getState(), p.getHistory(), folder);
            expect (next.bound (global::mix));
            next.restoreProject (nullptr);
            expect (next.current().bindings.empty());
        }
        expect (file.replaceWithText ("invalid"));
        expect (library.externallyChanged());
        expect (library.scan().empty());
        expect (library.load (file).failed());
        expect (library.bound (global::mix));
        expect (library.save (file, false).failed());
        expectEquals (file.loadFileAsString(), juce::String ("invalid"));
        expect (library.setDefault().failed());
        {
            MidiMappings next (p.getState(), p.getHistory(), folder);
            expect (next.current().bindings.empty());
            expect (next.startupStatus().isNotEmpty());
            next.restoreProject (state.get());
            expect (next.bound (global::mix));
            expect (next.externallyChanged());
        }
        expect (library.save (file, true).wasOk());
        expectEquals ((int) library.scan().size(), 1);
        library.bind (global::freeze, { 4, -1 });
        expect (library.current().dirty);
        library.clear();
        p.getHistory().undo();
        expect (library.current().dirty && library.current().file == file && library.bound (global::freeze));
        library.clear();
        expect (library.setDefault().wasOk());
        expect (library.defaultFile() == juce::File());
        expect (file.existsAsFile());
        expect (library.save (file, true).failed());
        const auto blockingFile = root.getChildFile ("blocked");
        expect (blockingFile.replaceWithText ("keep"));
        library.bind (global::mix, { 1, 1 });
        expect (library.save (blockingFile.getChildFile ("fail.json"), true).failed());
        expect (library.bound (global::mix));
        expectEquals (blockingFile.loadFileAsString(), juce::String ("keep"));
        p.getHistory().clear(); // Actions refer to this isolated library.
        expect (root.deleteRecursively());
    }

    void timing()
    {
        beginTest ("Event offset matches rendering the same update between blocks");
        AstralayProcessor whole (testFolder), split (testFolder);
        for (auto* p : { &whole, &split })
        {
            p->loadFactoryPreset (0);
            p->getMidiMappings().clear();
            p->getMidiMappings().bind (params::global::mix, { 1, 7 });
            p->prepareToPlay (48000, 128);
        }
        juce::AudioBuffer<float> a (2, 128), b (2, 40), c (2, 88);
        for (auto* buffer : { &a, &b, &c })
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer->getNumSamples(); ++i) buffer->setSample (ch, i, 0.3f);
        juce::MidiBuffer message, empty;
        message.addEvent (juce::MidiMessage::controllerEvent (1, 7, 0), 40);
        whole.processBlock (a, message);
        split.processBlock (b, empty);
        message.clear();
        message.addEvent (juce::MidiMessage::controllerEvent (1, 7, 0), 0);
        split.processBlock (c, message);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 128; ++i)
                expectWithinAbsoluteError (a.getSample (ch, i), i < 40 ? b.getSample (ch, i) : c.getSample (ch, i - 40), 0.000001f);
    }

    void editor()
    {
        beginTest ("Editor target selection consumes Enter, remains fixed on navigation, closes cleanly");
        AstralayProcessor p (testFolder);
        p.getMidiMappings().clear();
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p.createEditor());
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        const auto find = [&] (const juce::String& title) -> juce::Component*
        {
            for (auto* c : juce::KeyboardFocusTraverser().getAllComponents (editor.get()))
                if (c->getTitle() == title || (c->getAccessibilityHandler() && c->getAccessibilityHandler()->getTitle() == title)) return c;
            return nullptr;
        };
        auto* learn = dynamic_cast<juce::Button*> (find ("MIDI learn"));
        auto* toggle = find ("Tap 1 Enabled");
        auto* time = dynamic_cast<ui::ParameterSlider*> (find ("Tap 1 Time"));
        auto* selector = dynamic_cast<juce::ComboBox*> (find ("Selected tap"));
        expect (learn && toggle && time && selector);
        if (learn && toggle && time && selector)
        {
            learn->onClick();
            expect (p.getMidiMappings().learnState() == MidiMappings::Learn::target);
            const auto before = p.getState().getParameter (params::tapId (0, params::tap::enabled))->getValue();
            expect (toggle->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
            expectEquals (p.getState().getParameter (params::tapId (0, params::tap::enabled))->getValue(), before);
            selector->setSelectedId (4, juce::sendNotificationSync);
            p.getMidiMappings().process (juce::MidiMessage::controllerEvent (1, 3, 0));
            expect (p.getMidiMappings().bound (params::tapId (0, params::tap::enabled)));
            learn->onClick();
            const auto timeBefore = time->getValue();
            const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), { 1.0f, 1.0f },
                                    juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, time, time, now,
                                    { 1.0f, 1.0f }, now, 1, false);
            time->mouseDown (click);
            time->mouseDrag (click);
            time->mouseUp (click);
            expectEquals (time->getValue(), timeBefore);
            expect (p.getMidiMappings().learnState() == MidiMappings::Learn::midi);
            expect (editor->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
            expect (p.getMidiMappings().learnState() == MidiMappings::Learn::inactive);

            // Nested popup windows still receive the cancellation shortcut before their own keys.
            auto* main = dynamic_cast<juce::Button*> (find ("Main menu"));
            if (main != nullptr)
            {
                learn->onClick();
                main->onClick();
                auto* popup = juce::Component::getCurrentlyModalComponent();
                expect (popup != nullptr && popup->getPeer() != nullptr);
                if (popup != nullptr && popup->getPeer() != nullptr)
                {
                    popup->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::rightKey));
                    if (auto* submenu = juce::Component::getCurrentlyModalComponent(); submenu != nullptr && submenu->getPeer() != nullptr)
                        submenu->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::escapeKey));
                    expect (p.getMidiMappings().learnState() == MidiMappings::Learn::inactive);
                }
                juce::PopupMenu::dismissAllActiveMenus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            }

            // Arming cancels learning; entering learning disarms before binding a sound value.
            auto* arm = dynamic_cast<juce::Button*> (find ("Arm Macro 1"));
            if (arm != nullptr)
            {
                arm->onClick();
                expect (time->isEditingModulation());
                learn->onClick();
                expect (! time->isEditingModulation());
                arm->onClick();
                expect (p.getMidiMappings().learnState() == MidiMappings::Learn::inactive);
            }
            learn->onClick();
            expect (time->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
            expect (p.getMidiMappings().learnState() == MidiMappings::Learn::midi);
            editor.reset();
            expect (p.getMidiMappings().learnState() == MidiMappings::Learn::inactive);
            expect (p.getMidiMappings().bound (params::tapId (0, params::tap::enabled)));
        }
    }
private:
    juce::File testFolder;
};
MidiTests midiTests;
}
