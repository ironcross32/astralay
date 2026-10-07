#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include "SincKernel.h"

namespace astralay::dsp
{

/** Glitch types, in the order they are processed when several run at once. */
enum class GlitchType
{
    reverse,
    stutter,
    granularize,
    pitch,
    lpcFormant,
    cepstralFormant,
    ringModulation,
    frequencyModulation,
    bitCrusher
};

constexpr int numGlitchTypes = 9;

/** A range the plugin picks from each time a glitch fires. min may be above max; they are swapped. */
struct RandomRange
{
    float min = 0.0f, max = 0.0f;

    float pick (juce::Random& random) const noexcept
    {
        const auto lo = juce::jmin (min, max);
        const auto hi = juce::jmax (min, max);
        return lo + random.nextFloat() * (hi - lo);
    }

    int pickInt (juce::Random& random) const noexcept
    {
        const auto lo = juce::roundToInt (juce::jmin (min, max));
        const auto hi = juce::roundToInt (juce::jmax (min, max));
        return lo + random.nextInt (hi - lo + 1);
    }
};

/** Block-rate glitch settings for one tap, in DSP units. */
struct TapGlitchSettings
{
    std::array<float, numGlitchTypes> probability {};   // 0 to 1, indexed by GlitchType.

    RandomRange stutterSlice { 960.0f, 5760.0f };         // Samples.
    RandomRange grainSize { 960.0f, 3840.0f };            // Samples.
    RandomRange grainDensity { 10.0f, 40.0f };            // Grains per second.
    RandomRange pitch { -12.0f, 12.0f };                  // Semitones: where a sweep turns back, or varispeed's speed change.
    RandomRange pitchSpeed { 0.0f, 12.0f };               // Semitones a sweep moves on each pass through the tap.
    bool varispeed = false;                               // Otherwise the pitch glitch sweeps.
    RandomRange lpcShift { -5.0f, 5.0f };                 // Semitones.
    RandomRange cepstralShift { -5.0f, 5.0f };            // Semitones.
    RandomRange ringFrequency { 30.0f, 800.0f };          // Hz.
    RandomRange fmRatio { 0.5f, 3.0f };
    RandomRange fmIndex { 0.5f, 4.0f };
    RandomRange bits { 4.0f, 10.0f };
    RandomRange rateReduction { 1.0f, 8.0f };
};

/** Block-rate glitch settings shared by all taps. */
struct GlitchGlobalSettings
{
    bool stopped = false;               // No glitch starts, and any that are running fade out.
    bool outputAndFeedback = false;     // Otherwise glitches apply to the feedback path only.
    int chunkSamples = 6000;
    int maxSimultaneous = 2;
    RandomRange lengthChunks { 1.0f, 4.0f };
};

/** A circular buffer of recent input, read back by glitches that work on past audio. */
class HistoryBuffer
{
public:
    void prepare (int capacity)
    {
        sinc::kernels();   // Built here, not on the audio thread.

        const auto size = (size_t) juce::nextPowerOfTwo (juce::jmax (capacity + 4, 8));
        buffer.assign (size, 0.0f);
        mask = (int) size - 1;
        writeIndex = 0;
        validSamples = 0;
    }

    // Invalidate in constant time; reads supply silence until fresh samples replace the old ones.
    void clear() noexcept { validSamples = 0; }

    int getCapacity() const noexcept { return mask - 3; }

    void push (float sample) noexcept
    {
        writeIndex = (writeIndex + 1) & mask;
        buffer[(size_t) writeIndex] = sample;
        validSamples = juce::jmin (validSamples + 1, mask + 1);
    }

    /** The sample pushed samplesAgo pushes ago; 0 is the latest. */
    float back (int samplesAgo) const noexcept
    {
        if ((samplesAgo & mask) >= validSamples)
            return 0.0f;
        return buffer[(size_t) ((writeIndex - samplesAgo) & mask)];
    }

    /** Linearly interpolated read, samplesAgo >= 0. Cheap, and dulls the top a little each time:
        for values that travel beside the audio, not for audio that goes round a loop.
    */
    float read (float samplesAgo) const noexcept
    {
        const auto whole = (int) samplesAgo;
        const auto fraction = samplesAgo - (float) whole;
        const auto a = back (whole);
        const auto b = back (whole + 1);
        return a + (b - a) * fraction;
    }

    /** How far back readAudio() has to be reading for its full quality. */
    static constexpr int audioReadMargin = sinc::kernelSize / 2 - 1;

    /** Reads audio between samples with the delay line's 16-point windowed sinc, which keeps the
        top of the spectrum nearly intact. A feedback loop passes through a glitch's read many
        times a second, and linear interpolation lost treble on every pass.

        The kernel reaches half its length either side. Closer to the newest sample than
        audioReadMargin, where that would be audio not yet written, this falls back to read().
    */
    float readAudio (float samplesAgo) const noexcept
    {
        const auto whole = (int) samplesAgo;
        const auto fraction = samplesAgo - (float) whole;

        if (whole < audioReadMargin)
            return read (samplesAgo);

        if (fraction <= 0.0f)
            return back (whole);

        // The point wanted lies between the samples whole + 1 and whole ago, 1 - fraction of the
        // way from the earlier to the later.
        const auto& kernel = sinc::kernelFor (1.0f - fraction);
        const auto first = whole + sinc::kernelSize / 2;
        auto sum = 0.0f;

        for (int k = 0; k < sinc::kernelSize; ++k)
            sum += back (first - k) * kernel[(size_t) k];

        return sum;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writeIndex = 0;
    int validSamples = 0;
};

} // namespace astralay::dsp
