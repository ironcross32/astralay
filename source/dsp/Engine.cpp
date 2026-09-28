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

    smear.prepare (sampleRate, maxSmearSeconds);
    freeze.reset (sampleRate, freezeCrossfadeSeconds);

    for (auto* s : { &dryGain, &wetGain, &outputGain })
        s->reset (sampleRate, gainRampSeconds);

    reset();
    restartRandomness (global.reproducible ? global.seed : juce::Random::getSystemRandom().nextInt64());
}

void Engine::restartRandomness (juce::int64 baseSeed)
{
    // Each tap has its own sequence, so enabling one tap doesn't change another's glitches.
    for (size_t t = 0; t < taps.size(); ++t)
        taps[t].restartGlitches (baseSeed * 1000003 + (juce::int64) t);

    samplesToChunk = 0;
}

void Engine::setTransport (const TransportInfo& transport)
{
    if (transport.playing && ! wasPlaying && global.reproducible)
        restartRandomness (global.seed);

    wasPlaying = transport.playing;

    if (transport.synced && transport.playing && transport.hasPosition
        && transport.chunkQuarters > 0.0 && transport.samplesPerQuarter > 0.0)
    {
        // Distance to the next multiple of the chunk length on the host's beat grid.
        const auto position = transport.ppq / transport.chunkQuarters;
        const auto remaining = 1.0 - (position - std::floor (position));
        samplesToChunk = remaining > 1.0 - 1.0e-6 ? 0 : (int) std::llround (remaining * transport.chunkQuarters * transport.samplesPerQuarter);
    }
}

void Engine::reset()
{
    for (auto& tap : taps)
        tap.reset();

    freeze.setCurrentAndTargetValue (global.freeze ? 1.0f : 0.0f);
    setGlobalSettings (global);
    smear.reset();

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
    smear.setParameters (settings.smearAmount, settings.smearSeconds);
}

void Engine::setTapSettings (int tapIndex, const TapSettings& settings)
{
    taps[(size_t) tapIndex].setSettings (settings, global.glideSeconds, global.glitch);
}

void Engine::process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept
{
    const auto chunk = juce::jmax (1, global.glitch.chunkSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        if (samplesToChunk <= 0)
        {
            for (auto& tap : taps)
                if (! tap.isIdle())
                    tap.onChunkBoundary();

            samplesToChunk = chunk;
        }

        --samplesToChunk;

        const auto dryLeft = inLeft[i];
        const auto dryRight = inRight != nullptr ? inRight[i] : dryLeft;
        const auto mono = inRight != nullptr ? 0.5f * (dryLeft + dryRight) : dryLeft;
        const auto frozen = freeze.getNextValue();

        auto wetLeft = 0.0f;
        auto wetRight = 0.0f;

        for (auto& tap : taps)
            if (! tap.isIdle())
                tap.process (mono, frozen, wetLeft, wetRight);

        smear.process (wetLeft, wetRight);

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
