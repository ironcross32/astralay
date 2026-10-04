#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

namespace astralay::dsp
{

/** Removes a constant offset: a 6 dB per octave high-pass at 5 Hz, far enough below hearing to
    leave the audio alone. A frozen loop has nothing in it to remove an offset, and some glitches
    wear a short loop down to nothing else, so this sits on the taps' output rather than in the
    loop, where even this much would thin a short loop over its hundreds of passes a second.
*/
class DcBlocker
{
public:
    void prepare (double sampleRate) noexcept
    {
        constexpr double cutoffHz = 5.0;
        pole = std::exp (-juce::MathConstants<double>::twoPi * cutoffHz / sampleRate);
        reset();
    }

    void reset() noexcept { lastInput = lastOutput = 0.0; }

    float process (float input) noexcept
    {
        lastOutput = input - lastInput + pole * lastOutput;
        lastInput = input;
        return (float) lastOutput;
    }

private:
    double pole = 0.0, lastInput = 0.0, lastOutput = 0.0;
};

} // namespace astralay::dsp
