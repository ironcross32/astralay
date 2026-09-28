#include "Engine.h"

namespace astralay::dsp
{

namespace
{
    constexpr double freezeCrossfadeSeconds = 0.05;
    constexpr double gainRampSeconds = 0.02;
}

void Engine::prepare (double newSampleRate, int, double maxDelaySeconds)
{
    sampleRate = newSampleRate;

    const auto maxDelaySamples = (int) std::ceil (maxDelaySeconds * sampleRate);

    for (auto& tap : taps)
        tap.prepare (sampleRate, maxDelaySamples);

    freeze.reset (sampleRate, freezeCrossfadeSeconds);

    for (auto* s : { &dryGain, &wetGain, &outputGain })
        s->reset (sampleRate, gainRampSeconds);

    reset();
}

void Engine::reset()
{
    for (auto& tap : taps)
        tap.reset();

    freeze.setCurrentAndTargetValue (global.freeze ? 1.0f : 0.0f);
    setGlobalSettings (global);

    for (auto* s : { &dryGain, &wetGain, &outputGain })
        s->setCurrentAndTargetValue (s->getTargetValue());
}

void Engine::setGlobalSettings (const GlobalSettings& settings)
{
    global = settings;

    // Equal-power dry/wet crossfade.
    const auto angle = juce::jlimit (0.0f, 1.0f, settings.mix) * juce::MathConstants<float>::halfPi;

    freeze.setTargetValue (settings.freeze ? 1.0f : 0.0f);
    dryGain.setTargetValue (std::cos (angle));
    wetGain.setTargetValue (std::sin (angle));
    outputGain.setTargetValue (settings.outputGain);
}

void Engine::setTapSettings (int tapIndex, const TapSettings& settings)
{
    taps[(size_t) tapIndex].setSettings (settings, global.glideSeconds);
}

void Engine::process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto dryLeft = inLeft[i];
        const auto dryRight = inRight != nullptr ? inRight[i] : dryLeft;
        const auto mono = inRight != nullptr ? 0.5f * (dryLeft + dryRight) : dryLeft;
        const auto frozen = freeze.getNextValue();

        auto wetLeft = 0.0f;
        auto wetRight = 0.0f;

        for (auto& tap : taps)
            if (! tap.isIdle())
                tap.process (mono, frozen, wetLeft, wetRight);

        const auto dry = dryGain.getNextValue();
        const auto wet = wetGain.getNextValue();
        const auto out = outputGain.getNextValue();

        outLeft[i]  = (dryLeft * dry + wetLeft * wet) * out;
        outRight[i] = (dryRight * dry + wetRight * wet) * out;
    }

    for (auto& tap : taps)
        tap.endBlock();
}

} // namespace astralay::dsp
