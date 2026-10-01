#include "PluginProcessor.h"

#if ASTRALAY_DIAGNOSTICS

class DiagnosticsTests final : public juce::UnitTest
{
public:
    DiagnosticsTests() : juce::UnitTest ("Diagnostics", "Astralay") {}

    void runTest() override
    {
        using namespace astralay::dsp;
        using diagnostics::Event;

        constexpr double sampleRate = 48000.0;

        // A short frozen tap that pitch shifts on every chunk.
        const auto runEngine = [sampleRate] (diagnostics::Sink& sink, bool varispeed = false)
        {
            Engine engine;
            engine.setDiagnostics (&sink);
            engine.prepare (sampleRate, 256, 2.0);

            GlobalSettings global;
            global.mix = 1.0f;
            global.freeze = true;
            global.clipCeiling = 1.0f;
            global.glitch.threshold = 1.0f;
            global.glitch.chunkSamples = 2400;
            engine.setGlobalSettings (global);

            TapSettings tap;
            tap.enabled = true;
            tap.delaySamples = 240.0f;
            tap.glitch.probability[(size_t) GlitchType::pitch] = 1.0f;
            tap.glitch.pitch = { -8.0f, 5.0f };
            tap.glitch.pitchSpeed = { 7.0f, 3.0f };
            tap.glitch.varispeed = varispeed;
            engine.setTapSettings (2, tap);

            std::vector<float> left (48000, 0.25f), right (48000, 0.0f);

            for (size_t start = 0; start < left.size(); start += 256)
                engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start,
                                (int) std::min ((size_t) 256, left.size() - start));
        };

        beginTest ("The engine reports glitches with their picks and ranges, and levels per tap and output");
        {
            diagnostics::Sink sink;
            runEngine (sink);

            int glitches = 0, tapStatuses = 0, outputStatuses = 0;
            auto lowest = 0.0f, highest = 0.0f;

            sink.drain ([&] (const Event& e)
            {
                if (e.kind == Event::Kind::glitchStarted)
                {
                    ++glitches;
                    expectEquals (e.tap, 2);
                    expectEquals (e.type, (int) GlitchType::pitch);

                    // The range is reported in order even though it was set backwards.
                    expectEquals (e.values[1], 3.0f);
                    expectEquals (e.values[2], 7.0f);
                    expect (std::abs (e.values[0]) >= 3.0f && std::abs (e.values[0]) <= 7.0f);
                }
                else if (e.kind == Event::Kind::tapStatus)
                {
                    ++tapStatuses;
                    expectEquals (e.tap, 2);
                    expectWithinAbsoluteError (e.values[1], 240.0f, 0.01f);
                    lowest = std::min (lowest, e.values[7]);
                    highest = std::max (highest, e.values[7]);
                }
                else if (e.kind == Event::Kind::outputStatus)
                {
                    ++outputStatuses;
                    expectEquals (e.values[2], 1.0f);
                }
            });

            expect (glitches > 0);
            expectEquals (tapStatuses, 20);
            expectEquals (outputStatuses, 20);

            // The sweeps carried the loop's pitch to both ends of its range and no further.
            expect (lowest >= -8.01f && lowest < -6.0f, "Lowest " + juce::String (lowest));
            expect (highest <= 5.01f && highest > 3.0f, "Highest " + juce::String (highest));
        }

        beginTest ("A varispeed pitch glitch bends the tap's delay time and leaves the loop's pitch estimate alone");
        {
            diagnostics::Sink sink;
            runEngine (sink, true);

            int glitches = 0;
            auto shortest = 240.0f, longest = 240.0f;

            sink.drain ([&] (const Event& e)
            {
                if (e.kind == Event::Kind::glitchStarted)
                {
                    ++glitches;
                    expectEquals (e.type, numGlitchTypes);
                    expect (e.values[0] >= -8.0f && e.values[0] <= 5.0f);
                }
                else if (e.kind == Event::Kind::tapStatus)
                {
                    shortest = std::min (shortest, e.values[1]);
                    longest = std::max (longest, e.values[1]);
                    expectEquals (e.values[7], 0.0f);
                }
            });

            // Five semitones faster is a delay of 180 samples; eight slower is 381.
            expect (glitches > 0);
            expect (shortest < 235.0f || longest > 245.0f, "The delay never moved");
            expect (shortest >= 179.0f && longest <= 382.0f, "Delay went from " + juce::String (shortest) + " to " + juce::String (longest));
        }

        beginTest ("The log file lists every parameter, then changes, glitches and levels");
        {
            AstralayProcessor processor;
            const auto file = juce::File::createTempFile (".log");

            {
                astralay::state::DiagnosticLog log (processor, file);
                expect (log.getFile() == file);

                auto* feedback = processor.getState().getParameter (astralay::params::tapId (0, astralay::params::tap::feedback));
                feedback->setValueNotifyingHost (feedback->convertTo0to1 (65.0f));

                runEngine (log.getSink());
            }

            const auto text = file.loadFileAsString();
            file.deleteFile();

            expect (text.contains ("t01_feedback \"Tap 1 Feedback\" = 40%  (range 0% to 100%)"), "Initial value and range");
            expect (text.contains ("t01_feedback \"Tap 1 Feedback\" = 65%\n")
                    || text.contains ("t01_feedback \"Tap 1 Feedback\" = 65%\r"), "Changed value");
            expect (text.contains ("glitch  tap=03 type=pitch"));
            expect (text.contains ("(set 3.00 to 7.00)"));
            expect (text.contains ("tap     tap=03 freeze=1.00 delay=5.00ms"));
            expect (text.contains ("output  peak="));
        }
    }
};

static DiagnosticsTests diagnosticsTests;

#endif
