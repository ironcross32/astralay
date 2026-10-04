#include "dsp/Tap.h"

namespace
{
    using namespace astralay::dsp;

    double level (const std::vector<float>& audio, double rate, double from, double seconds)
    {
        double sum = 0.0, squares = 0.0;
        const auto count = (int) (seconds * rate);
        for (int i = (int) (from * rate); i < (int) (from * rate) + count; ++i)
        {
            const auto x = (double) audio[(size_t) i];
            sum += x;
            squares += x * x;
        }
        return std::sqrt (juce::jmax (0.0, squares / count - std::pow (sum / count, 2.0)));
    }
}

class FreezeSustainTests final : public juce::UnitTest
{
public:
    FreezeSustainTests() : juce::UnitTest ("Freeze sustain lifecycle", "Astralay") {}

    void runTest() override
    {
        beginTest ("Sustain leaves ordinary delay and unchanging frozen loops untouched");
        {
            for (const bool frozen : { false, true })
            {
                Tap original, sustained;
                for (auto* tap : { &original, &sustained })
                {
                    tap->prepare (48000.0, 4800);
                    TapSettings settings;
                    settings.enabled = true;
                    settings.feedback = 0.4f;
                    settings.delaySamples = 1920;
                    tap->setSettings (settings, 0.0f, {});
                    tap->reset();
                }
                double difference = 0.0;
                for (int i = 0; i < 192000; ++i)
                {
                    const auto input = 0.1f * std::sin ((float) i * 0.05f);
                    const auto freeze = frozen && i >= 48000 ? 1.0f : 0.0f;
                    float a = 0.0f, b = 0.0f, unused = 0.0f;
                    original.process (input, freeze, a, unused);
                    sustained.process (input, freeze, b, unused, true);
                    difference = juce::jmax (difference, (double) std::abs (a - b));
                }
                expectEquals (difference, 0.0);
            }
        }

        beginTest ("Capture preserves audio before destructive processing, even for a ten-second loop");
        {
            constexpr int length = 480000;
            DelayLine line;
            line.prepare (length);
            GlitchChain chain;
            chain.prepare (48000.0, 10.0);
            FreezeSustain sustain;
            sustain.prepare (48000.0, length);
            for (int i = 0; i < length; ++i)
                line.push (0.1f * std::sin ((float) i * 0.01f));

            double squares = 0.0;
            for (int i = 0; i < 4 * length; ++i)
            {
                const auto saved = sustain.next (true, 1.0f, length, line, chain);
                sustain.observe (0.0f); // The working loop is erased while capture is still running.
                line.push (0.0f);
                if (i >= 3 * length)
                    squares += std::pow ((double) saved.amount * saved.audio, 2.0);
            }
            expect (std::sqrt (squares / length) > 0.03);
        }

        beginTest ("Silent and DC-only captures never create audio");
        {
            for (const auto dc : { 0.0f, 0.2f })
            {
                DelayLine line;
                line.prepare (480);
                GlitchChain chain;
                chain.prepare (48000.0, 0.01);
                FreezeSustain sustain;
                sustain.prepare (48000.0, 480);
                for (int i = 0; i < 480; ++i)
                    line.push (dc);
                float peak = 0.0f;
                for (int i = 0; i < 48000; ++i)
                {
                    const auto saved = sustain.next (true, 1.0f, 480, line, chain);
                    sustain.observe (0.0f);
                    line.push (0.0f);
                    peak = juce::jmax (peak, std::abs (saved.audio * saved.amount));
                }
                expectEquals (peak, 0.0f);
            }
        }

        beginTest ("Restored pitch and formant estimates travel with the replacement audio");
        {
            GlitchChain chain;
            chain.prepare (48000.0, 0.01);
            chain.setLoop (480.0f, 1.0f);
            for (int i = 0; i < 1000; ++i)
                chain.process (0.1f, 1.0f, 7.0f, -3.0f);
            const auto state = chain.getLoopState (480.0f);
            expectWithinAbsoluteError (state.pitch, 7.0f, 0.0001f);
            expectWithinAbsoluteError (state.formant, -3.0f, 0.0001f);
        }

        beginTest ("Sustain survives a glide, fades off, recaptures on refreeze and clears on tap disable");
        for (const auto rate : { 44100.0, 96000.0 })
        {
            Tap tap;
            tap.prepare (rate, (int) rate);
            TapSettings settings;
            settings.enabled = true;
            settings.delaySamples = (float) (rate * 0.04);
            settings.feedback = 0.0f;
            settings.glitch.bits = { 1.0f, 1.0f };
            GlitchGlobalSettings global;
            global.threshold = 1.0f;
            global.chunkSamples = 256;
            global.outputAndFeedback = true;
            tap.setSettings (settings, 0.1f, global);
            tap.reset();
            std::vector<float> audio ((size_t) (8 * rate));
            bool finite = true;
            for (int i = 0; i < (int) audio.size(); ++i)
            {
                const auto at = [i, rate] (double seconds) { return i == (int) (rate * seconds); };
                if (at (1.5) || at (6.5))
                {
                    settings.glitch.probability[(size_t) GlitchType::bitCrusher] = 1.0f;
                    tap.setSettings (settings, 0.1f, global);
                }
                if (at (2.0))
                {
                    settings.delaySamples = (float) (rate * 0.0733);
                    tap.setSettings (settings, 0.1f, global);
                }
                if (at (5.0))
                {
                    settings.glitch.probability.fill (0.0f);
                    tap.restartGlitches (3);
                    tap.setSettings (settings, 0.1f, global);
                }
                if (at (7.0) || at (7.1))
                {
                    settings.enabled = at (7.1);
                    tap.setSettings (settings, 0.1f, global);
                }
                const auto time = i / rate;
                const bool frozen = time >= 1.0 && (time < 5.0 || time >= 6.0);
                const bool wanted = frozen && (time < 3.0 || time >= 4.0);
                if (i % 256 == 0)
                    tap.onChunkBoundary (frozen ? 1.0f : 0.0f);
                const auto input = time < 1.0 || (time >= 5.0 && time < 6.0)
                    ? 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * time) : 0.0f;
                float left = 0.0f, right = 0.0f;
                if (! tap.isIdle())
                    tap.process (input, frozen ? 1.0f : 0.0f, left, right, wanted);
                audio[(size_t) i] = left;
                finite = finite && std::isfinite (left) && std::abs (left) < 0.3f;
                if (i % 256 == 255)
                    tap.endBlock();
            }
            expect (finite);
            expect (level (audio, rate, 2.5, 0.4) > 0.02, "Gliding sustain went silent");
            expect (level (audio, rate, 3.8, 0.1) < 0.0001, "Turning sustain off didn't release recovery");
            expect (level (audio, rate, 4.8, 0.1) < 0.0001, "Enabling on silence resurrected an old capture");
            expect (level (audio, rate, 6.8, 0.1) > 0.02, "Refreeze didn't capture new audio");
            expect (level (audio, rate, 7.8, 0.1) < 0.0001, "Disabling a tap didn't discard its recording");
        }
    }
};

static FreezeSustainTests freezeSustainTests;
