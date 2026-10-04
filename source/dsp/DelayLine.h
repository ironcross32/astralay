#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace astralay::dsp
{

/** A mono circular buffer read between samples, so the delay time can move smoothly (tape-style
    glide) without zipper noise.

    A delay of a whole number of samples returns the stored sample untouched. Anything else is
    interpolated with a 16-point windowed sinc, which keeps the top of the spectrum nearly intact.
    That matters because a short feedback loop reads its own output back a hundred or more times a
    second: the 4-point interpolation this replaced lost a little treble on each pass, and a frozen
    loop went dull within seconds.

    Call read() before push() for each sample. A delay of 1 returns the sample pushed on the
    previous call.
*/
class DelayLine
{
public:
    static constexpr float minDelaySamples = 2.0f;

    void prepare (int maxDelaySamples)
    {
        const auto size = (size_t) juce::nextPowerOfTwo (maxDelaySamples + kernelSize + 4);
        buffer.assign (size, 0.0f);
        mask = (int) size - 1;
        writeIndex = 0;
        maxDelay = (float) maxDelaySamples;
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
    }

    float getMaxDelay() const noexcept { return maxDelay; }

    /** Reads the sample delaySamples ago. The delay is clamped to [minDelaySamples, getMaxDelay()].
        It is a double because a float holds a delay of several seconds only to a few hundredths of
        a sample, too coarse for a slow glide.
    */
    float read (double delaySamples) const noexcept
    {
        const auto d = juce::jlimit ((double) minDelaySamples, (double) maxDelay, delaySamples);
        const auto whole = (int) d;
        const auto fraction = (float) (d - (double) whole);

        if (fraction <= 0.0f)
            return at (writeIndex - whole);

        // The point wanted lies between [i] and [i + 1], t of the way along.
        const auto i = writeIndex - whole - 1;
        const auto t = 1.0f - fraction;

        // The kernel reaches half its length either side, which this close to the newest sample
        // would be audio not yet written. Only delays far shorter than any tap time come here.
        if (whole < kernelSize / 2)
            return hermite (i, t);

        const auto& kernel = kernels()[(size_t) juce::roundToInt (t * (float) numPhases)];
        const auto first = i - (kernelSize / 2 - 1);
        auto sum = 0.0f;

        for (int k = 0; k < kernelSize; ++k)
            sum += at (first + k) * kernel[(size_t) k];

        return sum;
    }

    void push (float sample) noexcept
    {
        buffer[(size_t) writeIndex] = sample;
        writeIndex = (writeIndex + 1) & mask;
    }

private:
    static constexpr int kernelSize = 16;
    static constexpr int numPhases = 1024;

    using Kernel = std::array<float, kernelSize>;

    /** The interpolation weights for each of numPhases + 1 positions between two samples. */
    static const std::vector<Kernel>& kernels()
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

    float hermite (int i, float t) const noexcept
    {
        const auto xm1 = at (i - 1);
        const auto x0  = at (i);
        const auto x1  = at (i + 1);
        const auto x2  = at (i + 2);

        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    float at (int index) const noexcept
    {
        return buffer[(size_t) (index & mask)];
    }

    std::vector<float> buffer;
    int mask = 0;
    int writeIndex = 0;
    float maxDelay = 0.0f;
};

} // namespace astralay::dsp
