#pragma once

#include "Glitch.h"
#include "RealFft.h"

namespace astralay::dsp
{

/** Shifts formants without changing pitch, using short-time Fourier analysis with overlap-add.

    Each frame's spectral envelope is estimated, stretched by the shift ratio, and imposed on the
    frame in place of the original envelope, keeping the fine structure (and so the pitch). The two
    methods differ only in how the envelope is estimated:
    - LPC: linear prediction, a coarse, vocoder-like envelope with a grittier sound.
    - Cepstral: a smoothed log spectrum, closer to the original timbre.

    The output lags the input by one frame (about 21 ms), or in a short loop by the whole number
    of trips round it that is at least a frame. Only runs while a glitch is active; on start it
    analyses recent history so there is no silent gap.
*/
class FormantShifter
{
public:
    enum class Method { lpc, cepstral };

    void prepare (double sampleRate, Method method);
    void reset();

    /** Records input. Call for every sample, active or not. */
    void push (float sample) noexcept { history.push (sample); }

    /** Begins shifting by the given number of semitones. loopSamples is the length of the feedback
        loop the shifter sits in, or 0 for none. In a loop of up to two frames, the output is lined
        up with the loop's own repeats.
    */
    void start (float semitones, float loopSamples = 0.0f);

    /** Changes the shift for the frames still to come. */
    void setShift (float semitones) noexcept { shift = semitones; }

    /** How long ago the audio at the middle of the next frame was pushed, in samples. */
    int getFrameCentreLag() const noexcept { return lookBack + frameSize / 2; }

    /** Returns the next output sample. Call once per sample, after push(), while active. */
    float next() noexcept;

    int getFrameSize() const noexcept { return frameSize; }

private:
    void addFrame (int endsSamplesAgo, int outputOffset) noexcept;
    void estimateEnvelope() noexcept;
    void estimateLpcEnvelope() noexcept;
    void estimateCepstralEnvelope() noexcept;

    Method method = Method::cepstral;
    std::unique_ptr<RealFft> fft;
    int frameSize = 1024, hopSize = 256, bins = 513, lpcOrder = 24, lifter = 48;
    int maxAlignedLoop = 2048, lookBack = 0;
    float overlapScale = 1.0f;
    float shift = 0.0f;   // Semitones.

    HistoryBuffer history;
    std::vector<float> window, spectrum, work, magnitude, envelope, gains;
    std::vector<double> lpcCoefficients, autocorrelation;
    std::vector<float> output;
    int outputMask = 0, readPosition = 0, samplesSinceHop = 0;
    int validOutputSamples = 0; // Contiguous pending overlap-add samples starting at readPosition.
    bool primed = false;
};

} // namespace astralay::dsp
