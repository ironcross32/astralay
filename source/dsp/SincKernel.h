#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace astralay::dsp::sinc
{

constexpr int kernelSize = 16;
constexpr int numPhases = 1024;

using Kernel = std::array<float, kernelSize>;

/** The weights for reading a point between two samples with a 16-point windowed sinc, for each of
    numPhases + 1 positions between them. Tap k of a kernel sits k - (kernelSize / 2 - 1) samples
    from the earlier of the two.

    The table is built on the first call, so make that call while preparing rather than on the
    audio thread.
*/
inline const std::vector<Kernel>& kernels()
{
    static const auto table = []
    {
        std::vector<Kernel> result ((size_t) numPhases + 1);

        const auto half = kernelSize / 2;
        const auto pi = juce::MathConstants<double>::pi;
        const auto beta = 7.0;

        // Zeroth-order modified Bessel function, for the Kaiser window.
        const auto bessel = [] (double x)
        {
            auto sum = 1.0, term = 1.0;

            for (int n = 1; n < 30; ++n)
            {
                term *= (x / (2.0 * n)) * (x / (2.0 * n));
                sum += term;
            }

            return sum;
        };

        for (int phase = 0; phase <= numPhases; ++phase)
        {
            const auto t = (double) phase / numPhases;
            std::array<double, kernelSize> weights {};
            auto total = 0.0;

            for (int k = 0; k < kernelSize; ++k)
            {
                // Distance from the point wanted to tap k, which sits at k - (half - 1).
                const auto x = t - (double) (k - (half - 1));
                const auto sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
                const auto position = juce::jlimit (-1.0, 1.0, x / half);
                const auto window = bessel (beta * std::sqrt (1.0 - position * position)) / bessel (beta);

                weights[(size_t) k] = sinc * window;
                total += weights[(size_t) k];
            }

            // Unity gain for a steady signal at every position.
            for (int k = 0; k < kernelSize; ++k)
                result[(size_t) phase][(size_t) k] = (float) (weights[(size_t) k] / total);
        }

        return result;
    }();

    return table;
}

/** The kernel for a point t of the way from one sample to the next, 0 to 1. */
inline const Kernel& kernelFor (float t) noexcept
{
    return kernels()[(size_t) juce::roundToInt (t * (float) numPhases)];
}

} // namespace astralay::dsp::sinc
