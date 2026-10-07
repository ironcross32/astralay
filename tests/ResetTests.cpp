#include "dsp/DelayLine.h"
#include "dsp/GlitchChain.h"
#include "dsp/Tap.h"
#include <limits>

namespace
{
    using namespace astralay::dsp;

    float signal (int n) { return 0.25f * std::sin ((float) n * 0.071f); }
}

class ResetTests final : public juce::UnitTest
{
public:
    ResetTests() : juce::UnitTest ("History invalidation", "Astralay") {}

    void runTest() override
    {
        beginTest ("Delay clears match physically zeroed storage through interpolation and wraparound");
        for (const int capacity : { 9, 37, 1000 })
        {
            DelayLine reused, clean;
            reused.prepare (capacity);
            clean.prepare (capacity);
            for (int i = 0; i < 3 * capacity + 11; ++i)
                reused.push (i % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : 0.9f);
            reused.clear();
            reused.clear();
            for (int n = 0; n < 3 * capacity + 19; ++n)
            {
                if (n == 17 || n == capacity + 7)
                {
                    reused.clear();
                    clean.prepare (capacity); // Really writes zeros; independent of clear().
                }
                bool same = true;
                for (int d = 2; d <= capacity; ++d)
                    for (const double fraction : { 0.0, 0.125, 0.5, 0.875 })
                        same &= reused.read (d + fraction) == clean.read (d + fraction);
                expect (same, "capacity " + juce::String (capacity) + ", sample " + juce::String (n));
                reused.push (signal (n));
                clean.push (signal (n));
            }
        }

        beginTest ("History clears mask stale neighbours in integer, linear and sinc reads");
        for (const int capacity : { 8, 63, 500 })
        {
            HistoryBuffer reused, clean;
            reused.prepare (capacity);
            clean.prepare (capacity);
            for (int i = 0; i < 4 * capacity + 13; ++i)
                reused.push (i % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : -0.7f);
            reused.clear();
            reused.clear();
            for (int n = 0; n < 4 * capacity + 19; ++n)
            {
                if (n == 3 || n == capacity + 11)
                {
                    reused.clear();
                    clean.prepare (capacity);
                }
                bool same = true;
                for (int d = 0; d < reused.getCapacity(); ++d)
                {
                    same &= reused.back (d) == clean.back (d);
                    for (const float fraction : { 0.0f, 0.25f, 0.75f })
                    {
                        same &= reused.read (d + fraction) == clean.read (d + fraction);
                        same &= reused.readAudio (d + fraction) == clean.readAudio (d + fraction);
                    }
                }
                expect (same, "capacity " + juce::String (capacity) + ", sample " + juce::String (n));
                reused.push (signal (n));
                clean.push (signal (n));
            }
        }

        beginTest ("Both formant methods discard old overlap-add output on reset and restart");
        for (const double rate : { 48000.0, 192000.0 })
            for (const auto method : { FormantShifter::Method::lpc, FormantShifter::Method::cepstral })
            {
                FormantShifter reused, clean;
                reused.prepare (rate, method);
                clean.prepare (rate, method);
                const auto frame = reused.getFrameSize();
                // Both histories get identical audio, but only one accumulates pending output.
                reused.start (4.0f);
                for (int n = 0; n < 5 * frame + 37; ++n)
                {
                    reused.push (signal (n));
                    clean.push (signal (n));
                    reused.next();
                }
                for (const bool resetHistory : { false, true })
                {
                    if (resetHistory)
                    {
                        reused.reset();
                        reused.reset();
                        clean.prepare (rate, method);
                    }
                    reused.start (-3.0f, 336.0f);
                    clean.start (-3.0f, 336.0f);
                    bool same = true;
                    for (int n = 0; n < 6 * frame + 13; ++n)
                    {
                        const auto input = resetHistory && n < 2 * frame ? 0.0f : signal (n);
                        reused.push (input);
                        clean.push (input);
                        const auto actual = reused.next();
                        same &= actual == clean.next();
                        if (resetHistory && n < 2 * frame)
                            same &= actual == 0.0f;
                    }
                    expect (same, resetHistory ? "reset history and pending output" : "restart with retained history");
                }
            }

        beginTest ("Loop metadata invalidation matches physically cleared tracks without shifting their clock");
        {
            constexpr int capacity = 304; // 0.1 seconds at 48 kHz, one entry per 16 samples plus 4.
            GlitchChain chain;
            chain.prepare (48000.0, 0.1);
            chain.setLoop (336.0f, 1.0f);
            std::array<float, capacity> pitches {}, formants {};
            int write = 0, phase = 0;
            for (int n = 0; n < 18000; ++n)
            {
                if (n == 5107 || n == 5121 || n == 11111)
                {
                    chain.reset();
                    pitches.fill (0.0f);
                    formants.fill (0.0f);
                }
                bool same = true;
                for (int back = 1; back < capacity; ++back)
                {
                    const auto state = chain.getLoopState ((float) (back * 16));
                    const auto index = (size_t) ((write - back + capacity) % capacity);
                    same &= state.pitch == pitches[index] && state.formant == formants[index];
                }
                expect (same, "metadata sample " + juce::String (n));
                const auto pitch = (float) (n % 13 - 6), formant = (float) (n % 7 - 3);
                chain.process (0.0f, 1.0f, pitch, formant);
                if (++phase == 16)
                {
                    phase = 0;
                    write = (write + 1) % capacity;
                    pitches[(size_t) write] = pitch;
                    formants[(size_t) write] = formant;
                }
            }
        }

        beginTest ("Reset glitch audio matches a silent-history chain for every effect");
        for (int type = 0; type < numGlitchTypes; ++type)
        {
            GlitchChain reused, clean;
            reused.prepare (48000.0, 0.1);
            clean.prepare (48000.0, 0.1);
            GlitchGlobalSettings global;
            global.chunkSamples = 4096;
            global.lengthChunks = { 1.0f, 1.0f };
            for (auto* chain : { &reused, &clean })
            {
                chain->setSettings ({}, global);
                chain->setLoop (336.0f, 1.0f);
            }
            for (int n = 0; n < 16397; ++n)
            {
                reused.process (signal (n));
                clean.process (0.0f); // Match metadata phase while keeping storage physically zero.
            }
            reused.reset();
            clean.reset();
            for (auto* chain : { &reused, &clean })
            {
                chain->reseed (123);
                chain->startForTesting ((GlitchType) type);
            }
            bool same = true;
            for (int n = 0; n < 8192; ++n)
            {
                const auto input = n < 2048 ? 0.0f : signal (n);
                const auto actual = reused.process (input);
                same &= actual == clean.process (input);
                if (n < 2048) same &= actual == 0.0f;
            }
            expect (same, "glitch " + juce::String (type));
        }

        beginTest ("A disabled tap cannot replay old audio when re-enabled at a different delay");
        {
            Tap tap;
            tap.prepare (48000.0, 4800);
            TapSettings settings;
            settings.enabled = true;
            settings.delaySamples = 1500.5f;
            settings.feedback = 0.0f;
            GlitchGlobalSettings global;
            tap.setSettings (settings, 0.0f, global);
            for (int n = 0; n < 17000; ++n)
            {
                float left = 0.0f, right = 0.0f;
                tap.process (signal (n), 0.0f, left, right);
            }
            settings.enabled = false;
            tap.setSettings (settings, 0.0f, global);
            for (int n = 0; n < 1024; ++n)
            {
                float left = 0.0f, right = 0.0f;
                tap.process (0.0f, 0.0f, left, right);
            }
            tap.endBlock();
            expect (tap.isIdle());
            settings.enabled = true;
            settings.delaySamples = 4790.25f;
            tap.setSettings (settings, 0.0f, global);
            bool silent = true;
            for (int n = 0; n < 17000; ++n)
            {
                float left = 0.0f, right = 0.0f;
                tap.process (0.0f, 0.0f, left, right);
                silent &= left == 0.0f && right == 0.0f;
            }
            expect (silent);
        }
    }
};

static ResetTests resetTests;
