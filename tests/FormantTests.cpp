#include <juce_core/juce_core.h>
#include "dsp/FormantShifter.h"

namespace
{
    using namespace astralay::dsp;

    constexpr double rate = 48000.0;
    constexpr float fundamental = 150.0f;

    /** Harmonics of 150 Hz shaped by a single formant, like a sung vowel. */
    std::vector<float> vowel (int length, float formantHz)
    {
        std::vector<float> signal ((size_t) length, 0.0f);

        for (int h = 1; h * fundamental < 6000.0f; ++h)
        {
            const auto frequency = h * fundamental;
            const auto distance = (frequency - formantHz) / 250.0f;
            const auto amplitude = 0.05f * std::exp (-0.5f * distance * distance) + 0.001f;

            for (int i = 0; i < length; ++i)
                signal[(size_t) i] += amplitude * std::sin (juce::MathConstants<float>::twoPi * frequency * (float) i / (float) rate);
        }

        return signal;
    }

    /** Magnitude at one frequency over a segment (Goertzel). */
    double magnitudeAt (const std::vector<float>& signal, int start, int length, double frequency)
    {
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * frequency / rate);
        double s1 = 0.0, s2 = 0.0;

        for (int i = start; i < start + length; ++i)
        {
            const auto s0 = signal[(size_t) i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return std::sqrt (s1 * s1 + s2 * s2 - coefficient * s1 * s2) / length;
    }

    double bandEnergy (const std::vector<float>& signal, int start, int length, float low, float high)
    {
        double sum = 0.0;

        for (auto f = fundamental; f < high; f += fundamental)
            if (f >= low)
                sum += std::pow (magnitudeAt (signal, start, length, f), 2.0);

        return sum;
    }

    std::vector<float> shift (FormantShifter::Method method, const std::vector<float>& input, float semitones, int startAt)
    {
        FormantShifter shifter;
        shifter.prepare (rate, method);

        std::vector<float> output (input.size(), 0.0f);

        for (size_t i = 0; i < input.size(); ++i)
        {
            shifter.push (input[i]);

            if ((int) i == startAt)
                shifter.start (semitones);

            if ((int) i >= startAt)
                output[i] = shifter.next();
        }

        return output;
    }
}

class FormantTests final : public juce::UnitTest
{
public:
    FormantTests() : juce::UnitTest ("Formant shifting", "Astralay") {}

    void runTest() override
    {
        constexpr int length = 48000, startAt = 4000;

        for (auto method : { FormantShifter::Method::lpc, FormantShifter::Method::cepstral })
        {
            const juce::String methodName = method == FormantShifter::Method::lpc ? "LPC" : "Cepstral";

            beginTest (methodName + ": no shift reproduces the input one frame later");
            {
                juce::Random random (5);
                std::vector<float> input ((size_t) length);

                for (auto& s : input)
                    s = random.nextFloat() - 0.5f;

                FormantShifter probe;
                probe.prepare (rate, method);
                const auto latency = probe.getFrameSize() - 1;

                const auto output = shift (method, input, 0.0f, startAt);

                // Including the very first samples: the shifter warms up from history.
                for (int i = startAt; i < startAt + 20000; ++i)
                    expectWithinAbsoluteError (output[(size_t) i], input[(size_t) (i - latency)], 1.0e-3f);
            }

            beginTest (methodName + ": an octave up moves the formant and keeps the pitch");
            {
                const auto input = vowel (length, 800.0f);
                const auto output = shift (method, input, 12.0f, startAt);

                const auto segment = 24000, from = startAt + 8000;
                const auto lowIn = bandEnergy (input, from, segment, 600.0f, 1000.0f);
                const auto highIn = bandEnergy (input, from, segment, 1400.0f, 1900.0f);
                const auto lowOut = bandEnergy (output, from, segment, 600.0f, 1000.0f);
                const auto highOut = bandEnergy (output, from, segment, 1400.0f, 1900.0f);

                expect (lowIn > 10.0 * highIn);

                // The balance between the formant's old and new regions moves by at least 10 dB.
                // LPC's broader envelope moves it less sharply than the cepstral method.
                const auto balanceChange = (highOut / lowOut) / (highIn / lowIn);
                expect (balanceChange > 10.0, methodName + ": balance moved by " + juce::String (10.0 * std::log10 (balanceChange), 1) + " dB");

                // Energy stays on the 150 Hz harmonics rather than moving between them.
                const auto onHarmonic = magnitudeAt (output, from, segment, 1650.0);
                const auto between = magnitudeAt (output, from, segment, 1575.0);
                expect (onHarmonic > 10.0 * between, "Harmonic " + juce::String (onHarmonic) + ", between " + juce::String (between));
            }
        }
    }
};

static FormantTests formantTests;
