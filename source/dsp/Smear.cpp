#include "Smear.h"

namespace astralay::dsp
{

namespace
{
    constexpr float diffusion = 0.6f;          // Allpass gain: how strongly each stage smears.
    constexpr double sizeRampSeconds = 0.1;
    constexpr double gainRampSeconds = 0.02;

    // Shares of the total size per stage. Roughly geometric and mutually inharmonic so echoes from
    // different stages don't line up; the right channel's differ slightly for stereo spread.
    constexpr std::array<float, Smear::numStages> leftRatios  { 0.031f, 0.067f, 0.109f, 0.163f, 0.251f, 0.379f };
    constexpr std::array<float, Smear::numStages> rightRatios { 0.037f, 0.059f, 0.121f, 0.149f, 0.269f, 0.365f };
}

float Smear::Allpass::process (float input, int delaySamples, float gain) noexcept
{
    // Schroeder allpass: w[n] = x[n] + g w[n - M], y[n] = w[n - M] - g w[n]. M is a whole number of
    // samples: interpolating inside the feedback loop would low-pass the signal on every pass.
    // buffer[writeIndex] holds w[n - 1], so w[n - M] is at writeIndex - M + 1.
    const auto delayed = buffer[(size_t) ((writeIndex - delaySamples + 1) & mask)];

    const auto w = input + gain * delayed;
    writeIndex = (writeIndex + 1) & mask;
    buffer[(size_t) writeIndex] = w;

    return delayed - gain * w;
}

void Smear::prepare (double newSampleRate, double maxSizeSeconds)
{
    sampleRate = newSampleRate;
    maxSizeSamples = (float) (maxSizeSeconds * sampleRate);

    const auto setUp = [this] (std::array<Allpass, numStages>& stages, const std::array<float, numStages>& ratios)
    {
        for (size_t i = 0; i < stages.size(); ++i)
        {
            const auto length = juce::nextPowerOfTwo ((int) std::ceil (ratios[i] * maxSizeSamples) + 4);
            stages[i].buffer.assign ((size_t) length, 0.0f);
            stages[i].mask = length - 1;
            stages[i].ratio = ratios[i];
        }
    };

    setUp (leftStages, leftRatios);
    setUp (rightStages, rightRatios);

    size.reset (sampleRate, sizeRampSeconds);
    dryGain.reset (sampleRate, gainRampSeconds);
    wetGain.reset (sampleRate, gainRampSeconds);

    reset();
}

void Smear::reset()
{
    for (auto* stages : { &leftStages, &rightStages })
    {
        for (auto& stage : *stages)
        {
            std::fill (stage.buffer.begin(), stage.buffer.end(), 0.0f);
            stage.writeIndex = 0;
        }
    }

    size.setCurrentAndTargetValue (size.getTargetValue());
    dryGain.setCurrentAndTargetValue (dryGain.getTargetValue());
    wetGain.setCurrentAndTargetValue (wetGain.getTargetValue());
}

void Smear::setParameters (float amount, float sizeSeconds)
{
    const auto angle = juce::jlimit (0.0f, 1.0f, amount) * juce::MathConstants<float>::halfPi;
    dryGain.setTargetValue (std::cos (angle));
    wetGain.setTargetValue (std::sin (angle));
    size.setTargetValue (juce::jlimit (1.0f, maxSizeSamples, sizeSeconds * (float) sampleRate));
}

void Smear::process (float& left, float& right) noexcept
{
    const auto currentSize = size.getNextValue();
    const auto dry = dryGain.getNextValue();
    const auto wet = wetGain.getNextValue();

    auto smearedLeft = left;
    auto smearedRight = right;

    for (size_t i = 0; i < (size_t) numStages; ++i)
    {
        smearedLeft  = leftStages[i].process (smearedLeft, juce::jmax (1, juce::roundToInt (leftStages[i].ratio * currentSize)), diffusion);
        smearedRight = rightStages[i].process (smearedRight, juce::jmax (1, juce::roundToInt (rightStages[i].ratio * currentSize)), diffusion);
    }

    left  = left * dry + smearedLeft * wet;
    right = right * dry + smearedRight * wet;
}

} // namespace astralay::dsp
