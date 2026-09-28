#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DelayLine.h"
#include "GlitchChain.h"

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
    void prepare (double sampleRate, int maxDelaySamples);
    void reset();

    /** Call once per block before processing. glideSeconds is the global glide time. */
    void setSettings (const TapSettings& settings, float glideSeconds, const GlitchGlobalSettings& glitchGlobal);

    /** Rolls for new glitches at a chunk boundary. */
    void onChunkBoundary() { glitches.onChunkBoundary(); }

    /** Restarts the tap's random sequence and stops its glitches. */
    void restartGlitches (juce::int64 seed);

    const GlitchChain& getGlitches() const noexcept { return glitches; }

    /** True when the tap is off and fully faded out, so processing can be skipped. */
    bool isIdle() const noexcept { return idle; }

    /** Processes one sample and adds the tap's panned output to left and right.
        freeze runs from 0 (off) to 1 (fully frozen).
    */
    void process (float input, float freeze, float& left, float& right) noexcept;

    /** Call at the end of each block. Flushes filter denormals and finishes fading out. */
    void endBlock() noexcept;

private:
    DelayLine line;
    GlitchChain glitches;
    bool glitchesHeard = false;
    juce::dsp::StateVariableTPTFilter<float> lowCut, highCut;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delay;
    juce::SmoothedValue<float> gain, feedback, leftGain, rightGain, enabledGain;

    double sampleRate = 44100.0;
    float currentGlideSeconds = -1.0f;
    bool enabled = false;
    bool idle = true;
};

} // namespace astralay::dsp
