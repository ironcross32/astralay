#pragma once

#include <array>
#include "Tap.h"

namespace astralay::dsp
{

/** Block-rate settings shared by all taps, already converted to DSP units. */
struct GlobalSettings
{
    float glideSeconds = 0.1f;
    bool freeze = false;
    float mix = 0.5f;           // 0 (dry) to 1 (wet).
    float outputGain = 1.0f;    // Linear.
};

/** The whole signal path: 16 taps, freeze, dry/wet mix and output gain. */
class Engine
{
public:
    static constexpr int numTaps = 16;

    void prepare (double sampleRate, int maxBlockSize, double maxDelaySeconds);
    void reset();

    void setGlobalSettings (const GlobalSettings& settings);
    void setTapSettings (int tapIndex, const TapSettings& settings);

    /** Processes one block. inRight may be null for a mono input. The input and output pointers may
        alias (in-place processing).
    */
    void process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept;

private:
    std::array<Tap, numTaps> taps;
    GlobalSettings global;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> freeze;
    juce::SmoothedValue<float> dryGain, wetGain, outputGain;
    double sampleRate = 44100.0;
};

} // namespace astralay::dsp
