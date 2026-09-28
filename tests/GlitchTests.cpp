#include <juce_core/juce_core.h>
#include "dsp/Engine.h"
#include "dsp/GlitchChain.h"

namespace
{
    using namespace astralay::dsp;

    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    std::vector<float> sine (int length, float frequency, float amplitude = 0.5f)
    {
        std::vector<float> signal ((size_t) length);

        for (int i = 0; i < length; ++i)
            signal[(size_t) i] = amplitude * std::sin (juce::MathConstants<float>::twoPi * frequency * (float) i / (float) rate);

        return signal;
    }

    std::vector<float> noise (int length, juce::int64 seed)
    {
        juce::Random random (seed);
        std::vector<float> signal ((size_t) length);

        for (auto& s : signal)
            s = random.nextFloat() - 0.5f;

        return signal;
    }

    int upwardCrossings (const std::vector<float>& signal, int start, int end)
    {
        int count = 0;

        for (int i = start + 1; i < end; ++i)
            if (signal[(size_t) i - 1] < 0.0f && signal[(size_t) i] >= 0.0f)
                ++count;

        return count;
    }

    /** A chain with one glitch forced on for lengthSamples, fed input; returns the output. */
    std::vector<float> runChain (GlitchType type, const std::vector<float>& input, int startAt,
                                 int lengthSamples, TapGlitchSettings settings = {})
    {
        GlitchChain chain;
        chain.prepare (rate);

        GlitchGlobalSettings global;
        global.threshold = 1.0f;
        global.maxSimultaneous = 4;
        global.chunkSamples = lengthSamples;
        global.lengthChunks = { 1.0f, 1.0f };
        chain.setSettings (settings, global);

        std::vector<float> output (input.size());

        for (size_t i = 0; i < input.size(); ++i)
        {
            if ((int) i == startAt)
                chain.startForTesting (type);

            output[i] = chain.process (input[i]);
        }

        return output;
    }

    TapGlitchSettings allAtMaximum()
    {
        TapGlitchSettings s;
        s.probability.fill (1.0f);
        return s;
    }

    struct EngineRun
    {
        GlobalSettings global;
        TapSettings tap;
        TransportInfo transport;
    };

    std::vector<float> runEngine (const EngineRun& run, const std::vector<float>& input)
    {
        Engine engine;
        engine.prepare (rate, blockSize, 2.0);
        engine.setGlobalSettings (run.global);
        engine.setTapSettings (0, run.tap);
        engine.reset();

        std::vector<float> left (input), right (input.size());

        for (size_t start = 0; start < input.size(); start += blockSize)
        {
            engine.setTransport (run.transport);
            const auto n = (int) std::min ((size_t) blockSize, input.size() - start);
            engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
        }

        return left;
    }

    EngineRun glitchyRun()
    {
        EngineRun run;
        run.global.glideSeconds = 0.0f;
        run.global.mix = 1.0f;
        run.global.outputGain = 1.0f;
        run.global.glitch.threshold = 1.0f;
        run.global.glitch.outputAndFeedback = true;
        run.global.glitch.chunkSamples = 2400;
        run.global.glitch.maxSimultaneous = 2;
        run.global.glitch.lengthChunks = { 1.0f, 3.0f };

        run.tap.enabled = true;
        run.tap.delaySamples = 1200.0f;
        run.tap.feedback = 0.5f;
        run.tap.glitch.probability.fill (0.3f);

        run.transport.playing = true;
        return run;
    }
}

class GlitchTests final : public juce::UnitTest
{
public:
    GlitchTests() : juce::UnitTest ("Glitches", "Astralay") {}

    void runTest() override
    {
        const auto input = noise (48000, 7);

        beginTest ("A threshold of 0% stops all glitching");
        {
            auto quiet = glitchyRun();
            quiet.global.glitch.threshold = 0.0f;

            auto none = glitchyRun();
            none.tap.glitch.probability.fill (0.0f);

            expect (runEngine (quiet, input) == runEngine (none, input));
        }

        beginTest ("Feedback-path glitches leave the first repeat clean at 0% feedback");
        {
            auto feedbackOnly = glitchyRun();
            feedbackOnly.global.glitch.outputAndFeedback = false;
            feedbackOnly.tap.feedback = 0.0f;

            auto none = feedbackOnly;
            none.tap.glitch.probability.fill (0.0f);

            expect (runEngine (feedbackOnly, input) == runEngine (none, input));

            // With output placement the same settings are audible.
            auto heard = feedbackOnly;
            heard.global.glitch.outputAndFeedback = true;
            expect (runEngine (heard, input) != runEngine (none, input));
        }

        beginTest ("Reproducible randomness repeats exactly for a seed");
        {
            auto seeded = glitchyRun();
            seeded.global.reproducible = true;
            seeded.global.seed = 42;

            const auto first = runEngine (seeded, input);
            expect (first == runEngine (seeded, input));

            seeded.global.seed = 43;
            expect (first != runEngine (seeded, input));
        }

        beginTest ("No more glitches run at once than the maximum");
        {
            GlitchChain chain;
            chain.prepare (rate);

            GlitchGlobalSettings global;
            global.threshold = 1.0f;
            global.maxSimultaneous = 2;
            global.chunkSamples = 1000;
            global.lengthChunks = { 4.0f, 4.0f };
            chain.setSettings (allAtMaximum(), global);

            for (int i = 0; i < 20000; ++i)
            {
                if (i % 1000 == 0)
                    chain.onChunkBoundary();

                chain.process (input[(size_t) i]);
                expect (chain.getNumActive() <= 2);
            }
        }

        beginTest ("Reverse plays each segment backwards");
        {
            std::vector<float> ramp (6000);

            for (size_t i = 0; i < ramp.size(); ++i)
                ramp[i] = (float) i * 1.0e-4f;

            const auto out = runChain (GlitchType::reverse, ramp, 3000, 2000);

            // Past the fade-in, the output falls while the input rises.
            for (int i = 3400; i < 3800; ++i)
                expect (out[(size_t) i + 1] < out[(size_t) i]);
        }

        beginTest ("Stutter repeats one slice");
        {
            TapGlitchSettings settings;
            settings.stutterSlice = { 480.0f, 480.0f };

            const auto out = runChain (GlitchType::stutter, input, 5000, 10000, settings);

            // Away from the loop-point fades, each sample equals the one a slice later.
            for (int i = 6000; i < 8000; ++i)
                if (const auto position = (i - 5000) % 480; position > 60 && position < 420)
                    expectWithinAbsoluteError (out[(size_t) i], out[(size_t) i + 480], 1.0e-6f);
        }

        beginTest ("Pitch up an octave doubles the frequency");
        {
            TapGlitchSettings settings;
            settings.pitch = { 12.0f, 12.0f };

            const auto in = sine (48000, 440.0f);
            const auto out = runChain (GlitchType::pitch, in, 1000, 40000, settings);

            // Count over one second, well inside the glitch.
            const auto crossings = upwardCrossings (out, 4000, 28000);
            expectWithinAbsoluteError ((float) crossings / 0.5f, 880.0f, 60.0f);
        }

        beginTest ("Bit crusher quantises to the chosen bit depth");
        {
            TapGlitchSettings settings;
            settings.bits = { 2.0f, 2.0f };
            settings.rateReduction = { 1.0f, 1.0f };

            const auto out = runChain (GlitchType::bitCrusher, sine (10000, 300.0f, 0.9f), 1000, 8000, settings);

            // Two bits: steps of 0.5 between -1 and 1.
            for (int i = 2000; i < 8000; ++i)
                expectWithinAbsoluteError (out[(size_t) i] * 2.0f, std::round (out[(size_t) i] * 2.0f), 1.0e-5f);
        }

        beginTest ("Ring modulation, FM and granularize change the signal and stay finite");
        {
            const auto in = sine (20000, 220.0f);

            for (auto type : { GlitchType::ringModulation, GlitchType::frequencyModulation, GlitchType::granularize })
            {
                const auto out = runChain (type, in, 1000, 15000);
                double difference = 0.0;

                for (int i = 2000; i < 15000; ++i)
                {
                    expect (std::isfinite (out[(size_t) i]));
                    difference += std::abs (out[(size_t) i] - in[(size_t) i]);
                }

                expect (difference > 10.0, "Glitch " + juce::String ((int) type) + " made no difference");
            }
        }

        beginTest ("Everything at maximum with full feedback stays bounded");
        {
            auto wild = glitchyRun();
            wild.tap.feedback = 1.0f;
            wild.tap.glitch = allAtMaximum();
            wild.global.glitch.maxSimultaneous = 4;

            const auto out = runEngine (wild, noise (96000, 3));
            float peak = 0.0f;

            for (auto s : out)
            {
                expect (std::isfinite (s));
                peak = juce::jmax (peak, std::abs (s));
            }

            expect (peak < 4.0f, "Peak " + juce::String (peak));
        }
    }
};

static GlitchTests glitchTests;
