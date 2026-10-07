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

    for (size_t t = 0; t < taps.size(); ++t)
        taps[t].prepare (sampleRate, maxDelaySamples, (int) t);

    leftDcBlocker.prepare (sampleRate);
    rightDcBlocker.prepare (sampleRate);
    smear.prepare (sampleRate, maxSmearSeconds);
    tape.prepare (sampleRate);
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
    formantClock = 0;
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

    if (transport.synced && transport.playing && transport.hasPosition && ! tape.isSlowed()
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
    formantClock = 0;
    expectedSamplePosition.reset();

    for (auto& tap : taps)
        tap.reset();

    freeze.setCurrentAndTargetValue (global.freeze ? 1.0f : 0.0f);
    setGlobalSettings (global);
    leftDcBlocker.reset();
    rightDcBlocker.reset();
    smear.reset();

    tape.reset (global.tapeStopped);
    tapeInputSum = 0.0f;
    tapeInputCount = 0;
    olderLeft = olderRight = newerLeft = newerRight = 0.0f;

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
    tape.setSettings (settings.tapeStopped, settings.tapeStopSeconds, settings.tapeStartSeconds);
}

void Engine::setTapSettings (int tapIndex, const TapSettings& settings)
{
    taps[(size_t) tapIndex].setSettings (settings, global.glideSeconds, global.glitch);
}

void Engine::processTapeSample (float input, float& wetLeft, float& wetRight) noexcept
{
    if (samplesToChunk <= 0)
    {
        for (auto& tap : taps)
            if (! tap.isIdle())
                tap.onChunkBoundary (freeze.getCurrentValue());

        samplesToChunk = juce::jmax (1, global.glitch.chunkSamples);
    }

    --samplesToChunk;

    const auto frozen = freeze.getNextValue();

    wetLeft = 0.0f;
    wetRight = 0.0f;

    for (auto& tap : taps)
        if (! tap.isIdle())
            tap.process (input, frozen, wetLeft, wetRight, global.freeze && global.freezeSustain, formantClock);

    // The largest supported formant hop is 2048 samples. Keep every tap on the same clock,
    // including taps enabled later; disabled taps do no processing to maintain this alignment.
    formantClock = (formantClock + 1) & (FormantShifter::clockPeriod - 1);

    wetLeft = leftDcBlocker.process (wetLeft);
    wetRight = rightDcBlocker.process (wetRight);

    smear.process (wetLeft, wetRight);
}

void Engine::process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept
{
    const auto ceiling = global.clipCeiling;

    for (int i = 0; i < numSamples; ++i)
    {
        // An input sample that isn't a number is taken as silence. Left alone it would stay in the
        // delay lines and filters for good, and reach the output even through a gain of zero.
        const auto dryLeft = isNonFinite (inLeft[i]) ? 0.0f : inLeft[i];
        const auto dryRight = inRight == nullptr ? dryLeft : (isNonFinite (inRight[i]) ? 0.0f : inRight[i]);
        const auto mono = inRight != nullptr ? 0.5f * (dryLeft + dryRight) : dryLeft;

        auto wetLeft = 0.0f;
        auto wetRight = 0.0f;

        const auto tapeMoved = tape.advance();

        if (tape.isAtFullSpeed())
        {
            processTapeSample (mono, wetLeft, wetRight);

            // Kept up to date for the moment the tape slows.
            newerLeft = wetLeft;
            newerRight = wetRight;
        }
        else
        {
            tapeInputSum += mono * tape.getRecordGain();
            ++tapeInputCount;

            if (tapeMoved)
            {
                olderLeft = newerLeft;
                olderRight = newerRight;
                processTapeSample (tapeInputSum / (float) tapeInputCount, newerLeft, newerRight);
            }

            if (tapeMoved || tape.isStopped())
            {
                tapeInputSum = 0.0f;
                tapeInputCount = 0;
            }

            const auto fraction = tape.getFraction();
            const auto level = tape.getGain();

            wetLeft = (olderLeft + (newerLeft - olderLeft) * fraction) * level;
            wetRight = (olderRight + (newerRight - olderRight) * fraction) * level;
        }

        const auto dry = dryGain.getNextValue();
        const auto wet = wetGain.getNextValue();
        const auto out = outputGain.getNextValue();

        outLeft[i]  = (dryLeft * dry + wetLeft * wet) * out;
        outRight[i] = (dryRight * dry + wetRight * wet) * out;
        outputProbe.sample (outLeft[i], outRight[i], ceiling, freeze.getCurrentValue(), out);

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
