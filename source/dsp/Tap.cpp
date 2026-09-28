#include "Tap.h"
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
    lowCut.reset();
    highCut.reset();
    enabledGain.setCurrentAndTargetValue (enabled ? 1.0f : 0.0f);
    idle = ! enabled;
}

void Tap::setSettings (const TapSettings& s, float glideSeconds)
{
    const auto target = juce::jlimit (DelayLine::minDelaySamples, line.getMaxDelay(), s.delaySamples);
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
    const auto fade = enabledGain.getNextValue();
    const auto delayed = line.read (delay.getNextValue());

    // Output.
    const auto out = delayed * gain.getNextValue() * fade;
    left  += out * leftGain.getNextValue();
    right += out * rightGain.getNextValue();

    // Feedback path. Freeze raises the feedback to unity and crossfades the filters and clipper out.
    const auto fb = feedback.getNextValue();
    const auto looped = delayed * (fb + (1.0f - fb) * freeze);
    const auto shaped = softClip (highCut.processSample (0, lowCut.processSample (0, looped)));
    const auto returned = shaped + (looped - shaped) * freeze;

    line.push ((input * (1.0f - freeze) + returned) * fade);
}

void Tap::endBlock() noexcept
{
    lowCut.snapToZero();
    highCut.snapToZero();

    if (! enabled && ! enabledGain.isSmoothing() && ! idle)
    {
        // Fully faded out: clear the line so re-enabling starts silent rather than replaying old audio.
        line.clear();
        lowCut.reset();
        highCut.reset();
        idle = true;
    }
}

} // namespace astralay::dsp
