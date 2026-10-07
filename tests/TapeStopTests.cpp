#include "PluginProcessor.h"
#include "dsp/Engine.h"
#include "params/Parameters.h"
#include "state/Presets.h"

namespace
{
    using namespace astralay;

    constexpr double tapeSampleRate = 48000.0;
    constexpr int tapeBlockSize = 256;

    /** Runs a mono signal through the engine in blocks and returns the left output. */
    std::vector<float> runTape (dsp::Engine& engine, const std::vector<float>& input)
    {
        std::vector<float> left (input), right (input.size(), 0.0f);

        for (size_t start = 0; start < input.size(); start += tapeBlockSize)
        {
            const auto n = (int) std::min ((size_t) tapeBlockSize, input.size() - start);
            engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
        }

        return left;
    }

    std::vector<float> sine (int length, int soundingLength, float frequency)
    {
        std::vector<float> signal ((size_t) length, 0.0f);

        for (int i = 0; i < soundingLength; ++i)
            signal[(size_t) i] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * frequency * (float) i / (float) tapeSampleRate);

        return signal;
    }

    float level (const std::vector<float>& signal, int start, int length)
    {
        double sum = 0.0;

        for (int i = start; i < start + length; ++i)
            sum += (double) signal[(size_t) i] * signal[(size_t) i];

        return (float) std::sqrt (sum / length);
    }

    float peak (const std::vector<float>& signal, int start, int length)
    {
        auto highest = 0.0f;

        for (int i = start; i < start + length; ++i)
            highest = std::max (highest, std::abs (signal[(size_t) i]));

        return highest;
    }

    int zeroCrossings (const std::vector<float>& signal, int start, int length)
    {
        int count = 0;

        for (int i = start + 1; i < start + length; ++i)
            if ((signal[(size_t) i - 1] < 0.0f) != (signal[(size_t) i] < 0.0f))
                ++count;

        return count;
    }

    dsp::GlobalSettings wetTape()
    {
        dsp::GlobalSettings g;
        g.glideSeconds = 0.0f;
        g.mix = 1.0f;
        g.outputGain = 1.0f;
        return g;
    }

    dsp::TapSettings loopTap (float delaySamples, float feedback)
    {
        dsp::TapSettings t;
        t.enabled = true;
        t.delaySamples = delaySamples;
        t.feedback = feedback;
        return t;
    }

    void setUpTape (dsp::Engine& engine, const dsp::GlobalSettings& global, const dsp::TapSettings& tap)
    {
        engine.prepare (tapeSampleRate, tapeBlockSize, 2.0);
        engine.setGlobalSettings (global);
        engine.setTapSettings (0, tap);
        engine.reset();
    }

    juce::RangedAudioParameter& tapeParameter (AstralayProcessor& processor, const char* id)
    {
        auto* p = processor.getState().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    /** Sets a parameter the way the editor does: as one complete gesture. */
    void editAsUser (juce::RangedAudioParameter& p, float normalisedValue)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (normalisedValue);
        p.endChangeGesture();
    }
}

class TapeStopTests final : public juce::UnitTest
{
public:
    TapeStopTests() : juce::UnitTest ("Tape stop", "Astralay") {}

    void runTest() override
    {
        using namespace params;

        beginTest ("The tape takes the stop time to stop and the start time to start");
        {
            dsp::TapeClock clock;
            clock.prepare (tapeSampleRate);
            clock.reset (false);
            expect (clock.isAtFullSpeed());

            clock.setSettings (true, 0.5f, 0.25f);
            int samples = 0, tapeSamples = 0;

            while (! clock.isStopped() && samples < 100000)
            {
                tapeSamples += clock.advance() ? 1 : 0;
                ++samples;
            }

            expectWithinAbsoluteError (samples, 24000, 1);

            // A straight line in speed covers half the distance.
            expectWithinAbsoluteError (tapeSamples, 12000, 2);
            expectEquals (clock.getGain(), 0.0f);
            expect (! clock.advance());

            clock.setSettings (false, 0.5f, 0.25f);
            samples = 0;

            while (clock.isSlowed() && samples < 100000)
            {
                clock.advance();
                ++samples;
            }

            expectWithinAbsoluteError (samples, 12000, 1);

            // Then it settles onto whole samples, where the output is the tape's newest sample.
            samples = 0;

            while (! clock.isAtFullSpeed() && samples < 100000)
            {
                expect (clock.advance());
                ++samples;
            }

            expect (samples <= 501);
            expectEquals (clock.getFraction(), 1.0f);
            expectEquals (clock.getGain(), 1.0f);
        }

        beginTest ("Let go part of the way down, the tape starts again from the speed it reached");
        {
            dsp::TapeClock clock;
            clock.prepare (tapeSampleRate);
            clock.reset (false);
            clock.setSettings (true, 0.5f, 0.25f);

            for (int i = 0; i < 12000; ++i)
                clock.advance();

            expectWithinAbsoluteError (clock.getSpeed(), 0.5, 1.0e-3);

            clock.setSettings (false, 0.5f, 0.25f);
            int samples = 0;

            while (clock.isSlowed() && samples < 100000)
            {
                clock.advance();
                ++samples;
            }

            expectWithinAbsoluteError (samples, 6000, 2);

            // And stopped again part of the way up, it slows from there.
            clock.reset (true);
            clock.setSettings (false, 0.5f, 0.25f);

            for (int i = 0; i < 6000; ++i)
                clock.advance();

            clock.setSettings (true, 0.5f, 0.25f);
            samples = 0;

            while (! clock.isStopped() && samples < 100000)
            {
                clock.advance();
                ++samples;
            }

            expectWithinAbsoluteError (samples, 12000, 2);
        }

        beginTest ("A tape that is never stopped is not changed by the stop and start times");
        {
            const auto input = sine (48000, 24000, 440.0f);
            auto global = wetTape();

            dsp::Engine first, second;
            setUpTape (first, global, loopTap (4800.0f, 0.6f));

            global.tapeStopSeconds = 2.0f;
            global.tapeStartSeconds = 0.05f;
            setUpTape (second, global, loopTap (4800.0f, 0.6f));

            const auto a = runTape (first, input);
            const auto b = runTape (second, input);

            expect (a == b);
            expect (level (a, 24000, 24000) > 0.01f);
            expect (first.getTape().isAtFullSpeed());
        }

        beginTest ("A stopped tape is silent, and the dry signal still passes");
        {
            auto global = wetTape();
            global.mix = 0.5f;
            global.tapeStopped = true;

            dsp::Engine engine;
            setUpTape (engine, global, loopTap (2400.0f, 0.6f));
            expect (engine.getTape().isStopped());

            const auto input = sine (24000, 24000, 440.0f);
            const auto output = runTape (engine, input);
            const auto dryGain = std::cos (juce::MathConstants<float>::halfPi * 0.5f);
            auto worst = 0.0f;

            for (size_t i = 0; i < input.size(); ++i)
                worst = std::max (worst, std::abs (output[i] - input[i] * dryGain));

            expect (worst < 1.0e-6f);
        }

        beginTest ("What is played into a stopped tape is not recorded");
        {
            const auto playAndListen = [] (bool stopped)
            {
                auto global = wetTape();
                global.tapeStopped = stopped;
                global.tapeStartSeconds = 0.05f;

                dsp::Engine engine;
                const auto tap = loopTap (4800.0f, 0.7f);
                setUpTape (engine, global, tap);
                runTape (engine, sine (24000, 24000, 440.0f));

                global.tapeStopped = false;
                engine.setGlobalSettings (global);
                engine.setTapSettings (0, tap);
                return runTape (engine, std::vector<float> (48000, 0.0f));
            };

            const auto afterStopped = playAndListen (true);
            const auto afterRunning = playAndListen (false);

            expect (peak (afterStopped, 0, 48000) < 1.0e-6f);
            expect (level (afterRunning, 0, 48000) > 0.01f);
        }

        beginTest ("Stopping drops the pitch to silence, and starting carries on from there");
        {
            auto global = wetTape();
            global.tapeStopSeconds = 2.0f;
            global.tapeStartSeconds = 0.25f;

            // A loop that closes on itself: the burst is twice as long as the delay.
            const auto tap = loopTap (12000.0f, 0.9f);

            dsp::Engine engine;
            setUpTape (engine, global, tap);

            const auto running = runTape (engine, sine (48000, 24000, 1000.0f));
            const auto crossingsBefore = zeroCrossings (running, 36000, 9600);
            const auto levelBefore = level (running, 36000, 9600);
            expectWithinAbsoluteError (crossingsBefore, 400, 8);

            global.tapeStopped = true;
            engine.setGlobalSettings (global);
            engine.setTapSettings (0, tap);

            const auto stopping = runTape (engine, std::vector<float> (105600, 0.0f));

            // Half way down, at half speed, what was recorded at full speed is an octave lower.
            const auto crossingsHalfway = zeroCrossings (stopping, 43200, 9600);
            expect (crossingsHalfway > 170 && crossingsHalfway < 230,
                    juce::String (crossingsHalfway) + " zero crossings half way down");

            expect (engine.getTape().isStopped());
            expectEquals (peak (stopping, 96100, 9500), 0.0f);

            global.tapeStopped = false;
            engine.setGlobalSettings (global);
            engine.setTapSettings (0, tap);

            const auto starting = runTape (engine, std::vector<float> (24000, 0.0f));
            expect (engine.getTape().isAtFullSpeed());

            // The loop is where it was left: at pitch again, having lost only what it would have
            // in the time the tape actually ran.
            expectWithinAbsoluteError (zeroCrossings (starting, 14400, 9600), 400, 8);
            expect (level (starting, 14400, 9600) > 0.3f * levelBefore);

            auto finite = true;

            for (const auto* signal : { &running, &stopping, &starting })
                for (const auto sample : *signal)
                    finite = finite && std::isfinite (sample);

            expect (finite);
        }

        beginTest ("What is played into a slowing tape doesn't come back as a squeal");
        {
            dsp::TapeClock clock;
            clock.prepare (tapeSampleRate);
            clock.reset (false);
            expectEquals (clock.getRecordGain(), 1.0f);

            clock.setSettings (true, 0.5f, 0.25f);

            for (int i = 0; i < 6000; ++i)
                clock.advance();

            expectWithinAbsoluteError (clock.getRecordGain(), 0.25f, 0.01f);

            for (int i = 0; i < 6000; ++i)
                clock.advance();

            expectWithinAbsoluteError (clock.getRecordGain(), 0.0f, 1.0e-4f);

            // A steady tone into one repeat, with the tape stopped for a second in the middle.
            // What was recorded on the way down and up returns once the tape is fast again.
            auto global = wetTape();
            const auto tap = loopTap (12000.0f, 0.0f);

            dsp::Engine engine;
            setUpTape (engine, global, tap);

            const auto tone = sine (48000, 48000, 440.0f);
            const auto before = runTape (engine, tone);
            const auto reference = level (before, 43200, 4800);

            global.tapeStopped = true;
            engine.setGlobalSettings (global);
            engine.setTapSettings (0, tap);
            runTape (engine, tone);

            global.tapeStopped = false;
            engine.setGlobalSettings (global);
            engine.setTapSettings (0, tap);
            const auto after = runTape (engine, tone);

            // In 10 ms windows from the tape reaching full speed: nothing audible is an octave
            // sharp, and nothing near full level is more than about 4 semitones sharp.
            auto highestAudible = 0, highestLoud = 0;

            for (int start = 12480; start + 480 <= 48000; start += 480)
            {
                const auto relative = level (after, start, 480) / reference;
                const auto pitch = zeroCrossings (after, start, 480) * 50;

                if (relative > 0.01f)
                    highestAudible = std::max (highestAudible, pitch);

                if (relative > 0.5f)
                    highestLoud = std::max (highestLoud, pitch);
            }

            expect (highestAudible < 880, "Audible up to " + juce::String (highestAudible) + " Hz");
            expect (highestLoud > 0 && highestLoud <= 550, "Loud up to " + juce::String (highestLoud) + " Hz");

            // And the repeat is back in full afterwards.
            expectWithinAbsoluteError (level (after, 43200, 4800) / reference, 1.0f, 0.02f);
        }

        beginTest ("The three parameters come after every older one, with their defaults");
        {
            AstralayProcessor processor;
            const auto count = processor.getParameters().size();

            expectEquals (tapeParameter (processor, global::tapeStop).getParameterIndex(), count - 3);
            expectEquals (tapeParameter (processor, global::tapeStopTime).getParameterIndex(), count - 2);
            expectEquals (tapeParameter (processor, global::tapeStartTime).getParameterIndex(), count - 1);
            expectEquals (tapeParameter (processor, global::freezeSustain).getParameterIndex(), count - 4);

            expectEquals (tapeParameter (processor, global::tapeStop).getValue(), 0.0f);
            expectEquals (tapeParameter (processor, global::tapeStopTime).getCurrentValueAsText(), juce::String ("500 ms"));
            expectEquals (tapeParameter (processor, global::tapeStartTime).getCurrentValueAsText(), juce::String ("250 ms"));

            const auto& range = tapeParameter (processor, global::tapeStopTime).getNormalisableRange();
            expectEquals (range.start, 50.0f);
            expectEquals (range.end, 2000.0f);

            expect (! canModulate (global::tapeStop));
            expect (! canModulate (global::tapeStopTime));
            expect (! canModulate (global::tapeStartTime));
        }

        beginTest ("Switching the tape stop is not undoable and doesn't mark the preset modified");
        {
            AstralayProcessor processor;
            auto& stop = tapeParameter (processor, global::tapeStop);

            editAsUser (stop, 1.0f);
            expect (stop.getValue() >= 0.5f);
            expect (! processor.getHistory().canUndo());
            expect (! processor.isPresetModified());

            // An undo of something else leaves it where it is.
            auto& mix = tapeParameter (processor, global::mix);
            editAsUser (mix, 1.0f);
            processor.getHistory().undo();
            expect (stop.getValue() >= 0.5f);

            // The times are ordinary settings.
            editAsUser (tapeParameter (processor, global::tapeStopTime), 1.0f);
            expect (processor.getHistory().canUndo());
            expect (processor.isPresetModified());
        }

        beginTest ("The tape stop is not saved with the session; the times are");
        {
            AstralayProcessor processor;
            auto& stopTime = tapeParameter (processor, global::tapeStopTime);

            tapeParameter (processor, global::tapeStop).setValueNotifyingHost (1.0f);
            stopTime.setValueNotifyingHost (stopTime.convertTo0to1 (1200.0f));

            juce::MemoryBlock saved;
            processor.getStateInformation (saved);

            const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            expect (xml != nullptr);

            if (xml != nullptr)
            {
                auto savedStop = false, savedTime = false;

                for (auto* child : xml->getChildIterator())
                {
                    savedStop = savedStop || child->getStringAttribute ("id") == global::tapeStop;
                    savedTime = savedTime || child->getStringAttribute ("id") == global::tapeStopTime;
                }

                expect (! savedStop);
                expect (savedTime);
            }

            AstralayProcessor other;
            tapeParameter (other, global::tapeStop).setValueNotifyingHost (1.0f);
            other.setStateInformation (saved.getData(), (int) saved.getSize());

            expectEquals (tapeParameter (other, global::tapeStop).getValue(), 0.0f);

            auto& restoredTime = tapeParameter (other, global::tapeStopTime);
            expectWithinAbsoluteError (restoredTime.convertFrom0to1 (restoredTime.getValue()), 1200.0f, 0.5f);
        }

        beginTest ("Presets neither hold the tape stop nor change it");
        {
            AstralayProcessor processor;
            auto& stop = tapeParameter (processor, global::tapeStop);
            stop.setValueNotifyingHost (1.0f);

            const auto xml = state::Presets::toXml (processor, "Test", processor.getMacros());
            auto held = false, holdsTime = false;

            for (auto* child : xml->getChildIterator())
            {
                held = held || child->getStringAttribute ("id") == global::tapeStop;
                holdsTime = holdsTime || child->getStringAttribute ("id") == global::tapeStopTime;
            }

            expect (! held);
            expect (holdsTime);

            processor.loadFactoryPreset (1);
            expect (stop.getValue() >= 0.5f);

            processor.getHistory().undo();
            expect (stop.getValue() >= 0.5f);

            stop.setValueNotifyingHost (0.0f);
            processor.getHistory().redo();
            expectEquals (stop.getValue(), 0.0f);
        }

        beginTest ("A MIDI controller stops the tape in its upper half and releases a latch");
        {
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getNonexistentChildFile ("AstralayTapeSuite", "", false).getChildFile ("MIDI Mappings");

            {
                AstralayProcessor processor (folder);
                auto& midi = processor.getMidiMappings();
                auto& stop = tapeParameter (processor, global::tapeStop);

                expect (midi.eligible (global::tapeStop));
                midi.clear();
                midi.bind (global::tapeStop, { 1, 20 });

                // Playing it is not a change to the sound.
                expect (! midi.process (juce::MidiMessage::controllerEvent (1, 20, 127)));
                expect (stop.getValue() >= 0.5f);

                midi.process (juce::MidiMessage::controllerEvent (1, 20, 63));
                expectEquals (stop.getValue(), 0.0f);

                midi.process (juce::MidiMessage::controllerEvent (1, 20, 64));
                expect (stop.getValue() >= 0.5f);

                // Latched from the keyboard, it is let go from the controller.
                midi.process (juce::MidiMessage::controllerEvent (1, 20, 0));
                editAsUser (stop, 1.0f);
                expect (stop.getValue() >= 0.5f);
                midi.process (juce::MidiMessage::controllerEvent (1, 20, 10));
                expectEquals (stop.getValue(), 0.0f);
            }

            folder.getParentDirectory().deleteRecursively();
        }
    }
};

static TapeStopTests tapeStopTests;
