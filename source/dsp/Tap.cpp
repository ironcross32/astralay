#include "Tap.h"
#include "Finite.h"
#include "SoftClip.h"

namespace astralay::dsp
{

namespace
{
    constexpr double parameterRampSeconds = 0.02;
    constexpr double enableRampSeconds = 0.02;
    constexpr float filterQ = 0.70710678f; // Butterworth, 12 dB per octave.
}

void Tap::prepare (double newSampleRate, int maxDelaySamples)
{
    sampleRate = newSampleRate;
    line.prepare (maxDelaySamples);
    glitches.prepare (sampleRate, (double) maxDelaySamples / sampleRate);

    const juce::dsp::ProcessSpec spec { sampleRate, 1, 1 };

    lowCut.prepare (spec);
    lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowCut.setResonance (filterQ);

    highCut.prepare (spec);
    highCut.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    highCut.setResonance (filterQ);

    for (auto* s : { &gain, &feedback, &leftGain, &rightGain })
        s->reset (sampleRate, parameterRampSeconds);

    enabledGain.reset (sampleRate, enableRampSeconds);
    currentGlideSeconds = -1.0f;

    reset();
}

void Tap::reset()
{
    line.clear();
    glitches.reset();
    lowCut.reset();
    highCut.reset();
    enabledGain.setCurrentAndTargetValue (enabled ? 1.0f : 0.0f);
    idle = ! enabled;
}

void Tap::restartGlitches (juce::int64 seed)
{
    glitches.reset();
    glitches.reseed (seed);
}

void Tap::setSettings (const TapSettings& s, float glideSeconds, const GlitchGlobalSettings& glitchGlobal)
{
    glitches.setSettings (s.glitch, glitchGlobal);
    glitchesHeard = glitchGlobal.outputAndFeedback;

    baseDelay = s.delaySamples;

    const auto target = (double) scaledDelay();
    const auto wasIdle = idle;

    if (s.enabled != enabled)
    {
        enabled = s.enabled;
        enabledGain.setTargetValue (enabled ? 1.0f : 0.0f);

        if (enabled)
            idle = false;
    }

    if (! juce::approximatelyEqual (glideSeconds, currentGlideSeconds))
    {
        // reset() snaps the smoother to its target, so only do it when the glide time changes.
        currentGlideSeconds = glideSeconds;
        delay.reset (sampleRate, (double) glideSeconds);
    }

    if (wasIdle || glideSeconds <= 0.0f)
        delay.setCurrentAndTargetValue (target);
    else
        delay.setTargetValue (target);

    // Constant-power pan law.
    const auto angle = (s.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

    gain.setTargetValue (s.gain);
    feedback.setTargetValue (juce::jlimit (0.0f, 1.0f, s.feedback));
    leftGain.setTargetValue (std::cos (angle));
    rightGain.setTargetValue (std::sin (angle));

    const auto nyquistLimit = (float) sampleRate * 0.45f;
    lowCut.setCutoffFrequency (juce::jmin (s.lowCutHz, nyquistLimit));
    highCut.setCutoffFrequency (juce::jmin (s.highCutHz, nyquistLimit));

    if (wasIdle)
    {
        for (auto* v : { &gain, &feedback, &leftGain, &rightGain })
            v->setCurrentAndTargetValue (v->getTargetValue());
    }
}

void Tap::process (float input, float freeze, float& left, float& right) noexcept
{
    // A varispeed pitch glitch changes the delay time, and the glide to it bends the pitch.
    if (const auto scale = glitches.getDelayScale(); ! juce::exactlyEqual (scale, delayScale))
    {
        delayScale = scale;
        delay.setTargetValue ((double) scaledDelay());
    }

    const auto fade = enabledGain.getNextValue();
    const auto fb = feedback.getNextValue();
    const auto loopGain = loopGainFor (fb, freeze);

    // A loop that keeps everything settles on a whole number of samples, which the delay line
    // reads back untouched. Between samples it has to interpolate, and even a good interpolator
    // takes a little off the top on each of the many passes such a loop makes.
    auto delayPosition = delay.getNextValue();

    if (loopGain > 0.999f && ! delay.isSmoothing())
        delayPosition = std::round (delayPosition);

    const auto delayed = line.read (delayPosition);
    const auto delaySamples = (float) delayPosition;

    glitches.setLoop (delaySamples, loopGain);
    const auto glitchOutput = glitches.process (delayed);

    // Nothing that isn't a number may reach the output or go back into the delay line, where it
    // would stay for good. The engine keeps such samples out of the input, so these guards only
    // act if something in the tap itself goes wrong, and then they clear whatever was holding it.
    const auto glitchFault = isNonFinite (glitchOutput);
    const auto glitched = glitchFault ? delayed : glitchOutput;

    if (glitchFault)
        glitches.reset();

    // Output.
    const auto out = (glitchesHeard ? glitched : delayed) * gain.getNextValue() * fade;
    left  += out * leftGain.getNextValue();
    right += out * rightGain.getNextValue();

    // Feedback path. Freeze crossfades the filters and clipper out.
    const auto looped = glitched * loopGain;
    const auto shaped = softClip (highCut.processSample (0, lowCut.processSample (0, looped)));
    const auto returned = shaped + (looped - shaped) * freeze;

    const auto written = (input * (1.0f - freeze) + returned) * fade;

    if (isNonFinite (written))
    {
        lowCut.reset();
        highCut.reset();
        line.push (0.0f);
    }
    else
    {
        line.push (written);
    }

    glitches.getProbe().sample (freeze, delaySamples, fb, delayed, glitchOutput, written);
}

void Tap::endBlock() noexcept
{
    lowCut.snapToZero();
    highCut.snapToZero();

    if (! enabled && ! enabledGain.isSmoothing() && ! idle)
    {
        // Fully faded out: clear the line so re-enabling starts silent rather than replaying old audio.
        line.clear();
        glitches.reset();
        lowCut.reset();
        highCut.reset();
        idle = true;
    }
}

} // namespace astralay::dsp
