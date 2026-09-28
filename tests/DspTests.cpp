#include <juce_core/juce_core.h>
#include "dsp/Engine.h"
#include "dsp/SoftClip.h"

namespace
{
    using namespace astralay::dsp;

    constexpr double testSampleRate = 48000.0;
    constexpr int blockSize = 256;

    /** Runs a mono signal through the engine in blocks and returns the stereo output. */
    std::pair<std::vector<float>, std::vector<float>> run (Engine& engine, const std::vector<float>& input)
    {
        std::vector<float> left (input), right (input.size(), 0.0f);

        for (size_t start = 0; start < input.size(); start += blockSize)
        {
            const auto n = (int) std::min ((size_t) blockSize, input.size() - start);
            engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
        }

        return { left, right };
    }

    float rms (const std::vector<float>& signal, int start, int length)
    {
        double sum = 0.0;

        for (int i = start; i < start + length; ++i)
            sum += (double) signal[(size_t) i] * signal[(size_t) i];

        return (float) std::sqrt (sum / length);
    }

    std::vector<float> sineBurst (int totalLength, int burstLength, float frequency)
    {
        std::vector<float> signal ((size_t) totalLength, 0.0f);

        for (int i = 0; i < burstLength; ++i)
            signal[(size_t) i] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * frequency * (float) i / (float) testSampleRate);

        return signal;
    }

    GlobalSettings wetOnly()
    {
        GlobalSettings g;
        g.glideSeconds = 0.0f;
        g.mix = 1.0f;
        g.outputGain = 1.0f;
        return g;
    }

    TapSettings singleTap (float delaySamples, float feedback)
    {
        TapSettings t;
        t.enabled = true;
        t.delaySamples = delaySamples;
        t.feedback = feedback;
        return t;
    }

    /** Prepares an engine with the given settings already in effect (no fades or ramps pending). */
    void setUp (Engine& engine, const GlobalSettings& global, const TapSettings& tap)
    {
        engine.prepare (testSampleRate, blockSize, 2.0);
        engine.setGlobalSettings (global);
        engine.setTapSettings (0, tap);
        engine.reset();
    }
}

class DspTests final : public juce::UnitTest
{
public:
    DspTests() : juce::UnitTest ("DSP", "Astralay") {}

    void runTest() override
    {
        beginTest ("An impulse comes back after exactly the tap's delay, panned to centre");
        {
            Engine engine;
            setUp (engine, wetOnly(), singleTap (100.0f, 0.0f));

            std::vector<float> input (400, 0.0f);
            input[0] = 1.0f;

            const auto [left, right] = run (engine, input);
            const auto centre = std::cos (juce::MathConstants<float>::pi * 0.25f);

            // The feedback filters don't touch the direct output, so the echo is a clean impulse.
            expectWithinAbsoluteError (left[100], centre, 1.0e-5f);
            expectWithinAbsoluteError (right[100], centre, 1.0e-5f);
            expectWithinAbsoluteError (left[99], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (left[101], 0.0f, 1.0e-6f);
        }

        beginTest ("Feedback scales each repeat");
        {
            Engine engine;
            setUp (engine, wetOnly(), singleTap (4800.0f, 0.5f));

            const auto [left, right] = run (engine, sineBurst (15000, 480, 1000.0f));
            const auto first = rms (left, 4800, 480);
            const auto second = rms (left, 9600, 480);

            expectWithinAbsoluteError (second / first, 0.5f, 0.02f);
        }

        beginTest ("Freeze holds repeats at a constant level and mutes new input");
        {
            Engine engine;
            auto global = wetOnly();
            setUp (engine, global, singleTap (4800.0f, 0.0f));

            auto input = sineBurst (30000, 480, 1000.0f);

            // New input during the freeze must not enter the loop.
            for (int i = 12000; i < 12480; ++i)
                input[(size_t) i] = 0.5f;

            std::vector<float> left (input), right (input.size(), 0.0f);

            for (size_t start = 0; start < input.size(); start += blockSize)
            {
                if (start == 1024)
                {
                    global.freeze = true;
                    engine.setGlobalSettings (global);
                }

                const auto n = (int) std::min ((size_t) blockSize, input.size() - start);
                engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
            }

            const auto first = rms (left, 4800, 480);
            const auto fourth = rms (left, 19200, 480);

            expectWithinAbsoluteError (fourth / first, 1.0f, 0.02f);
            expectWithinAbsoluteError (rms (left, 12000 + 4800, 480), 0.0f, 1.0e-4f);
        }

        beginTest ("Constant-power pan: hard left silences the right channel");
        {
            Engine engine;
            auto tap = singleTap (100.0f, 0.0f);
            tap.pan = -1.0f;
            setUp (engine, wetOnly(), tap);

            std::vector<float> input (200, 0.0f);
            input[0] = 1.0f;

            const auto [left, right] = run (engine, input);
            expectWithinAbsoluteError (left[100], 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (right[100], 0.0f, 1.0e-5f);
        }

        beginTest ("Fully dry mix passes the input unchanged");
        {
            Engine engine;
            auto global = wetOnly();
            global.mix = 0.0f;
            setUp (engine, global, singleTap (100.0f, 0.5f));

            const auto input = sineBurst (1000, 1000, 440.0f);
            const auto [left, right] = run (engine, input);

            for (size_t i = 0; i < input.size(); ++i)
            {
                expectWithinAbsoluteError (left[i], input[i], 1.0e-6f);
                expectWithinAbsoluteError (right[i], input[i], 1.0e-6f);
            }
        }

        beginTest ("With every tap off the wet signal is silent");
        {
            Engine engine;
            auto tap = singleTap (100.0f, 0.5f);
            tap.enabled = false;
            setUp (engine, wetOnly(), tap);

            const auto [left, right] = run (engine, sineBurst (2000, 2000, 440.0f));
            expectWithinAbsoluteError (rms (left, 0, 2000), 0.0f, 1.0e-7f);
            expectWithinAbsoluteError (rms (right, 0, 2000), 0.0f, 1.0e-7f);
        }

        beginTest ("Soft-clipper is transparent below the knee and never exceeds 1");
        {
            for (auto x : { -0.9f, -0.5f, 0.0f, 0.3f, 0.9f })
                expectEquals (softClip (x), x);

            for (auto x : { -100.0f, -2.0f, 1.0f, 1.5f, 100.0f })
                expect (std::abs (softClip (x)) <= 1.0f);

            expect (softClip (1.0f) > 0.9f);
        }
    }
};

static DspTests dspTests;
