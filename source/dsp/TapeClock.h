#pragma once

#include <juce_core/juce_core.h>

namespace astralay::dsp
{

/** The speed of the tape the engine runs on, for the tape stop.

    The speed runs from 1 (normal) to 0 (stopped) and moves in a straight line: the stop time is
    how long it takes to go from 1 to 0 and the start time from 0 to 1, so a turn-round part of the
    way takes its share of either. Call advance() once per output sample; it says whether the tape
    reaches its next sample, which is when the engine should process one.

    The output is read between the tape's last two samples, at getFraction() of the way from the
    older to the newer. At full speed that is exactly the newer one. After a slow-down the tape
    comes back to full speed a fraction of a sample behind, and makes that up over a few
    milliseconds at a speed imperceptibly above 1.
*/
class TapeClock
{
public:
    void prepare (double newSampleRate) noexcept { sampleRate = newSampleRate; }

    /** Jumps to stopped or to full speed. */
    void reset (bool stopped) noexcept
    {
        target = stopped ? 0.0 : 1.0;
        speed = target;
        phase = 1.0;
    }

    void setSettings (bool stopped, float stopSeconds, float startSeconds) noexcept
    {
        target = stopped ? 0.0 : 1.0;
        stopStep = 1.0 / juce::jmax (1.0, (double) stopSeconds * sampleRate);
        startStep = 1.0 / juce::jmax (1.0, (double) startSeconds * sampleRate);
    }

    /** Moves on by one output sample. Returns true if the tape reached its next sample. */
    bool advance() noexcept
    {
        if (speed < target)
            speed = juce::jmin (target, speed + startStep);
        else if (speed > target)
            speed = juce::jmax (target, speed - stopStep);

        if (speed >= 1.0)
        {
            phase = juce::jmin (1.0, phase + catchUpStep);
            return true;
        }

        if (speed <= 0.0)
            return false;

        phase += speed;

        if (phase <= 1.0)
            return false;

        phase -= 1.0;
        return true;
    }

    /** True when the tape runs at normal speed and the output is its newest sample. */
    bool isAtFullSpeed() const noexcept { return speed >= 1.0 && phase >= 1.0; }

    /** True from the moment the speed leaves 1 until it is back there. */
    bool isSlowed() const noexcept      { return speed < 1.0; }
    bool isStopped() const noexcept     { return speed <= 0.0; }

    double getSpeed() const noexcept    { return speed; }
    float getFraction() const noexcept  { return (float) phase; }

    /** The level of the output: 1, falling to 0 over the last of the speed, so that a stopped
        tape is silent rather than holding whatever level it stopped on.
    */
    float getGain() const noexcept
    {
        return speed >= fadeSpeed ? 1.0f : (float) (speed / fadeSpeed);
    }

    /** How much of the input is recorded. Whatever goes onto slow tape comes back, once the tape
        is fast again, sharp by as much as the tape was slow, and what was recorded near a stop
        comes back as a squeal. So the input fades as the tape slows, by the square of how far the
        speed is above half: all of it at full speed, a third of it 4 semitones sharp, a tenth 7
        semitones sharp, and nothing from half speed down, which would be an octave or more.
    */
    float getRecordGain() const noexcept
    {
        const auto above = juce::jlimit (0.0, 1.0, (speed - recordFloorSpeed) / (1.0 - recordFloorSpeed));
        return (float) (above * above);
    }

private:
    static constexpr double fadeSpeed = 0.06;
    static constexpr double recordFloorSpeed = 0.5;
    static constexpr double catchUpStep = 0.002;

    double sampleRate = 44100.0;
    double speed = 1.0, target = 1.0;
    double stopStep = 0.0, startStep = 0.0;
    double phase = 1.0;   // In (0, 1].
};

} // namespace astralay::dsp
