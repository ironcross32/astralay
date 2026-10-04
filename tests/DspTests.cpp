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

    /** How a frozen loop changed over a run of glitches: its level and its highest peak, both
        relative to the level before the glitches began, and how much its treble changed, measured
        by how far the signal moves from one sample to the next. offset is the constant offset the
        output ends up sitting on, also relative to the level before.
    */
    struct LoopChange
    {
        float level = 0.0f, peak = 0.0f, treble = 0.0f, offset = 0.0f;

        juce::String describe() const
        {
            return "level changed by a factor of " + juce::String (level) + ", treble by " + juce::String (treble)
                       + ", peak " + juce::String (peak) + " times the level before";
        }
    };

    /** Fills a tap's loop from source (a function of the sample index), freezes it, then lets the
        glitches in the tap's settings fire at every chunk for the given number of seconds.
    */
    template <typename Source>
    LoopChange runFrozenLoop (float delay, int seed, double seconds, TapGlitchSettings glitch, Source&& source)
    {
        auto global = wetOnly();
        global.glitch.threshold = 1.0f;
        global.glitch.chunkSamples = 6000;
        global.reproducible = true;
        global.seed = seed;

        // The glitches start once the loop has closed.
        auto tap = singleTap (delay, 0.4f);

        Engine engine;
        setUp (engine, global, tap);

        TransportInfo transport;
        transport.playing = true;
        engine.setTransport (transport);

        const auto fill = (size_t) (2.0f * delay) + 9600;
        const auto freezeAt = (fill - 2400) / blockSize * blockSize;
        const auto glitchAt = (fill + (size_t) (2.0f * delay) + 4800) / blockSize * blockSize;

        std::vector<float> left (glitchAt + (size_t) (seconds * testSampleRate), 0.0f), right (left.size(), 0.0f);

        for (size_t i = 0; i < fill; ++i)
            left[i] = source (i);

        for (size_t start = 0; start < left.size(); start += blockSize)
        {
            if (start == freezeAt)
            {
                global.freeze = true;
                engine.setGlobalSettings (global);
            }

            if (start == glitchAt)
            {
                tap.glitch = glitch;
                engine.setTapSettings (0, tap);
            }

            const auto n = (int) std::min ((size_t) blockSize, left.size() - start);
            engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
        }

        const auto reference = (int) juce::jmin (delay + 2400.0f, 24000.0f);
        const auto before = rms (left, (int) glitchAt - reference, reference);

        const auto movement = [&left] (int from, int length)
        {
            auto sum = 0.0;

            for (int i = from + 1; i < from + length; ++i)
                sum += std::pow ((double) left[(size_t) i] - left[(size_t) i - 1], 2.0);

            return (float) std::sqrt (sum / length);
        };

        LoopChange change;
        change.level = rms (left, (int) left.size() - 24000, 24000) / before;
        change.treble = movement ((int) left.size() - 24000, 24000) / movement ((int) glitchAt - reference, reference);

        for (size_t i = glitchAt; i < left.size(); ++i)
            change.peak = juce::jmax (change.peak, std::abs (left[i]) / before);

        auto sum = 0.0;

        for (size_t i = left.size() - 24000; i < left.size(); ++i)
            sum += left[i];

        change.offset = (float) std::abs (sum / 24000.0) / before;

        return change;
    }

    /** A tone with a third harmonic, as a function of the sample index. */
    float tone (size_t i)
    {
        const auto t = juce::MathConstants<double>::twoPi * (double) i / testSampleRate;
        return 0.3f * (float) std::sin (440.0 * t) + 0.15f * (float) std::sin (1320.0 * t);
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

            // The feedback filters don't touch the direct output, so the echo is a clean impulse. The
            // DC blocker leaves a slow tail after it, under a thousandth of its height.
            expectWithinAbsoluteError (left[100], centre, 1.0e-5f);
            expectWithinAbsoluteError (right[100], centre, 1.0e-5f);
            expectWithinAbsoluteError (left[99], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (left[101], 0.0f, 1.0e-3f);
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

            // Between repeats there is only the DC blocker's tail from the repeat before.
            expectWithinAbsoluteError (fourth / first, 1.0f, 0.02f);
            expectWithinAbsoluteError (rms (left, 12000 + 4800, 480), 0.0f, 1.0e-3f);
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
                // Some level is still lost to that much reshaping. Sweeps wear the loop down to little
                // but a constant offset, which doesn't reach the output, so with them there is no floor.
                const auto level = rms (left, 48000 * 9, 48000) / rms (left, 9600, 4800);
                const auto floor = types.size() > 1 ? 0.0f : 0.25f;
                expect (level > floor && level < 1.5f, "Level changed by a factor of " + juce::String (level));
            }
        }

        beginTest ("LPC formant shifting doesn't build up in a frozen loop that holds a constant offset");
        {
            // A short loop rarely holds a whole number of cycles, so it sits on an offset. Noise on
            // an offset stands in for that, in loops of 7 ms, 40 ms and 250 ms.
            for (const auto delay : { 336.0f, 1920.0f, 12000.0f })
            {
                for (const auto seed : { 3, 7 })
                {
                    TapGlitchSettings glitch;
                    glitch.probability[(size_t) GlitchType::lpcFormant] = 1.0f;

                    juce::Random random (11);
                    const auto change = runFrozenLoop (delay, seed, 20.0, glitch,
                                                       [&random] (size_t) { return 0.2f + 0.2f * (random.nextFloat() - 0.5f); });

                    // Twenty seconds of one glitch after another. With the first bin of each frame
                    // counted at twice its weight, these loops rose by 8 to 12 dB.
                    const auto message = "Loop of " + juce::String (delay) + " samples, seed " + juce::String (seed) + ": " + change.describe();

                    expect (change.level > 0.1f && change.level < 1.2f, message);
                    expect (change.peak < 4.0f, message);
                }
            }
        }

        beginTest ("LPC formant shifting doesn't empty a frozen loop of a tone");
        {
            // Downward shifts only. With the gain free to change inside a partial, each pass cost
            // up to 2 dB and these loops fell by 50 dB or more within five seconds.
            for (const auto delay : { 336.0f, 960.0f })
            {
                TapGlitchSettings glitch;
                glitch.probability[(size_t) GlitchType::lpcFormant] = 1.0f;
                glitch.lpcShift = { -5.0f, -0.5f };

                const auto change = runFrozenLoop (delay, 3, 20.0, glitch, tone);
                expect (change.level > 0.3f && change.level < 1.2f, "Loop of " + juce::String (delay) + " samples: " + change.describe());
            }
        }

        beginTest ("A formant glitch's fades neither add to a short frozen loop nor wear it down");
        {
            // No shift at all, so only the fades in and out can change the loop. Scaled up to keep
            // their level, they fed whichever frequencies the loop and its shifted copy shared.
            for (const auto type : { GlitchType::lpcFormant, GlitchType::cepstralFormant })
            {
                for (const auto delay : { 336.0f, 960.0f, 1500.0f })
                {
                    TapGlitchSettings glitch;
                    glitch.probability[(size_t) type] = 1.0f;
                    glitch.lpcShift = { 0.0f, 0.0f };
                    glitch.cepstralShift = { 0.0f, 0.0f };

                    const auto message = "Glitch " + juce::String ((int) type) + ", loop of " + juce::String (delay) + " samples";

                    const auto steady = runFrozenLoop (delay, 3, 30.0, glitch, tone);
                    expect (steady.level > 0.9f && steady.level < 1.05f, message + ", tone: " + steady.describe());

                    juce::Random random (11);
                    const auto noisy = runFrozenLoop (delay, 3, 30.0, glitch, [&random] (size_t) { return 0.5f * (random.nextFloat() - 0.5f); });
                    expect (noisy.level > 0.9f && noisy.level < 1.05f, message + ", noise: " + noisy.describe());
                }
            }
        }

        beginTest ("A formant glitch moves a frozen loop's formants to the value picked and no further");
        {
            for (const auto type : { GlitchType::lpcFormant, GlitchType::cepstralFormant })
            {
                const auto glitchName = "Glitch " + juce::String ((int) type);

                // Upward shifts only, one glitch after another for 30 seconds. Added on every pass,
                // these raised the treble of a loop of a tone by 13 to 39 dB and its peaks by 20 dB.
                for (const auto delay : { 336.0f, 960.0f })
                {
                    TapGlitchSettings glitch;
                    glitch.probability[(size_t) type] = 1.0f;
                    glitch.lpcShift = { 0.5f, 5.0f };
                    glitch.cepstralShift = { 0.5f, 5.0f };

                    const auto change = runFrozenLoop (delay, 3, 30.0, glitch, tone);
                    const auto message = glitchName + ", loop of " + juce::String (delay) + " samples: " + change.describe();

                    expect (change.level > 0.7f && change.level < 1.1f, message);
                    expect (change.treble < 2.0f, message);
                    expect (change.peak < 4.0f, message);
                }

                // The loop still ends up where it was sent: brighter sent up than sent down.
                const auto sentTo = [type] (float semitones)
                {
                    TapGlitchSettings glitch;
                    glitch.probability[(size_t) type] = 1.0f;
                    glitch.lpcShift = { semitones, semitones };
                    glitch.cepstralShift = { semitones, semitones };

                    juce::Random random (11);
                    auto smoothed = 0.0f;

                    return runFrozenLoop (960.0f, 3, 10.0, glitch, [&] (size_t)
                    {
                        smoothed += 0.05f * (random.nextFloat() - 0.5f - smoothed);
                        return 2.0f * smoothed;
                    });
                };

                const auto up = sentTo (7.0f), down = sentTo (-7.0f);
                expect (up.treble > 1.15f * down.treble, glitchName + " up: " + up.describe() + "; down: " + down.describe());
            }
        }

        beginTest ("A glitch's read between samples keeps the top of the spectrum");
        {
            // A 15 kHz sine read half a sample off, where interpolation has most to do. Linear
            // interpolation takes 5 dB off it there.
            HistoryBuffer history;
            history.prepare (4800);

            const auto sine = [] (double position) { return std::sin (juce::MathConstants<double>::twoPi * 15000.0 * position / testSampleRate); };

            for (int i = 0; i < 2000; ++i)
                history.push ((float) sine ((double) i));

            auto worst = 0.0, worstLinear = 0.0;

            for (auto samplesAgo = 20.5f; samplesAgo < 400.0f; samplesAgo += 1.0f)
            {
                const auto expected = sine (1999.0 - (double) samplesAgo);
                worst = juce::jmax (worst, std::abs ((double) history.readAudio (samplesAgo) - expected));
                worstLinear = juce::jmax (worstLinear, std::abs ((double) history.read (samplesAgo) - expected));
            }

            expect (worst < 0.01, "Off by " + juce::String (worst) + "; linear is off by " + juce::String (worstLinear));

            // A whole number of samples back is the stored sample, and so is a read too close to
            // the newest sample for the kernel to fit.
            expectEquals (history.readAudio (100.0f), history.back (100));
            expectEquals (history.readAudio (3.0f), history.back (3));
        }

        beginTest ("Pitch sweeps and frequency modulation don't dull a frozen loop");
        {
            // White noise in loops of 40 ms and 250 ms, swept for 30 seconds. Sweeps wear a loop
            // down whatever is done, but read with linear interpolation these lost 54 and 20 dB of
            // level and 80 and 65 dB of treble.
            for (const auto delay : { 1920.0f, 12000.0f })
            {
                TapGlitchSettings glitch;
                glitch.probability[(size_t) GlitchType::pitch] = 1.0f;

                juce::Random random (11);
                const auto change = runFrozenLoop (delay, 3, 30.0, glitch, [&random] (size_t) { return 0.5f * (random.nextFloat() - 0.5f); });
                const auto message = "Pitch, loop of " + juce::String (delay) + " samples: " + change.describe();

                expect (change.level > (delay < 5000.0f ? 0.02f : 0.2f) && change.level < 1.1f, message);
                expect (change.treble > (delay < 5000.0f ? 0.002f : 0.01f), message);
            }

            // A tone in a 250 ms loop under frequency modulation, which adds treble of its own.
            // With linear interpolation the loop ended up duller than it began all the same.
            TapGlitchSettings glitch;
            glitch.probability[(size_t) GlitchType::frequencyModulation] = 1.0f;

            const auto change = runFrozenLoop (12000.0f, 7, 30.0, glitch, tone);
            const auto message = "Frequency modulation: " + change.describe();

            expect (change.level > 0.4f && change.level < 1.1f, message);
            expect (change.treble > 0.7f, message);
        }

        beginTest ("With host sync on, a glitch that always fires runs without gaps at any tempo");
        {
            // Chunks of a sixteenth note are rarely a whole number of samples. At the tempos where
            // they round up, a glitch counted in rounded chunks outlasted the beat it should have
            // ended on by a fraction of a sample, and so couldn't fire again there.
            for (const auto bpm : { 97.0, 130.0, 133.0, 140.0 })
            {
                for (const auto length : { 1.0f, 3.0f })
                {
                    const auto samplesPerQuarter = testSampleRate * 60.0 / bpm;

                    TransportInfo transport;
                    transport.playing = true;
                    transport.hasPosition = true;
                    transport.synced = true;
                    transport.chunkQuarters = 0.25;
                    transport.samplesPerQuarter = samplesPerQuarter;

                    auto global = wetOnly();
                    global.glitch.threshold = 1.0f;
                    global.glitch.chunkSamples = (int) std::llround (transport.chunkQuarters * samplesPerQuarter);
                    global.glitch.lengthChunks = { length, length };

                    auto tap = singleTap (4800.0f, 0.4f);
                    tap.glitch.probability[(size_t) GlitchType::ringModulation] = 1.0f;

                    Engine engine;
                    setUp (engine, global, tap);

                    const auto input = sineBurst (48000 * 20, 48000 * 20, 440.0f);
                    std::vector<float> left (input), right (input.size(), 0.0f);
                    auto blocks = 0, running = 0;

                    for (size_t start = 0; start < input.size(); start += 64)
                    {
                        transport.ppq = (double) start / samplesPerQuarter;
                        engine.setTransport (transport);
                        engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, 64);

                        ++blocks;
                        running += engine.getTap (0).getGlitches().isActive (GlitchType::ringModulation) ? 1 : 0;
                    }

                    const auto share = (float) running / (float) blocks;
                    expect (share > 0.99f, juce::String (bpm) + " BPM, " + juce::String (length) + " chunks long: running "
                                               + juce::String (100.0f * share, 1) + "% of the time");
                }
            }
        }

        beginTest ("An input sample that isn't a number is treated as silence and leaves nothing behind");
        {
            const auto nan = std::numeric_limits<float>::quiet_NaN();
            const auto infinity = std::numeric_limits<float>::infinity();

            for (const auto bad : { nan, infinity, -infinity })
            {
                for (const auto feedback : { 0.0f, 0.5f })
                {
                    auto input = sineBurst (48000, 48000, 440.0f);

                    Engine clean;
                    setUp (clean, wetOnly(), singleTap (100.0f, feedback));
                    const auto expected = run (clean, input).first;

                    input[1000] = bad;

                    Engine engine;
                    setUp (engine, wetOnly(), singleTap (100.0f, feedback));
                    const auto [left, right] = run (engine, input);

                    auto notFinite = 0;
                    auto difference = 0.0;

                    for (size_t i = 0; i < left.size(); ++i)
                    {
                        notFinite += (isNonFinite (left[i]) ? 1 : 0) + (isNonFinite (right[i]) ? 1 : 0);

                        if (i >= 24000)
                            difference = juce::jmax (difference, (double) std::abs (left[i] - expected[i]));
                    }

                    // Without the guard every output sample from then on was not a number, even
                    // with no feedback, since such a sample times zero is still not a number.
                    const auto message = "Feedback " + juce::String (feedback) + ": " + juce::String (notFinite)
                                             + " output samples not finite, later output off by " + juce::String (difference);

                    expectEquals (notFinite, 0, message);
                    expect (difference < 1.0e-3, message);
                }
            }

            // A tap handed such a sample directly keeps it out of its delay line and its filters.
            Tap tap;
            tap.prepare (testSampleRate, 4800);
            tap.setSettings (singleTap (100.0f, 0.5f), 0.0f, {});
            tap.reset();

            auto notFinite = 0;
            auto last = 0.0f;

            for (int i = 0; i < 4800; ++i)
            {
                auto left = 0.0f, right = 0.0f;
                tap.process (i == 50 ? nan : (i == 60 ? infinity : 0.25f), 0.0f, left, right);

                notFinite += (isNonFinite (left) ? 1 : 0) + (isNonFinite (right) ? 1 : 0);
                last = left;
            }

            expectEquals (notFinite, 0);
            expect (last > 0.1f, "The tap is still passing audio");
        }

        beginTest ("A long tap glides to a new time smoothly");
        {
            // A 5 second tap moved by 10 ms over 2 seconds, and by 1 ms over 100 ms. Each sample's
            // share of the move is smaller than single precision can add to a delay that long: the
            // first stayed put and then jumped the whole way, the second overshot and snapped back.
            for (const auto& [move, glideSeconds] : { std::pair { 480.0f, 2.0f }, std::pair { 48.0f, 0.1f } })
            {
                auto global = wetOnly();
                global.glideSeconds = glideSeconds;

                auto tap = singleTap (240000.0f, 0.0f);

                Engine engine;
                engine.prepare (testSampleRate, blockSize, 10.0);
                engine.setGlobalSettings (global);
                engine.setTapSettings (0, tap);
                engine.reset();

                std::vector<float> left (48000 * 9), right (left.size(), 0.0f);

                for (size_t i = 0; i < left.size(); ++i)
                    left[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * (double) i / testSampleRate);

                for (size_t start = 0; start < left.size(); start += blockSize)
                {
                    if (start == (size_t) blockSize * 1100)
                    {
                        tap.delaySamples += move;
                        engine.setTapSettings (0, tap);
                    }

                    const auto n = (int) std::min ((size_t) blockSize, left.size() - start);
                    engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
                }

                // The 50 Hz sine comes back bent slightly by the glide but with no jump in it.
                auto biggest = 0.0f;

                for (size_t i = 264000; i + 1 < left.size(); ++i)
                    biggest = juce::jmax (biggest, std::abs (left[i + 1] - left[i]));

                const auto steady = 0.5f * juce::MathConstants<float>::twoPi * 50.0f / (float) testSampleRate
                                        * std::cos (juce::MathConstants<float>::pi * 0.25f);

                expect (biggest < 1.01f * steady, "Moved " + juce::String (move) + " samples over " + juce::String (glideSeconds)
                                                      + " s: biggest step " + juce::String (biggest / steady) + " times a steady sine's");
            }
        }

        beginTest ("Changing the glide time during a glide doesn't make the tap jump");
        {
            // A tap gliding from 100 ms to 500 ms over 2 seconds. The glide time is changed once
            // part of the way there, then on every block as a macro modulating it would. Either
            // used to send the tap straight to its new time.
            for (const auto everyBlock : { false, true })
            {
                auto global = wetOnly();
                global.glideSeconds = 2.0f;

                auto tap = singleTap (4800.0f, 0.0f);

                Engine engine;
                setUp (engine, global, tap);

                const auto input = sineBurst (48000 * 4, 48000 * 4, 1000.0f);
                std::vector<float> left (input), right (input.size(), 0.0f);
                auto block = 0;

                for (size_t start = 0; start < input.size(); start += blockSize, ++block)
                {
                    if (block == 100)
                        tap.delaySamples = 24000.0f;

                    if (everyBlock ? block >= 200 : block == 200)
                        global.glideSeconds = block % 2 == 0 ? 1.9f : 2.0f;

                    engine.setGlobalSettings (global);
                    engine.setTapSettings (0, tap);

                    const auto n = (int) std::min ((size_t) blockSize, input.size() - start);
                    engine.process (left.data() + start, nullptr, left.data() + start, right.data() + start, n);
                }

                // The tap's time is growing, so the sine comes back slowed and steps no further from
                // one sample to the next than it does at full speed, give or take the interpolation.
                // The jump was a step 5.6 times that.
                const auto centre = std::cos (juce::MathConstants<float>::pi * 0.25f);
                const auto steady = 0.5f * centre * juce::MathConstants<float>::twoPi * 1000.0f / (float) testSampleRate;
                auto biggest = 0.0f;

                for (size_t i = 10000; i + 1 < left.size(); ++i)
                    biggest = juce::jmax (biggest, std::abs (left[i + 1] - left[i]));

                // And it still arrives: the last quarter second is the input from 500 ms before.
                auto furthest = 0.0f;

                for (size_t i = left.size() - 12000; i < left.size(); ++i)
                    furthest = juce::jmax (furthest, std::abs (left[i] - centre * input[i - 24000]));

                const auto message = juce::String (everyBlock ? "Changed every block" : "Changed once") + ": biggest step "
                                         + juce::String (biggest / steady) + " times a steady sine's, " + juce::String (furthest) + " from the tap's new time";

                expect (biggest < 1.05f * steady, message);
                expect (furthest < 0.01f, message);
            }
        }

        beginTest ("A frozen loop worn down to a constant offset doesn't send that offset to the output");
        {
            // Pitch sweeps wear a short loop down to its mean, which the level-keeping fades then
            // hold at about the loop's original level. Loops of 1 ms and 7 ms.
            for (const auto delay : { 48.0f, 336.0f })
            {
                for (const auto seed : { 3, 7 })
                {
                    TapGlitchSettings glitch;
                    glitch.probability[(size_t) GlitchType::pitch] = 1.0f;

                    juce::Random random (11);
                    const auto change = runFrozenLoop (delay, seed, 20.0, glitch,
                                                       [&random] (size_t) { return 0.5f * (random.nextFloat() - 0.5f); });

                    const auto message = "Loop of " + juce::String (delay) + " samples, seed " + juce::String (seed) + ": " + change.describe()
                                             + ", offset " + juce::String (change.offset) + " times the level before";

                    expect (change.offset < 0.01f, message);
                }
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
