#include <iostream>
#include <juce_core/juce_core.h>
#include "dsp/Engine.h"
#include "dsp/FormantShifter.h"
#include "PluginProcessor.h"

namespace
{
    using namespace astralay::dsp;

    /** Seconds of audio processed per second of CPU time (higher is better; 1 is real time). */
    double realTimeFactor (int enabledTaps, bool glitching)
    {
        constexpr double rate = 48000.0;
        constexpr int block = 256;
        constexpr int seconds = 10;

        Engine engine;
        engine.prepare (rate, block, 10.0);

        GlobalSettings global;
        global.glitch.threshold = glitching ? 1.0f : 0.0f;
        global.glitch.outputAndFeedback = true;
        global.glitch.chunkSamples = 6000;
        global.glitch.maxSimultaneous = 4;
        engine.setGlobalSettings (global);

        for (int t = 0; t < Engine::numTaps; ++t)
        {
            TapSettings tap;
            tap.enabled = t < enabledTaps;
            tap.delaySamples = (float) (4800 * (t + 1));
            tap.feedback = 0.6f;
            tap.glitch.probability.fill (glitching ? 0.5f : 0.0f);
            engine.setTapSettings (t, tap);
        }

        engine.reset();

        juce::Random random (1);
        std::vector<float> left (block), right (block);

        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int n = 0; n < seconds * (int) rate / block; ++n)
        {
            for (int i = 0; i < block; ++i)
                left[(size_t) i] = right[(size_t) i] = random.nextFloat() - 0.5f;

            engine.process (left.data(), right.data(), left.data(), right.data(), block);
        }

        const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        return seconds / elapsedSeconds;
    }
}

namespace
{
    /** Average milliseconds to switch the tap selector, with the editor on a (hidden) window so
        accessibility handlers exist as they would in a host.
    */
    double tapSwitchMilliseconds()
    {
        AstralayProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);

        juce::ComboBox* selector = nullptr;

        for (auto* c : juce::KeyboardFocusTraverser().getAllComponents (editor.get()))
            if (c->getTitle() == "Selected tap")
                selector = dynamic_cast<juce::ComboBox*> (c);

        if (selector == nullptr)
            return -1.0;

        constexpr int switches = 32;
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < switches; ++i)
            selector->setSelectedId (i % 16 + 1, juce::sendNotificationSync);

        return (juce::Time::getMillisecondCounterHiRes() - start) / switches;
    }
}

namespace
{
    /** Microseconds per 1024-point real forward transform. */
    template <typename Transform>
    double microsecondsPer (Transform&& transform)
    {
        std::vector<float> data (2048, 0.0f);
        constexpr int runs = 20000;

        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < runs; ++i)
        {
            data[0] = (float) i;
            transform (data.data());
        }

        return (juce::Time::getMillisecondCounterHiRes() - start) * 1000.0 / runs;
    }

    /** Milliseconds of CPU per second of audio with the shifter running continuously. */
    double formantMilliseconds (FormantShifter::Method method)
    {
        FormantShifter shifter;
        shifter.prepare (48000.0, method);
        juce::Random random (2);

        for (int i = 0; i < 4096; ++i)
            shifter.push (random.nextFloat() - 0.5f);

        shifter.start (5.0f);
        float sink = 0.0f;

        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < 48000; ++i)
        {
            shifter.push (random.nextFloat() - 0.5f);
            sink += shifter.next();
        }

        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
        return sink == 12345.0f ? 0.0 : elapsed;
    }
}

void runBenchmark()
{
    juce::dsp::FFT juceFft (10);
    RealFft pffft (10);

    std::cout << "JUCE FFT (1024 point, real): "
              << juce::String (microsecondsPer ([&] (float* d) { juceFft.performRealOnlyForwardTransform (d, true); }), 2) << " us" << std::endl;
    std::cout << "PFFFT (1024 point, real): "
              << juce::String (microsecondsPer ([&] (float* d) { pffft.forward (d, d); }), 2) << " us" << std::endl;
    std::cout << "LPC formant shifter: " << juce::String (formantMilliseconds (FormantShifter::Method::lpc), 2) << " ms per second of audio" << std::endl;
    std::cout << "Cepstral formant shifter: " << juce::String (formantMilliseconds (FormantShifter::Method::cepstral), 2) << " ms per second of audio" << std::endl;

    std::cout << "Tap switch: " << juce::String (tapSwitchMilliseconds(), 2) << " ms" << std::endl;

    for (auto [taps, glitching] : { std::pair { 1, false }, std::pair { 1, true },
                                    std::pair { 16, false }, std::pair { 16, true } })
    {
        std::cout << taps << (taps == 1 ? " tap, " : " taps, ") << (glitching ? "glitching" : "no glitches")
                  << ": " << juce::String (realTimeFactor (taps, glitching), 1) << "x real time" << std::endl;
    }
}
