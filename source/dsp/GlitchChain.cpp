#include "GlitchChain.h"

namespace astralay::dsp
{

namespace
{
    constexpr double fadeSeconds = 0.005;
    constexpr double reverseCapSeconds = 2.0;
    constexpr double sliceCapSeconds = 2.0;
    constexpr double grainHistorySeconds = 1.0;
    constexpr double grainSpreadSeconds = 0.5;
    constexpr double pitchWindowSeconds = 0.04;
    constexpr double fmHistorySeconds = 0.1;
    constexpr double fmMaxDepthSeconds = 0.04;
    constexpr double pitchEstimateSeconds = 0.05;

    constexpr auto twoPi = juce::MathConstants<float>::twoPi;
    constexpr auto pi = juce::MathConstants<float>::pi;

    /** Gain of a short fade at both ends of a segment of the given length. */
    float edgeFade (int position, int length, int fade) noexcept
    {
        if (fade <= 0)
            return 1.0f;

        return juce::jmin (1.0f, (float) position / (float) fade, (float) (length - position) / (float) fade);
    }

    float wrap (float phase) noexcept
    {
        return phase - std::floor (phase);
    }
}

void GlitchChain::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    const auto samples = [this] (double seconds) { return (int) std::ceil (seconds * sampleRate); };

    reverseCap = samples (reverseCapSeconds);
    reverseInput.prepare (2 * reverseCap + 4);
    stutterInput.prepare (samples (sliceCapSeconds));
    grainInput.prepare (samples (grainHistorySeconds));
    pitchInput.prepare (samples (pitchWindowSeconds) + 4);
    fmInput.prepare (samples (fmHistorySeconds));

    slice.assign ((size_t) samples (sliceCapSeconds), 0.0f);
    pitchWindow = (float) (pitchWindowSeconds * sampleRate);

    lpcShifter.prepare (sampleRate, FormantShifter::Method::lpc);
    cepstralShifter.prepare (sampleRate, FormantShifter::Method::cepstral);

    reset();
}

void GlitchChain::reset()
{
    for (auto* history : { &reverseInput, &stutterInput, &grainInput, &pitchInput, &fmInput })
        history->clear();

    lpcShifter.reset();
    cepstralShifter.reset();

    for (auto& slot : slots)
        slot = {};

    for (auto& voice : voices)
        voice = {};
}

void GlitchChain::reseed (juce::int64 seed)
{
    random.setSeed (seed);
}

void GlitchChain::setSettings (const TapGlitchSettings& tapSettings, const GlitchGlobalSettings& globalSettings)
{
    settings = tapSettings;
    global = globalSettings;
}

int GlitchChain::getNumActive() const noexcept
{
    int count = 0;

    for (const auto& slot : slots)
        count += slot.active ? 1 : 0;

    return count;
}

void GlitchChain::onChunkBoundary()
{
    for (int i = 0; i < numGlitchTypes; ++i)
    {
        const auto type = (GlitchType) i;

        // Always draw, so the random sequence doesn't depend on which glitches are running.
        const auto roll = random.nextFloat();

        if (slots[(size_t) i].active || getNumActive() >= global.maxSimultaneous)
            continue;

        if (roll < settings.probability[(size_t) i] * global.threshold)
            start (type);
    }
}

void GlitchChain::start (GlitchType type)
{
    auto& slot = slots[(size_t) type];
    slot.active = true;
    slot.elapsed = 0;
    slot.length = juce::jmax (1, global.lengthChunks.pickInt (random)) * juce::jmax (1, global.chunkSamples);
    slot.fade = juce::jmin ((int) (fadeSeconds * sampleRate), slot.length / 2);

    switch (type)
    {
        case GlitchType::reverse:
            reverseSegment = juce::jlimit (16, reverseCap, global.chunkSamples);
            reversePosition = 0;
            break;

        case GlitchType::stutter:
        {
            sliceLength = juce::jlimit (16, (int) slice.size(), juce::roundToInt (settings.stutterSlice.pick (random)));
            sliceLength = juce::jmin (sliceLength, stutterInput.getCapacity());

            for (int i = 0; i < sliceLength; ++i)
                slice[(size_t) i] = stutterInput.back (sliceLength - 1 - i);

            slicePosition = 0;
            break;
        }

        case GlitchType::granularize:
        {
            const auto maxSize = grainInput.getCapacity() / 4;
            grainSize = juce::jlimit (16, maxSize, juce::roundToInt (settings.grainSize.pick (random)));
            grainSpread = juce::jmax (0, juce::jmin ((int) (grainSpreadSeconds * sampleRate), grainInput.getCapacity() - grainSize - 2));

            const auto density = juce::jmax (0.1f, settings.grainDensity.pick (random));
            grainInterval = (float) sampleRate / density;
            grainCountdown = 0.0f;

            // Hann grains average 0.5, so this keeps the level steady as they overlap.
            const auto overlap = density * (float) grainSize / (float) sampleRate;
            grainGain = 1.0f / juce::jmax (1.0f, overlap * 0.5f);

            for (auto& voice : voices)
                voice.active = false;

            break;
        }

        case GlitchType::pitch:
            pitchRatio = std::pow (2.0f, settings.pitch.pick (random) / 12.0f);
            pitchPhase = 0.0f;
            break;

        case GlitchType::ringModulation:
            ringFrequency = settings.ringFrequency.pick (random);
            ringPhase = 0.0f;
            break;

        case GlitchType::frequencyModulation:
        {
            const auto fundamental = estimateFundamental();
            fmFrequency = settings.fmRatio.pick (random) * fundamental;

            // A phase deviation of index radians at the fundamental.
            const auto depthSeconds = settings.fmIndex.pick (random) / (twoPi * fundamental);
            fmDepth = (float) juce::jmin ((double) depthSeconds, fmMaxDepthSeconds) * (float) sampleRate;
            fmCentre = fmDepth + 2.0f;
            fmPhase = 0.0f;
            break;
        }

        case GlitchType::bitCrusher:
            crushLevels = std::pow (2.0f, (float) (juce::jlimit (1, 16, settings.bits.pickInt (random)) - 1));
            crushRate = juce::jmax (1.0f, settings.rateReduction.pick (random));
            crushCounter = crushRate;
            break;

        case GlitchType::lpcFormant:
            lpcShifter.start (settings.lpcShift.pick (random));
            break;

        case GlitchType::cepstralFormant:
            cepstralShifter.start (settings.cepstralShift.pick (random));
            break;
    }
}

float GlitchChain::envelope (GlitchType type) noexcept
{
    auto& slot = slots[(size_t) type];
    const auto gain = edgeFade (slot.elapsed, slot.length, slot.fade);

    if (++slot.elapsed >= slot.length)
        slot.active = false;

    return gain;
}

float GlitchChain::applyStage (GlitchType type, float dry, float wet) noexcept
{
    return dry + (wet - dry) * envelope (type);
}

float GlitchChain::process (float input) noexcept
{
    auto y = input;

    reverseInput.push (y);
    if (isActive (GlitchType::reverse))
        y = applyStage (GlitchType::reverse, y, reverse());

    stutterInput.push (y);
    if (isActive (GlitchType::stutter))
        y = applyStage (GlitchType::stutter, y, stutter());

    grainInput.push (y);
    if (isActive (GlitchType::granularize))
        y = applyStage (GlitchType::granularize, y, granularize());

    pitchInput.push (y);
    if (isActive (GlitchType::pitch))
        y = applyStage (GlitchType::pitch, y, pitchShift());

    lpcShifter.push (y);
    if (isActive (GlitchType::lpcFormant))
        y = applyStage (GlitchType::lpcFormant, y, lpcShifter.next());

    cepstralShifter.push (y);
    if (isActive (GlitchType::cepstralFormant))
        y = applyStage (GlitchType::cepstralFormant, y, cepstralShifter.next());

    if (isActive (GlitchType::ringModulation))
        y = applyStage (GlitchType::ringModulation, y, ringModulate (y));

    fmInput.push (y);
    if (isActive (GlitchType::frequencyModulation))
        y = applyStage (GlitchType::frequencyModulation, y, frequencyModulate());

    if (isActive (GlitchType::bitCrusher))
        y = applyStage (GlitchType::bitCrusher, y, bitCrush (y));

    return y;
}

float GlitchChain::reverse() noexcept
{
    // Each segment plays the segment before it backwards: at n samples in, read 2n + 1 samples ago.
    const auto fade = juce::jmin ((int) (0.003 * sampleRate), reverseSegment / 4);
    const auto wet = reverseInput.back (2 * reversePosition + 1) * edgeFade (reversePosition, reverseSegment, fade);

    if (++reversePosition >= reverseSegment)
        reversePosition = 0;

    return wet;
}

float GlitchChain::stutter() noexcept
{
    const auto fade = juce::jmin ((int) (0.002 * sampleRate), sliceLength / 4);
    const auto wet = slice[(size_t) slicePosition] * edgeFade (slicePosition, sliceLength, fade);

    if (++slicePosition >= sliceLength)
        slicePosition = 0;

    return wet;
}

float GlitchChain::granularize() noexcept
{
    grainCountdown -= 1.0f;

    if (grainCountdown <= 0.0f)
    {
        // Jitter the spacing by up to half an interval either way so grains don't sound periodic.
        grainCountdown += grainInterval * (0.5f + random.nextFloat());

        for (auto& voice : voices)
        {
            if (! voice.active)
            {
                voice.active = true;
                voice.size = grainSize;
                voice.position = 0;
                voice.samplesAgo = grainSize + random.nextInt (grainSpread + 1);
                break;
            }
        }
    }

    auto wet = 0.0f;

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        // Reading forward at normal speed from a point in the past keeps a constant distance behind.
        const auto window = 0.5f - 0.5f * std::cos (twoPi * (float) voice.position / (float) voice.size);
        wet += grainInput.back (voice.samplesAgo) * window;

        if (++voice.position >= voice.size)
            voice.active = false;
    }

    return wet * grainGain;
}

float GlitchChain::pitchShift() noexcept
{
    // Two read heads sweep through a short window at the pitch ratio, crossfading so that one is
    // always away from the jump back.
    pitchPhase = wrap (pitchPhase + (1.0f - pitchRatio) / pitchWindow);
    const auto otherPhase = wrap (pitchPhase + 0.5f);

    return pitchInput.read (1.0f + pitchPhase * pitchWindow) * std::sin (pi * pitchPhase)
         + pitchInput.read (1.0f + otherPhase * pitchWindow) * std::sin (pi * otherPhase);
}

float GlitchChain::ringModulate (float input) noexcept
{
    ringPhase = wrap (ringPhase + ringFrequency / (float) sampleRate);
    return input * std::sin (twoPi * ringPhase);
}

float GlitchChain::frequencyModulate() noexcept
{
    // Modulating the read position is phase modulation: sidebands at the fundamental plus and
    // minus multiples of the modulator frequency.
    fmPhase = wrap (fmPhase + fmFrequency / (float) sampleRate);
    return fmInput.read (fmCentre + fmDepth * std::sin (twoPi * fmPhase));
}

float GlitchChain::bitCrush (float input) noexcept
{
    crushCounter += 1.0f;

    if (crushCounter >= crushRate)
    {
        crushCounter -= crushRate;
        crushHeld = juce::jlimit (-1.0f, 1.0f, std::round (input * crushLevels) / crushLevels);
    }

    return crushHeld;
}

float GlitchChain::estimateFundamental() const noexcept
{
    // Counts upward zero crossings in the most recent audio: cheap, and close enough to place
    // the FM sidebands musically.
    const auto length = juce::jmin ((int) (pitchEstimateSeconds * sampleRate), fmInput.getCapacity() - 1);
    int crossings = 0;

    for (int i = length; i > 0; --i)
        if (fmInput.back (i) < 0.0f && fmInput.back (i - 1) >= 0.0f)
            ++crossings;

    if (crossings < 2)
        return 220.0f;

    return juce::jlimit (40.0f, 2000.0f, (float) crossings / (float) pitchEstimateSeconds);
}

} // namespace astralay::dsp
