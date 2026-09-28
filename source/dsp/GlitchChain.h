#pragma once

#include "Glitch.h"

namespace astralay::dsp
{

/** The glitches for one tap.

    At each chunk boundary the chain rolls every glitch type; each one that fires runs for a whole
    number of chunks with values picked from its ranges, fading in and out to avoid clicks. Glitches
    are processed in series in GlitchType order. Those that work on past audio keep a history of
    their own input, so each one hears the output of the glitches before it.

    The LPC and cepstral formant glitches are not implemented yet and never fire.
*/
class GlitchChain
{
public:
    void prepare (double sampleRate);
    void reset();
    void reseed (juce::int64 seed);

    void setSettings (const TapGlitchSettings& tapSettings, const GlitchGlobalSettings& globalSettings);

    /** Rolls for new glitches. Call at each chunk boundary, before processing that sample. */
    void onChunkBoundary();

    /** Processes one sample. Always call it, even with no glitches running, so histories fill. */
    float process (float input) noexcept;

    int getNumActive() const noexcept;
    bool isActive (GlitchType type) const noexcept { return slots[(size_t) type].active; }

    /** Starts a glitch immediately, for tests. */
    void startForTesting (GlitchType type) { start (type); }

private:
    struct Slot
    {
        bool active = false;
        int elapsed = 0;
        int length = 0;
        int fade = 0;
    };

    struct Voice
    {
        bool active = false;
        int samplesAgo = 0;
        int position = 0;
        int size = 0;
    };

    static constexpr int maxGrainVoices = 32;

    void start (GlitchType type);
    float envelope (GlitchType type) noexcept;
    float applyStage (GlitchType type, float dry, float wet) noexcept;

    float reverse() noexcept;
    float stutter() noexcept;
    float granularize() noexcept;
    float pitchShift() noexcept;
    float ringModulate (float input) noexcept;
    float frequencyModulate() noexcept;
    float bitCrush (float input) noexcept;

    float estimateFundamental() const noexcept;

    double sampleRate = 48000.0;
    juce::Random random;
    TapGlitchSettings settings;
    GlitchGlobalSettings global;
    std::array<Slot, numGlitchTypes> slots;

    HistoryBuffer reverseInput, stutterInput, grainInput, pitchInput, fmInput;

    // Reverse
    int reverseSegment = 0, reversePosition = 0, reverseCap = 0;

    // Stutter
    std::vector<float> slice;
    int sliceLength = 0, slicePosition = 0;

    // Granularize
    std::array<Voice, maxGrainVoices> voices;
    int grainSize = 0, grainSpread = 0;
    float grainInterval = 1.0f, grainCountdown = 0.0f, grainGain = 1.0f;

    // Pitch
    float pitchRatio = 1.0f, pitchPhase = 0.0f, pitchWindow = 1.0f;

    // Ring modulation
    float ringFrequency = 100.0f, ringPhase = 0.0f;

    // Frequency modulation
    float fmFrequency = 100.0f, fmPhase = 0.0f, fmDepth = 0.0f, fmCentre = 2.0f;

    // Bit crusher
    float crushLevels = 128.0f, crushRate = 1.0f, crushCounter = 0.0f, crushHeld = 0.0f;
};

} // namespace astralay::dsp
