#pragma once

#include "Diagnostics.h"
#include "FormantShifter.h"
#include "Glitch.h"

namespace astralay::dsp
{

/** The glitches for one tap.

    The pitch glitch has two modes. A sweep shifts the audio on every pass through the tap, so in a
    feedback loop the pitch keeps moving for as long as the glitch runs. The chain keeps an estimate
    of where the loop's pitch has got to; when it reaches either end of the pitch range the sweep
    turns back, and after that sweeps favour the middle of the range until the pitch is back there.
    Varispeed leaves the audio alone and has the tap change its delay time instead.

    At each chunk boundary the chain rolls every glitch type; each one that fires runs for a whole
    number of chunks with values picked from its ranges, fading in and out to avoid clicks. Glitches
    are processed in series in GlitchType order. Those that work on past audio keep a history of
    their own input, so each one hears the output of the glitches before it.
*/
class GlitchChain
{
public:
    /** maxLoopSeconds is the longest feedback loop setLoop() will describe. */
    void prepare (double sampleRate, double maxLoopSeconds = 10.0);
    void reset();
    void reseed (juce::int64 seed);

    void setSettings (const TapGlitchSettings& tapSettings, const GlitchGlobalSettings& globalSettings);

    /** Describes the feedback loop the chain sits in: how long one trip round it takes, and how
        much of the audio survives each trip (0 to 1, where 1 is a loop that never fades). The
        pitch glitch uses it to keep a loop's pitch inside the range set for it. Call it before
        each sample; without a call, the chain behaves as if there were no loop.
    */
    void setLoop (float loopSamples, float loopGain) noexcept;

    /** What the tap's delay time should be multiplied by: 1, except while a varispeed pitch glitch
        runs. The tap glides to it, which bends the pitch as changing a tape's speed would.
    */
    float getDelayScale() const noexcept { return isActive (GlitchType::pitch) && pitchIsVarispeed ? varispeedScale : 1.0f; }

    /** Rolls for new glitches. Call at each chunk boundary, before processing that sample. */
    void onChunkBoundary();

    /** Processes one sample. Always call it, even with no glitches running, so histories fill. */
    float process (float input) noexcept;

    int getNumActive() const noexcept;
    bool isActive (GlitchType type) const noexcept { return slots[(size_t) type].active; }

    /** Reports to the diagnostic log, in builds that have one. */
    diagnostics::TapProbe& getProbe() noexcept { return probe; }

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
    float applyPitchStage (float dry, float wet) noexcept;
    float applyLevelStage (GlitchType type, float dry, float wet) noexcept;

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
    diagnostics::TapProbe probe;

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
    float pitchSpan = 1.0f;         // The window this glitch sweeps: pitchWindow, or fitted to a short loop.
    float pitchSmoothing = 0.0f;
    float pitchMean = 0.0f, pitchMeanSmoothing = 0.0f;   // The slow-moving level the audio rides on.

    /** A running measure of how alike two signals are, from 0 (unrelated) to 1 (identical). */
    struct Likeness
    {
        void reset() noexcept { powerA = powerB = cross = 1.0e-9f; }   // Alike until shown otherwise.

        float update (float a, float b, float smoothing) noexcept
        {
            powerA += smoothing * (a * a - powerA);
            powerB += smoothing * (b * b - powerB);
            cross  += smoothing * (a * b - cross);
            return juce::jlimit (0.0f, 1.0f, cross / (std::sqrt (powerA * powerB) + 1.0e-12f));
        }

        float powerA = 1.0e-9f, powerB = 1.0e-9f, cross = 1.0e-9f;
    };

    /** Crossfades a and b, with gains that add up to 1, without the dip in level a plain crossfade
        has when the two are unalike: the mix is scaled up by as much as it would lose, which is
        nothing for identical audio and 3 dB at most. A frozen loop passes through these crossfades
        dozens of times a second, so a small loss on each soon empties it.

        It only compares the two signals it is mixing, at the moment it mixes them. Steering a
        stage's output level by its input level looks simpler but is unstable in a feedback loop,
        where the output becomes the input a moment later.

        Only the movement around the mean is scaled. A constant offset is the same in both
        signals, and scaling it would let it grow a little with every crossfade.
    */
    float levelMix (float a, float gainA, float b, Likeness& likeness) noexcept
    {
        const auto gainB = 1.0f - gainA;
        const auto alike = likeness.update (a - pitchMean, b - pitchMean, pitchSmoothing);
        const auto scale = 1.0f / std::sqrt (gainA * gainA + gainB * gainB + 2.0f * gainA * gainB * alike);

        return pitchMean + ((a - pitchMean) * gainA + (b - pitchMean) * gainB) * scale;
    }

    Likeness pitchHeads;                              // The shifter's two read heads.
    std::array<Likeness, numGlitchTypes> stageEdges;  // A stage's output and the audio it fades to and from.
    float loopSamples = 1.0e9f, loopGain = 0.0f;

    // The estimate of where the loop's pitch has got to travels with the audio. Each sample's
    // offset from its original pitch, in semitones, is kept beside it: pitchTrack mirrors
    // pitchInput, so the shifter's read heads pick up the pitch of the audio they read, and
    // loopTrack mirrors the tap's delay line (coarsely, one entry per loopTrackStep samples), so
    // the pitch comes back round with the audio. Counting a fixed step for each trip round the
    // loop is far out: the heads read up to a window back, so audio is often shifted less than
    // once a trip, and by an amount that depends on where the heads happen to be.
    HistoryBuffer pitchTrack;
    std::vector<float> loopTrack;
    int loopTrackIndex = 0, loopTrackCount = 0;
    static constexpr int loopTrackStep = 16;

    float readLoopTrack() const noexcept;
    void writeLoopTrack (float semitones) noexcept;

    float pitchOffset = 0.0f;       // Of the audio leaving the pitch stage now.
    float pitchArriving = 0.0f;     // Of the audio reaching it now.
    float pitchWetOffset = 0.0f;    // Of the shifter's output, before its fades.
    float pitchSpeed = 0.0f;        // Semitones this sweep moves on each pass.
    float pitchUpRatio = 1.0f, pitchDownRatio = 1.0f;
    int pitchDirection = 1;
    bool pitchHoming = false;       // Set at a wall: sweeps favour the middle of the range until they reach it.
    bool pitchIsVarispeed = false;  // What the running pitch glitch is.
    float varispeedScale = 1.0f;

    struct PitchWalls
    {
        float low, high, centre, deadZone;
    };

    PitchWalls pitchWalls() const noexcept;

    // Formants
    FormantShifter lpcShifter, cepstralShifter;

    // Ring modulation
    float ringFrequency = 100.0f, ringPhase = 0.0f;

    // Frequency modulation
    float fmFrequency = 100.0f, fmPhase = 0.0f, fmDepth = 0.0f, fmCentre = 2.0f;

    // Bit crusher
    float crushLevels = 128.0f, crushRate = 1.0f, crushCounter = 0.0f, crushHeld = 0.0f;
};

} // namespace astralay::dsp
