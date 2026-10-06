#include "Engine.h"
#include "Finite.h"
#include "Seed.h"
#include <limits>

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

    leftDcBlocker.prepare (sampleRate);
    rightDcBlocker.prepare (sampleRate);
    smear.prepare (sampleRate, maxSmearSeconds);
    freeze.reset (sampleRate, freezeCrossfadeSeconds);

    for (auto* s : { &dryGain, &wetGain, &outputGain })
        s->reset (sampleRate, gainRampSeconds);

    setDiagnostics (diagnosticSink);
    reset();
    restartRandomness (global.reproducible ? global.seed : juce::Random::getSystemRandom().nextInt64());
}

void Engine::setDiagnostics (diagnostics::Sink* sink)
{
    diagnosticSink = sink;
    outputProbe.attach (sink, sampleRate);

    for (size_t t = 0; t < taps.size(); ++t)
        taps[t].setDiagnostics (sink, (int) t);
}

void Engine::restartRandomness (juce::int64 baseSeed)
{
    // Each tap has its own sequence, so enabling one tap doesn't change another's glitches.
    for (size_t t = 0; t < taps.size(); ++t)
        taps[t].restartGlitches (seedForTap (baseSeed, t));

    samplesToChunk = 0;
}

void Engine::setTransport (const TransportInfo& transport)
{
    // Some hosts suspend processing while stopped, so we never see playing == false.
    // Sample time also catches a restart/seek/loop wrap without confusing tempo changes with jumps.
    const auto jumped = expectedSamplePosition && transport.samplePosition
                        && *transport.samplePosition != *expectedSamplePosition;
    if (transport.playing && (! wasPlaying || jumped) && global.reproducible)
        restartRandomness (global.seed);

    wasPlaying = transport.playing;
    expectedSamplePosition = transport.playing ? transport.samplePosition : std::nullopt;

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
    wasPlaying = false;
    expectedSamplePosition.reset();

    for (auto& tap : taps)
        tap.reset();

    freeze.setCurrentAndTargetValue (global.freeze ? 1.0f : 0.0f);
    setGlobalSettings (global);
    leftDcBlocker.reset();
    rightDcBlocker.reset();
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
    const auto ceiling = global.clipCeiling;

    for (int i = 0; i < numSamples; ++i)
    {
        if (samplesToChunk <= 0)
        {
            for (auto& tap : taps)
                if (! tap.isIdle())
                    tap.onChunkBoundary (freeze.getCurrentValue());

            samplesToChunk = chunk;
        }

        --samplesToChunk;

        // An input sample that isn't a number is taken as silence. Left alone it would stay in the
        // delay lines and filters for good, and reach the output even through a gain of zero.
        const auto dryLeft = isNonFinite (inLeft[i]) ? 0.0f : inLeft[i];
        const auto dryRight = inRight == nullptr ? dryLeft : (isNonFinite (inRight[i]) ? 0.0f : inRight[i]);
        const auto mono = inRight != nullptr ? 0.5f * (dryLeft + dryRight) : dryLeft;
        const auto frozen = freeze.getNextValue();

        auto wetLeft = 0.0f;
        auto wetRight = 0.0f;

        for (auto& tap : taps)
            if (! tap.isIdle())
                tap.process (mono, frozen, wetLeft, wetRight, global.freeze && global.freezeSustain);

        wetLeft = leftDcBlocker.process (wetLeft);
        wetRight = rightDcBlocker.process (wetRight);

        smear.process (wetLeft, wetRight);

        const auto dry = dryGain.getNextValue();
        const auto wet = wetGain.getNextValue();
        const auto out = outputGain.getNextValue();

        outLeft[i]  = (dryLeft * dry + wetLeft * wet) * out;
        outRight[i] = (dryRight * dry + wetRight * wet) * out;
        outputProbe.sample (outLeft[i], outRight[i], ceiling, frozen, out);

        if (ceiling > 0.0f)
        {
            outLeft[i]  = juce::jlimit (-ceiling, ceiling, outLeft[i]);
            outRight[i] = juce::jlimit (-ceiling, ceiling, outRight[i]);
        }
    }

    for (auto& tap : taps)
        tap.endBlock();

    if (expectedSamplePosition && numSamples > 0)
    {
        if (*expectedSamplePosition <= std::numeric_limits<juce::int64>::max() - numSamples)
            *expectedSamplePosition += numSamples;
        else
            expectedSamplePosition.reset();
    }
}

} // namespace astralay::dsp
