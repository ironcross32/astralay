#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

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
    float threshold = 0.2f;             // 0 to 1, scales every probability.
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
        const auto size = (size_t) juce::nextPowerOfTwo (juce::jmax (capacity + 4, 8));
        buffer.assign (size, 0.0f);
        mask = (int) size - 1;
        writeIndex = 0;
    }

    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    int getCapacity() const noexcept { return mask - 3; }

    void push (float sample) noexcept
    {
        writeIndex = (writeIndex + 1) & mask;
        buffer[(size_t) writeIndex] = sample;
    }

    /** The sample pushed samplesAgo pushes ago; 0 is the latest. */
    float back (int samplesAgo) const noexcept
    {
        return buffer[(size_t) ((writeIndex - samplesAgo) & mask)];
    }

    /** Linearly interpolated read, samplesAgo >= 0. */
    float read (float samplesAgo) const noexcept
    {
        const auto whole = (int) samplesAgo;
        const auto fraction = samplesAgo - (float) whole;
        const auto a = back (whole);
        const auto b = back (whole + 1);
        return a + (b - a) * fraction;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writeIndex = 0;
};

} // namespace astralay::dsp
