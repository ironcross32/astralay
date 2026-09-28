#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace astralay::dsp
{

/** Smears and diffuses the combined repeats: a cascade of allpass diffusers per channel whose
    delays add up to the smear size. Allpasses spread a sound out in time without colouring its
    overall spectrum or changing its level. Left and right use different delays, which also
    decorrelates them.

    The amount crossfades (at equal power) between the unsmeared and smeared signal, so 0 is an
    exact bypass.
*/
class Smear
{
public:
    static constexpr int numStages = 6;

    void prepare (double sampleRate, double maxSizeSeconds);
    void reset();

    /** amount 0 to 1, size in seconds. */
    void setParameters (float amount, float sizeSeconds);

    void process (float& left, float& right) noexcept;

private:
    struct Allpass
    {
        std::vector<float> buffer;
        int mask = 0, writeIndex = 0;
        float ratio = 0.0f;   // Share of the total size given to this stage.

        float process (float input, int delaySamples, float gain) noexcept;
    };

    double sampleRate = 48000.0;
    float maxSizeSamples = 0.0f;
    std::array<Allpass, numStages> leftStages, rightStages;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> size;
    juce::SmoothedValue<float> dryGain, wetGain;
};

} // namespace astralay::dsp
