#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace astralay::dsp
{

/** A mono circular buffer read with 4-point Hermite interpolation, so the delay time can move
    smoothly (tape-style glide) without zipper noise.

    Call read() before push() for each sample. A delay of 1 returns the sample pushed on the
    previous call.
*/
class DelayLine
{
public:
    static constexpr float minDelaySamples = 2.0f;

    void prepare (int maxDelaySamples)
    {
        const auto size = (size_t) juce::nextPowerOfTwo (maxDelaySamples + 4);
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

    /** Reads the sample delaySamples ago. The delay is clamped to [minDelaySamples, getMaxDelay()]. */
    float read (float delaySamples) const noexcept
    {
        const auto d = juce::jlimit (minDelaySamples, maxDelay, delaySamples);
        const auto whole = (int) d;
        const auto fraction = d - (float) whole;

        // Interpolate between x0 = [writeIndex - whole - 1] and x1 = [writeIndex - whole].
        const auto i = writeIndex - whole - 1;
        const auto t = 1.0f - fraction;

        const auto xm1 = at (i - 1);
        const auto x0  = at (i);
        const auto x1  = at (i + 1);
        const auto x2  = at (i + 2);

        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    void push (float sample) noexcept
    {
        buffer[(size_t) writeIndex] = sample;
        writeIndex = (writeIndex + 1) & mask;
    }

private:
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
