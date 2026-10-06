#include "dsp/Engine.h"

namespace
{
    using namespace astralay::dsp;
}

class FormantSchedulingTests final : public juce::UnitTest
{
public:
    FormantSchedulingTests() : juce::UnitTest ("Formant scheduling", "Astralay") {}
    void runTest() override
    {
        beginTest ("All 32 shifters share bounded work across startup, retrigger and normal frames");
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            auto shifters = std::make_unique<std::array<FormantShifter, 32>>();
            for (int s = 0; s < 32; ++s)
                (*shifters)[(size_t) s].prepare (rate, s % 2 == 0 ? FormantShifter::Method::lpc
                                                                 : FormantShifter::Method::cepstral, s);
            const auto hop = (*shifters)[0].getFrameSize() / 4;
            const auto spacing = hop / 128;
            for (int startPhase : { 0, 1, hop / 2 + 3, hop - 1 })
            {
                for (auto& shifter : *shifters)
                    shifter.start (0.0f, 336.0f);
                bool bounded = true, allReady = true;
                std::vector<int> work;
                for (int n = 0; n < 3 * hop; ++n)
                {
                    int frames = 0;
                    for (auto& shifter : *shifters)
                    {
                        const auto before = shifter.getFrameCount();
                        shifter.push (0.2f, startPhase + n);
                        shifter.next();
                        frames += (int) (shifter.getFrameCount() - before);
                        if (n == shifter.getMaxWarmupSamples() + 1)
                            allReady &= shifter.getStartupGain() > 0.0f;
                    }
                    bounded &= frames <= 1;
                    work.push_back (frames);
                }
                expect (bounded, "At most one frame per sample, including simultaneous retriggers");
                expect (allReady, "Every shifter becomes audible within the documented warm-up");
                for (const int block : { 32, 64, 128, 256 })
                {
                    int window = 0, peak = 0;
                    for (int n = 0; n < (int) work.size(); ++n)
                    {
                        window += work[(size_t) n];
                        if (n >= block) window -= work[(size_t) (n - block)];
                        peak = juce::jmax (peak, window);
                    }
                    expect (peak <= (block + spacing - 1) / spacing, "Bound holds for every callback alignment");
                }
            }
        }

        beginTest ("The engine keeps later-enabled taps on the shared formant schedule");
        {
            Engine engine;
            engine.prepare (48000.0, 64, 0.1);
            GlobalSettings g;
            g.reproducible = true;
            g.glitch.threshold = 1.0f;
            g.glitch.chunkSamples = 480;
            g.glitch.lengthChunks = { 1, 1 };
            engine.setGlobalSettings (g);
            TapSettings tap;
            tap.enabled = true;
            tap.delaySamples = 336;
            tap.glitch.probability[(int) GlitchType::lpcFormant] = 1;
            tap.glitch.probability[(int) GlitchType::cepstralFormant] = 1;
            for (int t = 0; t < 16; ++t) engine.setTapSettings (t, tap);
            engine.reset();
            bool bounded = true;
            for (int n = 0; n < 5000; ++n)
            {
                if (n == 19 || n == 1301)
                {
                    tap.enabled = n == 1301;
                    for (int t = 1; t < 16; t += 2) engine.setTapSettings (t, tap);
                }
                juce::uint64 before = 0, after = 0;
                for (int t = 0; t < 16; ++t) before += engine.getTap(t).getGlitches().getFormantFrameCount();
                float input = 0.1f, left = 0, right = 0;
                engine.process (&input, nullptr, &left, &right, 1);
                for (int t = 0; t < 16; ++t) after += engine.getTap(t).getGlitches().getFormantFrameCount();
                // A disable resets diagnostic counts; only positive deltas represent new work.
                if (after >= before) bounded &= after - before <= 1;
            }
            expect (bounded);
        }

        beginTest ("Seeded formant output without feedback is independent of host callback size");
        {
            const auto render = [] (int block)
            {
                Engine engine;
                engine.prepare (48000.0, block, 0.1);
                GlobalSettings g;
                g.reproducible = true;
                g.seed = 42;
                g.glideSeconds = 0;
                g.glitch.threshold = 1;
                g.glitch.chunkSamples = 480;
                g.glitch.lengthChunks = { 1, 3 };
                g.glitch.outputAndFeedback = true;
                engine.setGlobalSettings (g);
                for (int t : { 0, 15 })
                {
                    TapSettings tap;
                    tap.enabled = true;
                    tap.delaySamples = 336;
                    // Per-block filter snapToZero already introduces tiny feedback differences.
                    // Isolate the formant schedule while retaining exact sample comparisons.
                    tap.feedback = 0.0f;
                    tap.glitch.probability[(int) GlitchType::lpcFormant] = 1;
                    tap.glitch.probability[(int) GlitchType::cepstralFormant] = 1;
                    engine.setTapSettings (t, tap);
                }
                engine.reset();
                TransportInfo transport;
                transport.playing = true;
                engine.setTransport (transport);
                std::vector<float> left (16000), right (16000);
                for (int i = 0; i < (int) left.size(); ++i) left[(size_t) i] = 0.2f * std::sin (0.07f * i);
                for (int i = 0; i < (int) left.size(); i += block)
                    engine.process (left.data() + i, nullptr, left.data() + i, right.data() + i,
                                    juce::jmin (block, (int) left.size() - i));
                return left;
            };
            const auto reference = render (1);
            for (int block : { 32, 64, 257 }) expect (render (block) == reference);
        }

        beginTest ("Formant warm-up passes dry audio and a ten-millisecond glitch remains audible");
        for (int tap : { 0, 15 })
        {
            GlitchChain chain;
            chain.prepare (48000.0, 0.1, tap);
            GlitchGlobalSettings g;
            g.chunkSamples = 480;
            g.lengthChunks = { 1, 1 };
            TapGlitchSettings settings;
            settings.lpcShift = { 5, 5 };
            chain.setSettings (settings, g);
            for (int n = 0; n < 4096; ++n) chain.process (0.2f * std::sin (0.07f * n));
            chain.startForTesting (GlitchType::lpcFormant);
            bool dryAtStart = true;
            double difference = 0;
            for (int n = 0; n < 480; ++n)
            {
                const auto input = 0.2f * std::sin (0.07f * (n + 4096));
                const auto output = chain.process (input);
                if (n < 6) dryAtStart &= input == output;
                difference += std::abs (output - input);
            }
            expect (dryAtStart && difference > 0.01);
        }

        beginTest ("A minimum-length formant glitch still changes audio and has a bounded wet fade");
        for (int slot = 0; slot < 32; ++slot)
        {
            FormantShifter shifter;
            shifter.prepare (48000.0, FormantShifter::Method::cepstral, slot);
            for (int n = 0; n < 4096; ++n) shifter.push (0.2f * std::sin ((float) n * 0.1f));
            shifter.start (5.0f);
            bool audible = false, smooth = true;
            float previous = 0.0f;
            for (int n = 0; n < 480; ++n)
            {
                shifter.push (0.2f * std::sin ((float) (n + 4096) * 0.1f));
                const auto wet = shifter.next();
                const auto gain = shifter.getStartupGain();
                audible |= std::abs (wet * gain) > 0.001f;
                smooth &= gain >= previous && gain <= 1.0f && gain - previous < 0.0042f;
                previous = gain;
            }
            expect (audible && smooth, "slot " + juce::String (slot));
        }
    }
};
static FormantSchedulingTests formantSchedulingTests;
