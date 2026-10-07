#include "GlitchChain.h"

namespace astralay::dsp
{

namespace
{
    constexpr double fadeSeconds = 0.005;
    constexpr int boundarySlackSamples = 8;   // How far a glitch's count may drift from the chunk grid.
    constexpr double reverseCapSeconds = 2.0;
    constexpr double sliceCapSeconds = 2.0;
    constexpr double grainHistorySeconds = 1.0;
    constexpr double grainSpreadSeconds = 0.5;
    constexpr double pitchWindowSeconds = 0.04;
    constexpr double pitchLevelSeconds = 0.005;
    constexpr double pitchMeanSeconds = 0.05;
    constexpr float wallMargin = 0.05f;   // Semitones from a wall at which a sweep counts as there.
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

void GlitchChain::prepare (double newSampleRate, double maxLoopSeconds, int tapIndex)
{
    sampleRate = newSampleRate;

    const auto samples = [this] (double seconds) { return (int) std::ceil (seconds * sampleRate); };

    reverseCap = samples (reverseCapSeconds);
    reverseInput.prepare (2 * reverseCap + 4);
    stutterInput.prepare (samples (sliceCapSeconds));
    grainInput.prepare (samples (grainHistorySeconds));
    pitchInput.prepare (samples (2.0 * pitchWindowSeconds) + 4 + sinc::kernelSize);
    pitchTrack.prepare (samples (2.0 * pitchWindowSeconds) + 4);
    loopTrack.assign ((size_t) (samples (maxLoopSeconds) / loopTrackStep + 4), 0.0f);
    formantLoopTrack.assign (loopTrack.size(), 0.0f);
    formantTrackingSpan = (int) formantLoopTrack.size() * loopTrackStep;
    pitchSmoothing = 1.0f - std::exp (-1.0f / (float) (pitchLevelSeconds * sampleRate));
    pitchMeanSmoothing = 1.0f - std::exp (-1.0f / (float) (pitchMeanSeconds * sampleRate));
    fmInput.prepare (samples (fmHistorySeconds));

    slice.assign ((size_t) samples (sliceCapSeconds), 0.0f);
    pitchWindow = (float) (pitchWindowSeconds * sampleRate);

    lpcShifter.prepare (sampleRate, FormantShifter::Method::lpc, 2 * tapIndex);
    cepstralShifter.prepare (sampleRate, FormantShifter::Method::cepstral, 2 * tapIndex + 1);

    // As far back as the middle of a frame taken from two frames back in a short loop.
    lpcTrack.prepare (3 * lpcShifter.getFrameSize() + 8);
    cepstralTrack.prepare (3 * cepstralShifter.getFrameSize() + 8);

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

    pitchTrack.clear();
    lpcTrack.clear();
    cepstralTrack.clear();
    validLoopEntries = 0;
    formantOffset = 0.0f;
    formantTrackingLeft = 0;
    pitchOffset = pitchArriving = 0.0f;
    pitchMean = 0.0f;
    grainMean = 0.0f;
    pitchHoming = false;
}

float GlitchChain::readLoopTrack (const std::vector<float>& track) const noexcept
{
    return readLoopTrack (track, loopSamples);
}

float GlitchChain::readLoopTrack (const std::vector<float>& track, float samplesAgo) const noexcept
{
    const auto size = (int) track.size();
    const auto back = juce::jmax (1, juce::roundToInt (juce::jmin (samplesAgo, 1.0e8f) / (float) loopTrackStep));

    // A loop longer than the track is no loop at all, as when setLoop() was never called.
    if (back >= size || back >= validLoopEntries)
        return 0.0f;

    return track[(size_t) ((loopTrackIndex - back + size) % size)];
}

void GlitchChain::writeLoopTracks (float pitchSemitones, float formantSemitones) noexcept
{
    if (++loopTrackCount < loopTrackStep)
        return;

    loopTrackCount = 0;
    loopTrackIndex = (loopTrackIndex + 1) % (int) loopTrack.size();
    loopTrack[(size_t) loopTrackIndex] = pitchSemitones;
    formantLoopTrack[(size_t) loopTrackIndex] = formantSemitones;
    validLoopEntries = juce::jmin (validLoopEntries + 1, (int) loopTrack.size());
}

void GlitchChain::setLoop (float newLoopSamples, float newLoopGain) noexcept
{
    loopSamples = juce::jlimit (1.0f, 1.0e9f, newLoopSamples);
    loopGain = juce::jlimit (0.0f, 1.0f, newLoopGain);
}

void GlitchChain::reseed (juce::int64 seed)
{
    random.setSeed (seed);
}

void GlitchChain::setSettings (const TapGlitchSettings& tapSettings, const GlitchGlobalSettings& globalSettings)
{
    settings = tapSettings;
    global = globalSettings;

    if (! global.stopped)
        return;

    // Cut each running glitch down to its fade-out, or to as much of one as it has faded in.
    for (auto& slot : slots)
        if (slot.active)
            slot.length = juce::jmin (slot.length, slot.elapsed + juce::jmin (slot.fade, slot.elapsed));
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
    // A glitch lasts a whole number of chunks and is counted out in samples. With host sync on,
    // the chunks follow the host's beats and are rarely a whole number of samples long, so the
    // count drifts off the boundaries by a sample or so. A glitch counted a fraction too long was
    // still running at the boundary it should have ended on, and couldn't fire again there. So at
    // each boundary a running glitch's remaining time is set back to a whole number of chunks,
    // and one with none left ends here. A glitch further off than that is left to run out: its
    // chunk length has changed under it, and cutting it short would click.
    const auto chunk = juce::jmax (1, global.chunkSamples);

    for (auto& slot : slots)
    {
        if (! slot.active)
            continue;

        const auto remaining = slot.length - slot.elapsed;
        const auto chunksLeft = (remaining + chunk / 2) / chunk;
        const auto drift = remaining - chunksLeft * chunk;

        if (std::abs (drift) > boundarySlackSamples)
            continue;

        if (chunksLeft == 0)
            slot.active = false;
        else
            slot.length -= drift;
    }

    for (int i = 0; i < numGlitchTypes; ++i)
    {
        const auto type = (GlitchType) i;

        // Always draw, so the random sequence doesn't depend on which glitches are running.
        const auto roll = random.nextFloat();

        if (global.stopped || slots[(size_t) i].active || getNumActive() >= global.maxSimultaneous)
            continue;

        if (roll < settings.probability[(size_t) i])
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

    // What was picked, for the diagnostic log.
    diagnostics::Picks picks;
    using diagnostics::picked;

    switch (type)
    {
        case GlitchType::reverse:
            reverseSegment = juce::jlimit (16, reverseCap, global.chunkSamples);
            reversePosition = 0;
            picks[0].value = picks[0].min = picks[0].max = (float) reverseSegment;
            break;

        case GlitchType::stutter:
        {
            sliceLength = juce::jlimit (16, (int) slice.size(), juce::roundToInt (settings.stutterSlice.pick (random)));
            sliceLength = juce::jmin (sliceLength, stutterInput.getCapacity());
            picks[0] = picked ((float) sliceLength, settings.stutterSlice);

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

            // How many grains sound at once, on average. Grains taken from different places are
            // unrelated, so they add in power: a Hann grain's power averages 0.375. A constant
            // offset is the same in every grain and adds in amplitude, where a Hann grain
            // averages 0.5. granularize() sets the gain from these.
            const auto overlap = juce::jmin (density * (float) grainSize / (float) sampleRate, (float) maxGrainVoices);
            grainPowerSum = std::sqrt (overlap * 0.375f);
            grainAmplitudeSum = overlap * 0.5f;

            for (auto& voice : voices)
                voice.active = false;

            picks = { picked ((float) grainSize, settings.grainSize), picked (density, settings.grainDensity) };
            break;
        }

        case GlitchType::pitch:
        {
            pitchIsVarispeed = settings.varispeed;

            if (pitchIsVarispeed)
            {
                // A faster tape is a shorter delay.
                const auto semitones = settings.pitch.pick (random);
                varispeedScale = std::pow (2.0f, -semitones / 12.0f);
                picks[0] = picked (semitones, settings.pitch);
                break;
            }

            pitchSpeed = juce::jmax (0.0f, settings.pitchSpeed.pick (random));
            pitchUpRatio = std::pow (2.0f, pitchSpeed / 12.0f);
            pitchDownRatio = 1.0f / pitchUpRatio;

            // Which way: back into the range if the pitch is outside it, otherwise either way, with
            // the odds leaning towards the middle while homing. The lean is strongest at a wall and
            // gone by the edge of the dead zone round the middle.
            const auto roll = random.nextFloat();
            const auto walls = pitchWalls();
            const auto distance = std::abs (pitchOffset - walls.centre);
            const auto towardsCentre = pitchOffset > walls.centre ? -1 : 1;

            if (pitchOffset < walls.low || pitchOffset > walls.high)
            {
                pitchDirection = towardsCentre;
            }
            else
            {
                const auto reach = (walls.high - walls.low) * 0.5f - walls.deadZone;
                const auto lean = pitchHoming && reach > 0.0f ? juce::jlimit (0.0f, 1.0f, (distance - walls.deadZone) / reach) : 0.0f;
                pitchDirection = roll < 0.5f + 0.4f * lean ? towardsCentre : -towardsCentre;
            }

            pitchRatio = pitchDirection > 0 ? pitchUpRatio : pitchDownRatio;
            pitchPhase = 0.0f;
            pitchHeads.reset();

            // A short loop repeats itself. Sweeping a window twice a whole number of loop lengths
            // puts the two read heads a whole number of repeats apart, on identical audio, so
            // their crossfade is seamless and loses nothing.
            pitchSpan = pitchWindow;

            if (loopSamples <= pitchWindow)
                pitchSpan = 2.0f * loopSamples * juce::jmax (1.0f, std::round (pitchWindow / (2.0f * loopSamples)));

            picks = { picked ((float) pitchDirection * pitchSpeed, settings.pitchSpeed),
                      diagnostics::Pick { pitchOffset, pitchOffset, pitchOffset } };
            break;
        }

        case GlitchType::ringModulation:
            ringFrequency = settings.ringFrequency.pick (random);
            ringPhase = 0.0f;
            picks[0] = picked (ringFrequency, settings.ringFrequency);
            break;

        case GlitchType::frequencyModulation:
        {
            const auto fundamental = estimateFundamental();
            const auto ratio = settings.fmRatio.pick (random);
            fmFrequency = ratio * fundamental;

            // A phase deviation of index radians at the fundamental.
            const auto index = settings.fmIndex.pick (random);
            const auto depthSeconds = index / (twoPi * fundamental);
            fmDepth = (float) juce::jmin ((double) depthSeconds, fmMaxDepthSeconds) * (float) sampleRate;
            fmCentre = fmDepth + (float) HistoryBuffer::audioReadMargin + 1.0f;
            fmPhase = 0.0f;
            picks = { picked (ratio, settings.fmRatio), picked (index, settings.fmIndex) };
            break;
        }

        case GlitchType::bitCrusher:
        {
            const auto bits = juce::jlimit (1, 16, settings.bits.pickInt (random));
            crushLevels = std::pow (2.0f, (float) (bits - 1));
            crushRate = juce::jmax (1.0f, settings.rateReduction.pick (random));
            crushCounter = crushRate;
            picks = { picked ((float) bits, settings.bits), picked (crushRate, settings.rateReduction) };
            break;
        }

        case GlitchType::lpcFormant:
        {
            lpcTarget = settings.lpcShift.pick (random);
            lpcShifter.start (lpcTarget, loopSamples);
            picks = { picked (lpcTarget, settings.lpcShift), diagnostics::Pick { formantOffset, formantOffset, formantOffset } };
            break;
        }

        case GlitchType::cepstralFormant:
        {
            cepstralTarget = settings.cepstralShift.pick (random);
            cepstralShifter.start (cepstralTarget, loopSamples);
            picks = { picked (cepstralTarget, settings.cepstralShift), diagnostics::Pick { formantOffset, formantOffset, formantOffset } };
            break;
        }
    }

    if (type == GlitchType::pitch)
        pitchEdges.reset();

    probe.glitchStarted (type, slot.length, picks, type == GlitchType::pitch && pitchIsVarispeed);
}

GlitchChain::PitchWalls GlitchChain::pitchWalls() const noexcept
{
    const auto low = juce::jmin (settings.pitch.min, settings.pitch.max);
    const auto high = juce::jmax (settings.pitch.min, settings.pitch.max);

    // The dead zone is the middle fifth of the range.
    return { low, high, (low + high) * 0.5f, (high - low) * 0.1f };
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
    const auto y = dry + (wet - dry) * envelope (type);
    probe.stage (type, y);
    return y;
}

float GlitchChain::applyPitchStage (float dry, float wet) noexcept
{
    const auto gain = envelope (GlitchType::pitch);
    pitchOffset = pitchArriving + (pitchWetOffset - pitchArriving) * gain;

    const auto y = levelMix (wet, gain, dry, pitchEdges);
    probe.stage (GlitchType::pitch, y);
    return y;
}

float GlitchChain::applyFormantStage (GlitchType type, FormantShifter& shifter, const HistoryBuffer& track, float target, float dry) noexcept
{
    // Each frame is shifted by what is left between the audio in it and the target, so audio that
    // has already been round the loop and moved passes through as it is.
    shifter.setShift (target - track.back (shifter.getFrameCentreLag()));

    const auto wet = shifter.next();
    const auto gain = envelope (type) * shifter.getStartupGain();
    formantOffset += (target - formantOffset) * gain;

    const auto y = dry + (wet - dry) * gain;
    probe.stage (type, y);
    return y;
}

float GlitchChain::process (float input, float restoredAmount, float restoredPitch, float restoredFormant, int formantClock) noexcept
{
    auto y = input;

    reverseInput.push (y);
    if (isActive (GlitchType::reverse))
        y = applyStage (GlitchType::reverse, y, reverse());

    stutterInput.push (y);
    if (isActive (GlitchType::stutter))
        y = applyStage (GlitchType::stutter, y, stutter());

    grainMean += pitchMeanSmoothing * (y - grainMean);
    grainInput.push (y);
    if (isActive (GlitchType::granularize))
        y = applyStage (GlitchType::granularize, y, granularize());

    const auto pitching = isActive (GlitchType::pitch);
    const auto sweeping = pitching && ! pitchIsVarispeed;

    // The pitch of the audio arriving: what left here one trip round the loop ago, pulled towards
    // the original pitch by however much new input has been mixed in since.
    pitchArriving = loopGain * readLoopTrack (loopTrack);
    pitchOffset = pitchArriving;

    pitchMean += pitchMeanSmoothing * (y - pitchMean);
    pitchInput.push (y);
    pitchTrack.push (pitchArriving);

    if (sweeping)
    {
        y = applyPitchStage (y, pitchShift());

        if (const auto walls = pitchWalls(); pitchHoming && std::abs (pitchOffset - walls.centre) <= walls.deadZone)
            pitchHoming = false;
    }
    else if (pitching)
    {
        // Varispeed changes nothing here; the tap bends its delay time. This just runs the clock.
        envelope (GlitchType::pitch);
        probe.stage (GlitchType::pitch, y);
    }

    probe.setPitchOffset (pitchOffset);

    // Where the formants of the audio arriving sit: where they were left one trip round the loop
    // ago, pulled back towards their own place by however much new input has been mixed in since.
    const auto shiftingFormants = isActive (GlitchType::lpcFormant) || isActive (GlitchType::cepstralFormant);
    const auto trackingFormants = shiftingFormants || formantTrackingLeft > 0;

    formantOffset = trackingFormants ? loopGain * readLoopTrack (formantLoopTrack) : 0.0f;

    // The formant shifters line their output up with a short loop's repeats, so a plain fade loses
    // next to nothing there. Scaling the fade up to keep its level, as the pitch stage does, favours
    // whichever frequencies the two signals share, and round a loop those frequencies keep growing.
    if (trackingFormants)
        lpcTrack.push (formantOffset);

    lpcShifter.push (y, formantClock);
    if (isActive (GlitchType::lpcFormant))
        y = applyFormantStage (GlitchType::lpcFormant, lpcShifter, lpcTrack, lpcTarget, y);

    if (trackingFormants)
        cepstralTrack.push (formantOffset);

    cepstralShifter.push (y, formantClock);
    if (isActive (GlitchType::cepstralFormant))
        y = applyFormantStage (GlitchType::cepstralFormant, cepstralShifter, cepstralTrack, cepstralTarget, y);

    if (trackingFormants)
        formantTrackingLeft = shiftingFormants || std::abs (formantOffset) > 1.0e-4f ? formantTrackingSpan : formantTrackingLeft - 1;

    // The tap blends protected audio in after the chain. Its metadata must travel back with
    // that mixture too; otherwise a shifter would mistake restored audio for already shifted audio.
    if (restoredAmount > 0.0f)
    {
        pitchOffset += restoredAmount * (restoredPitch - pitchOffset);
        formantOffset += restoredAmount * (restoredFormant - formantOffset);
        if (std::abs (formantOffset) > 1.0e-4f)
            formantTrackingLeft = formantTrackingSpan;
    }
    writeLoopTracks (pitchOffset, formantOffset);

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

    auto wet = 0.0f, windows = 0.0f;

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        // Reading forward at normal speed from a point in the past keeps a constant distance behind.
        const auto window = 0.5f - 0.5f * std::cos (twoPi * (float) voice.position / (float) voice.size);
        wet += grainInput.back (voice.samplesAgo) * window;
        windows += window;

        if (++voice.position >= voice.size)
            voice.active = false;
    }

    // Overlapping grains are turned down to the level of the audio they are made from: as grains
    // that add in power, which is right for unrelated grains heard once. In a feedback loop that
    // is too much. The grains are copies of the loop's own audio at different delays, and at the
    // frequencies where those copies line up they add in amplitude; with more coming back than
    // went in, those frequencies grow on every trip until the loop howls. So the gain is held to
    // what the loop can take, which for a loop that keeps everything is the gain for grains that
    // add in amplitude.
    const auto gain = 1.0f / juce::jmax (1.0f, grainPowerSum, loopGain * grainAmplitudeSum);

    // The offset the audio rides on is the same in every grain, so it always adds in amplitude.
    const auto offset = grainMean * windows;
    return (wet - offset) * gain + offset / juce::jmax (1.0f, grainAmplitudeSum);
}

float GlitchChain::pitchShift() noexcept
{
    // Two read heads sweep through a short window at the pitch ratio, crossfading so that one is
    // always away from the jump back.
    pitchPhase = wrap (pitchPhase + (1.0f - pitchRatio) / pitchSpan);
    const auto otherPhase = wrap (pitchPhase + 0.5f);

    const auto delayA = 1.0f + pitchPhase * pitchSpan;
    const auto delayB = 1.0f + otherPhase * pitchSpan;

    const auto s = std::sin (pi * pitchPhase);
    const auto gain = s * s;

    // The pitch of the audio the heads are reading, and how far that is from the wall ahead.
    const auto reading = pitchTrack.read (delayA) * gain + pitchTrack.read (delayB) * (1.0f - gain);
    const auto walls = pitchWalls();
    auto room = pitchDirection > 0 ? walls.high - reading : reading - walls.low;

    if (room <= wallMargin)
    {
        // At the wall: turn back, and favour the middle from now on.
        pitchDirection = -pitchDirection;
        pitchHoming = true;
        room = pitchDirection > 0 ? walls.high - reading : reading - walls.low;
    }

    // The step is cut short where a full one would carry the audio through the wall, so the sweep
    // lands on the wall, and the next time that audio comes round it turns back.
    const auto step = juce::jlimit (0.0f, pitchSpeed, room);

    pitchRatio = step < pitchSpeed ? std::exp2 ((float) pitchDirection * step / 12.0f)
                                   : (pitchDirection > 0 ? pitchUpRatio : pitchDownRatio);
    pitchWetOffset = reading + (float) pitchDirection * step;

    return levelMix (pitchInput.readAudio (delayA), gain, pitchInput.readAudio (delayB), pitchHeads);
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
    return fmInput.readAudio (fmCentre + fmDepth * std::sin (twoPi * fmPhase));
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
