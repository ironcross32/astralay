#include <juce_core/juce_core.h>
#include "dsp/Smear.h"

namespace
{
    using namespace astralay::dsp;

    constexpr double rate = 48000.0;

    struct Stereo
    {
        std::vector<float> left, right;
    };

    Stereo run (float amount, float sizeSeconds, const std::vector<float>& input)
    {
        Smear smear;
        smear.prepare (rate, 0.5);
        smear.setParameters (amount, sizeSeconds);
        smear.reset();

        Stereo out { input, input };

        for (size_t i = 0; i < input.size(); ++i)
            smear.process (out.left[i], out.right[i]);

        return out;
    }

    std::vector<float> impulse (int length)
    {
        std::vector<float> signal ((size_t) length, 0.0f);
        signal[0] = 1.0f;
        return signal;
    }

    double energy (const std::vector<float>& signal)
    {
        double sum = 0.0;

        for (auto s : signal)
            sum += (double) s * s;

        return sum;
    }

    /** Energy-weighted mean time, in seconds. */
    double centroid (const std::vector<float>& signal)
    {
        double weighted = 0.0, total = 0.0;

        for (size_t i = 0; i < signal.size(); ++i)
        {
            const auto e = (double) signal[i] * signal[i];
            weighted += e * (double) i;
            total += e;
        }

        return weighted / total / rate;
    }
}

class SmearTests final : public juce::UnitTest
{
public:
    SmearTests() : juce::UnitTest ("Smear", "Astralay") {}

    void runTest() override
    {
        beginTest ("No smear passes the signal through unchanged");
        {
            juce::Random random (3);
            std::vector<float> input (10000);

            for (auto& s : input)
                s = random.nextFloat() - 0.5f;

            const auto out = run (0.0f, 0.2f, input);
            expect (out.left == input);
            expect (out.right == input);
        }

        beginTest ("Full smear keeps an impulse's energy but spreads it out in time");
        {
            const auto out = run (1.0f, 0.2f, impulse (5 * (int) rate));

            expectWithinAbsoluteError (energy (out.left), 1.0, 0.1);
            expectWithinAbsoluteError (energy (out.right), 1.0, 0.1);

            float peak = 0.0f;

            for (auto s : out.left)
                peak = juce::jmax (peak, std::abs (s));

            expect (peak < 0.7f, "Peak " + juce::String (peak));
            expect (centroid (out.left) > 0.02, "Centroid " + juce::String (centroid (out.left)));
        }

        beginTest ("A larger size smears further");
        {
            const auto small = run (1.0f, 0.05f, impulse (5 * (int) rate));
            const auto large = run (1.0f, 0.4f, impulse (5 * (int) rate));
            expect (centroid (large.left) > 3.0 * centroid (small.left));
        }

        beginTest ("Left and right are smeared differently");
        {
            const auto out = run (1.0f, 0.2f, impulse ((int) rate));
            expect (out.left != out.right);
        }
    }
};

static SmearTests smearTests;
