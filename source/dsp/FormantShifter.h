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
    of trips round it that is at least a frame. Only runs while a glitch is active. Startup
    analysis is scheduled across a hop; the caller keeps dry audio until ready, then fades wet in.
*/
class FormantShifter
{
public:
    enum class Method { lpc, cepstral };

    static constexpr int scheduleSlots = 32; // Two shifters on each of sixteen taps.
    static constexpr int clockPeriod = 2048; // Largest hop, for the maximum 8192-point frame.
    void prepare (double sampleRate, Method method, int scheduleSlot = 0);
    void reset();

    /** Records input. The engine supplies its shared sample clock; -1 advances a local clock. */
    void push (float sample, int sampleClock = -1) noexcept
    {
        history.push (sample);
        frameClock = (sampleClock >= 0 ? sampleClock : frameClock + 1) & (hopSize - 1);
    }

    /** Begins shifting by the given number of semitones. loopSamples is the length of the feedback
        loop the shifter sits in, or 0 for none. In a loop of up to two frames, the output is lined
        up with the loop's own repeats.
    */
    void start (float semitones, float loopSamples = 0.0f);

    /** Changes the shift for the frames still to come. */
    void setShift (float semitones) noexcept { shift = semitones; }

    /** How long ago the audio at the middle of the next frame was pushed, in samples. */
    int getFrameCentreLag() const noexcept
    {
        return lookBack + frameSize / 2 + (primed ? 0 : (primeFramesLeft - 1) * (hopSize - primeSpacing));
    }

    /** Call once per sample after push(), while active. Returns zero during scheduled warm-up;
        multiply the wet crossfade (and its metadata) by getStartupGain(). */
    float next() noexcept;

    int getFrameSize() const noexcept { return frameSize; }
    int getMaxWarmupSamples() const noexcept { return hopSize + 3 * primeSpacing; }
    float getStartupGain() const noexcept { return startupGain; }
    /** Work counter for verifying scheduling bounds; reset() starts it over. */
    juce::uint64 getFrameCount() const noexcept { return frameCount; }

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
    int schedulePhase = 0, primeSpacing = 1, frameClock = 0, primeFramesLeft = 4;
    int startupSamples = 0, startupFadeSamples = 1;
    float startupGain = 0.0f;
    juce::uint64 frameCount = 0;
};

} // namespace astralay::dsp
