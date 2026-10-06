#include <juce_core/juce_core.h>
#include "dsp/Engine.h"
#include "dsp/GlitchChain.h"
#include "dsp/Seed.h"
#include "PluginProcessor.h"
#include <limits>

namespace
{
    using namespace astralay::dsp;

    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    // Constant evaluation rejects signed overflow even when a runtime build happens to wrap it.
    // Fixed expected values cover negative seeds, multiplication overflow, the signed boundary,
    // and unsigned addition wrapping back through zero.
    constexpr auto minSeed = std::numeric_limits<std::int64_t>::min();
    constexpr auto maxSeed = std::numeric_limits<std::int64_t>::max();
    static_assert (seedForTap (minSeed, 0) == minSeed);
    static_assert (seedForTap (minSeed, 15) == minSeed + 15);
    static_assert (seedForTap (maxSeed, 0) == 9223372036853775805LL);
    static_assert (seedForTap (maxSeed, 15) == 9223372036853775820LL);
    static_assert (seedForTap (-1, 15) == -999988);
    static_assert (seedForTap (-8974618439281595224LL, 0) == 9223372036854775800LL);
    static_assert (seedForTap (-8974618439281595224LL, 15) == -9223372036854775801LL);
    static_assert (seedForTap (248753597573180584LL, 0) == -8);
    static_assert (seedForTap (248753597573180584LL, 15) == 7);

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

    // Observe actual scheduler decisions without depending on delay tails from an earlier pass.
    std::vector<int> schedule (Engine& engine, TransportInfo& transport)
    {
        std::vector<int> result;
        std::array<float, blockSize> silence {}, left {}, right {};
        constexpr int sizes[] { 1, 127, 256, 93 };
        for (int elapsed = 0, block = 0; elapsed < 48000; ++block)
        {
            const auto n = std::min (sizes[block % 4], 48000 - elapsed);
            engine.setTransport (transport);
            engine.process (silence.data(), nullptr, left.data(), right.data(), n);
            int mask = 0;
            for (int type = 0; type < numGlitchTypes; ++type)
                if (engine.getTap (0).getGlitches().isActive ((GlitchType) type))
                    mask |= 1 << type;
            result.push_back (mask);
            if (transport.samplePosition)
                *transport.samplePosition += n;
            elapsed += n;
        }
        return result;
    }

    struct SamplePlayHead final : juce::AudioPlayHead
    {
        PositionInfo position;
        juce::Optional<PositionInfo> getPosition() const override { return position; }
    };
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

        beginTest ("Reproducible transport restarts survive missing stopped callbacks and preparation");
        {
            auto run = glitchyRun();
            run.global.reproducible = true;
            run.global.seed = 42;
            Engine engine;
            engine.prepare (rate, blockSize, 2.0);
            engine.setGlobalSettings (run.global);
            engine.setTapSettings (0, run.tap);
            auto transport = run.transport;
            transport.samplePosition = -48000; // Include preroll and variable block sizes.
            const auto first = schedule (engine, transport);
            expect (schedule (engine, transport) != first, "Continuous playback must advance the sequence");

            transport.samplePosition = -48000;
            expect (schedule (engine, transport) == first, "Restart without a stopped callback");
            transport.samplePosition = 200000;
            expect (schedule (engine, transport) == first, "Forward seek");
            transport.samplePosition = -48000;
            expect (schedule (engine, transport) == first, "Loop wrap");

            transport.playing = false;
            engine.setTransport (transport);
            transport.playing = true;
            expect (schedule (engine, transport) == first, "Ordinary stop/start");

            transport.samplePosition.reset();
            engine.reset();
            expect (schedule (engine, transport) == first, "Reset without host position");
            engine.prepare (rate, blockSize, 2.0);
            // Settings can change after prepare, as they do in prepareToPlay.
            run.global.seed = 43;
            engine.setGlobalSettings (run.global);
            engine.setTapSettings (0, run.tap);
            const auto prepared = schedule (engine, transport);
            transport.playing = false;
            engine.setTransport (transport);
            transport.playing = true;
            expect (schedule (engine, transport) == prepared, "Preparation must use the current seed on play");
            expect (prepared != first, "Different seeds must produce different decisions");
        }

        beginTest ("Sample position tracking leaves continuous playback and unseeded playback alone");
        {
            auto run = glitchyRun();
            run.global.reproducible = true;
            run.global.seed = 42;
            Engine tracked, reference;
            for (auto* engine : { &tracked, &reference })
            {
                engine->prepare (rate, blockSize, 2.0);
                engine->setGlobalSettings (run.global);
                engine->setTapSettings (0, run.tap);
            }
            auto withPosition = run.transport, withoutPosition = run.transport;
            withPosition.samplePosition = 0;
            expect (schedule (tracked, withPosition) == schedule (reference, withoutPosition));
            expect (schedule (tracked, withPosition) == schedule (reference, withoutPosition));
            withPosition.samplePosition.reset();
            expect (schedule (tracked, withPosition) == schedule (reference, withoutPosition), "Position disappears");
            withPosition.samplePosition = 0;
            expect (schedule (tracked, withPosition) == schedule (reference, withoutPosition), "Position reappears");
            run.global.reproducible = false;
            tracked.setGlobalSettings (run.global);
            reference.setGlobalSettings (run.global);
            withPosition.samplePosition = 0;
            expect (schedule (tracked, withPosition) == schedule (reference, withoutPosition), "Toggle off ignores jumps");
        }

        beginTest ("The processor detects a host restart without stopped callbacks or PPQ");
        {
            using namespace astralay::params;
            SamplePlayHead skippedStop, reportedStop;
            AstralayProcessor actual, reference;
            actual.setPlayHead (&skippedStop);
            reference.setPlayHead (&reportedStop);
            for (auto* host : { &skippedStop, &reportedStop })
            {
                host->position.setIsPlaying (true);
                host->position.setTimeInSamples (0);
            }
            for (auto* processor : { &actual, &reference })
            {
                const auto set = [&] (const juce::String& id, float value)
                {
                    auto* p = processor->getState().getParameter (id);
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
                };
                set (global::reproducible, 1.0f);
                set (global::seed, 42.0f);
                set (global::sync, 0.0f);
                set (global::threshold, 100.0f);
                set (global::placement, 1.0f);
                set (global::bufferSize, 50.0f);
                set (global::mix, 100.0f);
                set (tapId (0, tap::time), 25.0f);
                set (tapId (0, tap::reverseProb), 50.0f);
                set (tapId (0, tap::crushProb), 50.0f);
                processor->prepareToPlay (rate, blockSize);
            }
            juce::MidiBuffer midi;
            const auto render = [&] (AstralayProcessor& processor, SamplePlayHead& host)
            {
                std::vector<float> output;
                for (int start = 0; start < (int) input.size(); start += blockSize)
                {
                    const auto n = std::min (blockSize, (int) input.size() - start);
                    juce::AudioBuffer<float> buffer (2, n);
                    for (int channel = 0; channel < 2; ++channel)
                        buffer.copyFrom (channel, 0, input.data() + start, n);
                    host.position.setTimeInSamples (start);
                    processor.processBlock (buffer, midi);
                    output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
                }
                return output;
            };
            expect (render (actual, skippedStop) == render (reference, reportedStop));
            // Both processors retain identical audio tails. Only one receives a stopped callback.
            reportedStop.position.setIsPlaying (false);
            juce::AudioBuffer<float> empty (2, 0);
            reference.processBlock (empty, midi);
            reportedStop.position.setIsPlaying (true);
            expect (render (actual, skippedStop) == render (reference, reportedStop));
            actual.releaseResources();
            reference.releaseResources();
        }

        beginTest ("Defined seed wrapping preserves user-seeded sequences and keeps taps distinct");
        {
            for (const auto seed : { 0, 1, 42, 9999 })
                for (int tap = 0; tap < Engine::numTaps; ++tap)
                    expectEquals (seedForTap (seed, (std::uint64_t) tap), (std::int64_t) seed * 1000003 + tap);

            for (const auto seed : { minSeed, maxSeed, (std::int64_t) -8974618439281595224LL,
                                     (std::int64_t) 248753597573180584LL })
            {
                for (int tap = 1; tap < Engine::numTaps; ++tap)
                    expect (seedForTap (seed, (std::uint64_t) tap) != seedForTap (seed, (std::uint64_t) (tap - 1)));
            }
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
            settings.pitchSpeed = { 12.0f, 12.0f };

            const auto in = sine (48000, 440.0f);
            const auto out = runChain (GlitchType::pitch, in, 1000, 40000, settings);

            // Count over one second, well inside the glitch.
            const auto crossings = upwardCrossings (out, 4000, 28000);
            expectWithinAbsoluteError ((float) crossings / 0.5f, 880.0f, 60.0f);
        }

        beginTest ("A sweep is held to its range when one step would carry it past");
        {
            TapGlitchSettings settings;
            settings.pitch = { -5.0f, 5.0f };
            settings.pitchSpeed = { 12.0f, 12.0f };

            const auto in = sine (48000, 440.0f);
            const auto out = runChain (GlitchType::pitch, in, 1000, 40000, settings);

            // Five semitones either way: 330 Hz or 587 Hz, never the octave the speed asks for.
            const auto frequency = (float) upwardCrossings (out, 4000, 28000) / 0.5f;
            expect (std::abs (frequency - 587.0f) < 40.0f || std::abs (frequency - 330.0f) < 25.0f,
                    "Frequency " + juce::String (frequency));
        }

        beginTest ("Varispeed asks the tap for a different delay time and leaves the audio alone");
        {
            GlitchChain chain;
            chain.prepare (rate);

            TapGlitchSettings settings;
            settings.varispeed = true;
            settings.pitch = { 12.0f, 12.0f };

            GlitchGlobalSettings global;
            global.chunkSamples = 2000;
            global.lengthChunks = { 1.0f, 1.0f };
            chain.setSettings (settings, global);

            const auto in = sine (6000, 440.0f);
            expectEquals (chain.getDelayScale(), 1.0f);

            for (int i = 0; i < 6000; ++i)
            {
                if (i == 1000)
                    chain.startForTesting (GlitchType::pitch);

                expectEquals (chain.process (in[(size_t) i]), in[(size_t) i]);

                // An octave up is half the delay, for as long as the glitch lasts.
                if (i >= 1000 && i < 2999)
                    expectWithinAbsoluteError (chain.getDelayScale(), 0.5f, 1.0e-5f);
                else if (i < 1000 || i >= 3000)
                    expectEquals (chain.getDelayScale(), 1.0f);
            }
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
