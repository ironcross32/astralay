#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DelayLine.h"
#include "GlitchChain.h"
#include "FreezeSustain.h"

namespace astralay::dsp
{

/** Block-rate settings for one tap, already converted to DSP units. */
struct TapSettings
{
    bool enabled = false;
    float delaySamples = 0.0f;
    float gain = 1.0f;          // Linear output gain (volume).
    float pan = 0.0f;           // -1 (left) to 1 (right).
    float feedback = 0.4f;      // 0 to 1, where 1 is unity.
    float lowCutHz = 20.0f;
    float highCutHz = 20000.0f;
    TapGlitchSettings glitch;
};

/** One delay tap: its own delay line and feedback loop.

    Per sample: the delayed signal is read (gliding towards the target delay time) and run through
    the tap's glitches. The output hears the glitched signal when glitch placement is "output and
    feedback", or the clean one otherwise. The glitched signal is fed back through the low cut,
    high cut and soft-clipper into the delay line together with the new input. Freeze fades the
    input out, raises the feedback to unity and bypasses the filters and clipper.
*/
class Tap
{
public:
    void prepare (double sampleRate, int maxDelaySamples, int tapIndex = 0);
    void reset();

    /** Call once per block before processing. glideSeconds is the global glide time. */
    void setSettings (const TapSettings& settings, float glideSeconds, const GlitchGlobalSettings& glitchGlobal);

    /** Rolls for new glitches at a chunk boundary, which comes before that sample is processed.
        freeze is as for process().
    */
    void onChunkBoundary (float freeze)
    {
        glitches.setLoop ((float) delay.getCurrentValue(), loopGainFor (feedback.getCurrentValue(), freeze));
        glitches.onChunkBoundary();
    }

    /** Restarts the tap's random sequence and stops its glitches. */
    void restartGlitches (juce::int64 seed);

    const GlitchChain& getGlitches() const noexcept { return glitches; }

    /** Reports this tap to a diagnostic log, or to none with a null sink. Call after prepare(). */
    void setDiagnostics (diagnostics::Sink* sink, int tapIndex) noexcept { glitches.getProbe().attach (sink, tapIndex, sampleRate); }

    /** True when the tap is off and fully faded out, so processing can be skipped. */
    bool isIdle() const noexcept { return idle; }

    /** Processes one sample and adds the tap's panned output to left and right.
        freeze runs from 0 (off) to 1 (fully frozen).
    */
    void process (float input, float freeze, float& left, float& right, bool sustain = false, int formantClock = -1) noexcept;

    /** Call at the end of each block. Flushes filter denormals and finishes fading out. */
    void endBlock() noexcept;

private:
    /** How much of the loop survives each trip round. Freeze raises the feedback to unity. */
    static float loopGainFor (float feedbackGain, float freeze) noexcept { return feedbackGain + (1.0f - feedbackGain) * freeze; }

    float scaledDelay() const noexcept
    {
        return juce::jlimit (DelayLine::minDelaySamples, maxDelay, baseDelay * delayScale);
    }

    DelayLine line;
    GlitchChain glitches;
    FreezeSustain freezeSustain;
    float baseDelay = 0.0f, delayScale = 1.0f, maxDelay = 0.0f;
    bool glitchesHeard = false;
    juce::dsp::StateVariableTPTFilter<float> lowCut, highCut;

    /** The tap's delay time on its way to a new value, in a straight line that takes the glide time.

        In double precision: on a long tap, one sample's share of a glide is too small for a float
        to add to the delay, which then stays put and jumps at the end of the glide.

        A change of glide time during a glide keeps the glide going, with the same share of its
        time left. A juce::SmoothedValue has to be reset to change its time, which sends it
        straight to its target.
    */
    class DelayGlide
    {
    public:
        void setGlideSamples (double newGlideSamples) noexcept
        {
            if (remaining > 0 && glideSamples > 0.0)
            {
                remaining = juce::jmax (1, juce::roundToInt ((double) remaining * newGlideSamples / glideSamples));
                step = (target - current) / (double) remaining;
            }

            glideSamples = newGlideSamples;
        }

        void setCurrentAndTargetValue (double value) noexcept
        {
            current = target = value;
            remaining = 0;
        }

        void setTargetValue (double value) noexcept
        {
            if (juce::exactlyEqual (value, target))
                return;

            target = value;
            remaining = (int) glideSamples;

            if (remaining <= 0)
                current = target;
            else
                step = (target - current) / (double) remaining;
        }

        double getNextValue() noexcept
        {
            if (remaining > 0)
                current = --remaining == 0 ? target : current + step;

            return current;
        }

        double getCurrentValue() const noexcept { return current; }
        bool isSmoothing() const noexcept { return remaining > 0; }

    private:
        double current = 0.0, target = 0.0, step = 0.0, glideSamples = 0.0;
        int remaining = 0;
    };

    DelayGlide delay;
    juce::SmoothedValue<float> gain, feedback, leftGain, rightGain, enabledGain;

    double sampleRate = 44100.0;
    bool enabled = false;
    bool idle = true;
};

} // namespace astralay::dsp
