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

        beginTest ("A pitch sweep moves a short frozen loop's pitch the way it is going, and keeps its level");
        {
            // The strongest frequency in a stretch of the output, to the nearest 2 Hz.
            const auto strongest = [] (const std::vector<float>& signal, int from, int to)
            {
                auto best = 0.0, bestPower = -1.0;

                for (auto frequency = 100.0; frequency < 3000.0; frequency += 2.0)
                {
                    auto re = 0.0, im = 0.0;

                    for (int i = from; i < to; ++i)
                    {
                        const auto window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (i - from) / (to - from));
                        const auto phase = juce::MathConstants<double>::twoPi * frequency * i / testSampleRate;
                        re += signal[(size_t) i] * window * std::cos (phase);
                        im += signal[(size_t) i] * window * std::sin (phase);
                    }

                    if (re * re + im * im > bestPower)
                    {
                        bestPower = re * re + im * im;
                        best = frequency;
                    }
                }

                return best;
            };

            for (const bool up : { false, true })
            {
                auto global = wetOnly();
                global.glitch.threshold = 1.0f;
                global.glitch.chunkSamples = 6000;
                global.glitch.lengthChunks = { 1.0f, 1.0f };

                // The range lies well to one side of the loop's pitch, so the sweep heads that way.
                auto tap = singleTap (672.0f, 0.4f);
                tap.glitch.pitch = up ? RandomRange { 40.0f, 41.0f } : RandomRange { -41.0f, -40.0f };
                tap.glitch.pitchSpeed = { 2.0f, 2.0f };

                Engine engine;
                setUp (engine, global, tap);

                // The sine runs on past the freeze, so the loop is full of it when it closes.
                const auto input = sineBurst (48000, 9600, 440.0f);
                std::vector<float> left (input), right (input.size(), 0.0f);

                for (size_t start = 0; start < input.size(); start += blockSize)
                {
                    if (start == 4864)
                    {
                        global.freeze = true;
                        engine.setGlobalSettings (global);
                    }

                    // One glitch, at the chunk boundary half a second in.
                    if (start == 23808 || start == 24064)
                    {
                        tap.glitch.probability[(size_t) GlitchType::pitch] = start == 23808 ? 1.0f : 0.0f;
                        engine.setTapSettings (0, tap);
                    }

                    const auto n = (int) std::min ((size_t) blockSize, input.size() - start);
                    engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
                }

                // The sweep runs for about nine trips round the loop, but the shifter's read heads
                // trail behind, so the audio moves about half as far as nine steps.
                const auto semitones = 12.0 * std::log2 (strongest (left, 36000, 48000) / strongest (left, 14400, 24000));
                const auto message = "Moved " + juce::String (semitones, 1) + " semitones";

                if (up)
                    expect (semitones > 6.0 && semitones < 14.0, message);
                else
                    expect (semitones < -4.0 && semitones > -12.0, message);

                const auto level = rms (left, 36000, 12000) / rms (left, 14400, 9600);
                expect (level > 0.6f && level < 1.5f, "Level changed by a factor of " + juce::String (level));
            }
        }

        beginTest ("Formant shifting in a short frozen loop doesn't build up");
        {
            // Each formant shifter alone, then the cepstral one with pitch sweeps running through it.
            const std::vector<std::vector<GlitchType>> cases { { GlitchType::cepstralFormant }, { GlitchType::lpcFormant },
                                                               { GlitchType::pitch, GlitchType::cepstralFormant } };

            for (const auto& types : cases)
            {
                auto global = wetOnly();
                global.glitch.threshold = 1.0f;
                global.glitch.chunkSamples = 6000;
                global.reproducible = true;
                global.seed = 3;

                auto tap = singleTap (336.0f, 0.4f);
                tap.glitch.lpcShift = { -8.0f, 8.0f };
                tap.glitch.cepstralShift = { -8.0f, 8.0f };
                tap.glitch.pitch = { -8.0f, 8.0f };
                tap.glitch.pitchSpeed = { 0.5f, 2.5f };

                Engine engine;
                setUp (engine, global, tap);

                TransportInfo transport;
                transport.playing = true;
                engine.setTransport (transport);

                // Noise that falls away towards the top, as most sound does. Moving the formants of
                // such a sound up raises every band a little, and down lowers every band.
                juce::Random random (11);
                std::vector<float> left (48000 * 10, 0.0f), right (left.size(), 0.0f);
                auto smoothed = 0.0f;

                for (size_t i = 0; i < 9600; ++i)
                {
                    smoothed += 0.05f * (random.nextFloat() - 0.5f - smoothed);
                    left[i] = 2.0f * smoothed;
                }

                for (size_t start = 0; start < left.size(); start += blockSize)
                {
                    if (start == 4864)
                    {
                        global.freeze = true;
                        engine.setGlobalSettings (global);
                    }

                    // The glitches start once the loop has closed.
                    if (start == 9728)
                    {
                        for (const auto type : types)
                            tap.glitch.probability[(size_t) type] = 1.0f;

                        engine.setTapSettings (0, tap);
                    }

                    const auto n = (int) std::min ((size_t) blockSize, left.size() - start);
                    engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
                }

                // About 80 glitches in ten seconds, one straight after another, each many trips round
                // the 7 ms loop. Unchecked, the cepstral shifter raised this loop by tens of decibels.
                // Some level is still lost to that much reshaping, and sweeps wear the loop down too.
                const auto level = rms (left, 48000 * 9, 48000) / rms (left, 9600, 4800);
                const auto floor = types.size() > 1 ? 0.05f : 0.25f;
                expect (level > floor && level < 1.5f, "Level changed by a factor of " + juce::String (level));
            }
        }

        beginTest ("A frozen loop keeps its treble through varispeed glitches and at awkward tap times");
        {
            // How much the signal moves from sample to sample: a measure weighted to the treble.
            const auto treble = [] (const std::vector<float>& signal, int from, int length)
            {
                auto sum = 0.0;

                for (int i = from + 1; i < from + length; ++i)
                    sum += std::pow ((double) signal[(size_t) i] - signal[(size_t) i - 1], 2.0);

                return (float) std::sqrt (sum / length);
            };

            // Varispeed gliding to whatever delay it picks, then a tap time between samples with
            // no glitches at all.
            for (const bool varispeed : { true, false })
            {
                auto global = wetOnly();
                global.glideSeconds = 0.1f;
                global.glitch.threshold = 1.0f;
                global.glitch.chunkSamples = 4800;
                global.glitch.lengthChunks = { 2.0f, 6.0f };
                global.reproducible = true;
                global.seed = 3;

                auto tap = singleTap (varispeed ? 336.0f : 336.37f, 0.4f);
                tap.glitch.varispeed = true;
                tap.glitch.pitch = { 0.0f, 12.0f };

                Engine engine;
                setUp (engine, global, tap);

                TransportInfo transport;
                transport.playing = true;
                engine.setTransport (transport);

                juce::Random random (11);
                std::vector<float> left (48000 * 6, 0.0f), right (left.size(), 0.0f);

                for (size_t i = 0; i < 9600; ++i)
                    left[i] = 0.5f * (random.nextFloat() - 0.5f);

                for (size_t start = 0; start < left.size(); start += blockSize)
                {
                    if (start == 4864)
                    {
                        global.freeze = true;
                        engine.setGlobalSettings (global);
                    }

                    if (start == 24064 && varispeed)
                    {
                        tap.glitch.probability[(size_t) GlitchType::pitch] = 0.4f;
                        engine.setTapSettings (0, tap);
                    }

                    const auto n = (int) std::min ((size_t) blockSize, left.size() - start);
                    engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
                }

                const auto level = juce::Decibels::gainToDecibels (rms (left, 252000, 12000) / rms (left, 12000, 12000));
                const auto top = juce::Decibels::gainToDecibels (treble (left, 252000, 12000) / treble (left, 12000, 12000));
                const auto message = "Level " + juce::String (level, 1) + " dB, treble " + juce::String (top, 1) + " dB";

                // Settled on a whole number of samples, the loop is read back untouched. Varispeed
                // still costs some of the very top: speeding white noise up pushes part of it past
                // the highest frequency the sample rate can hold. With the old 4-point interpolation
                // these came out at -10 dB and -26 dB.
                expect (level > (varispeed ? -5.0f : -0.5f) && level < 1.0f, message);
                expect (top > (varispeed ? -10.0f : -0.5f) && top < 1.0f, message);
            }
        }

        beginTest ("The output clip holds peaks at its ceiling and leaves quieter audio alone");
        {
            auto global = wetOnly();
            global.mix = 0.0f;
            global.outputGain = 20.0f;

            const auto input = sineBurst (1000, 1000, 440.0f);

            for (auto ceiling : { 0.0f, 1.0f, 7.9f })
            {
                Engine engine;
                global.clipCeiling = ceiling;
                setUp (engine, global, singleTap (100.0f, 0.5f));

                const auto [left, right] = run (engine, input);

                for (size_t i = 0; i < input.size(); ++i)
                {
                    const auto unclipped = input[i] * 20.0f;
                    const auto expected = ceiling > 0.0f ? juce::jlimit (-ceiling, ceiling, unclipped) : unclipped;

                    expectWithinAbsoluteError (left[i], expected, 1.0e-4f);
                    expectWithinAbsoluteError (right[i], expected, 1.0e-4f);
                }
            }
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
