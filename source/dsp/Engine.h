#pragma once

#include <array>
#include <optional>
#include "DcBlocker.h"
#include "Smear.h"
#include "Tap.h"

namespace astralay::dsp
{

/** Block-rate settings shared by all taps, already converted to DSP units. */
struct GlobalSettings
{
    float glideSeconds = 0.1f;
    bool freeze = false;
    bool freezeSustain = false;
    float mix = 0.5f;           // 0 (dry) to 1 (wet).
    float outputGain = 1.0f;    // Linear.
    float clipCeiling = 0.0f;   // Linear level the output is hard clipped at; 0 doesn't clip.
    float smearAmount = 0.0f;   // 0 to 1.
    float smearSeconds = 0.2f;

    GlitchGlobalSettings glitch;
    bool reproducible = false;
    int seed = 0;
};

/** The host's transport, read once per block. */
struct TransportInfo
{
    bool playing = false;
    bool hasPosition = false;       // ppq is valid.
    double ppq = 0.0;               // Position in quarter notes at the start of the block.
    double samplesPerQuarter = 0.0;
    bool synced = false;            // Host sync is on, so chunks follow the beat grid.
    double chunkQuarters = 0.25;    // Chunk length in quarter notes when synced.
    std::optional<juce::int64> samplePosition; // Host timeline, independent of tempo and sync.
};

/** The whole signal path: 16 taps, the shared glitch chunk grid, freeze, a DC blocker and smear on
    the combined repeats, dry/wet mix, output gain and the output hard clip.
*/
class Engine
{
public:
    static constexpr int numTaps = 16;
    static constexpr double maxSmearSeconds = 0.5;

    void prepare (double sampleRate, int maxBlockSize, double maxDelaySeconds);
    void reset();

    void setGlobalSettings (const GlobalSettings& settings);
    void setTapSettings (int tapIndex, const TapSettings& settings);

    /** Call once per block before process(). Aligns the chunk grid to the host's beats when synced,
        and restarts the random sequences on playback starts and timeline jumps when reproducible
        randomness is on. A missing sample position disables jump detection for that block.
    */
    void setTransport (const TransportInfo& transport);

    /** Processes one block. inRight may be null for a mono input. The input and output pointers may
        alias (in-place processing).
    */
    void process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept;

    const Tap& getTap (int tapIndex) const { return taps[(size_t) tapIndex]; }

    /** Reports glitches and levels to a diagnostic log, or to none with a null sink. Does nothing
        in builds without diagnostics. Not while audio is running.
    */
    void setDiagnostics (diagnostics::Sink* sink);

private:
    void restartRandomness (juce::int64 baseSeed);

    diagnostics::Sink* diagnosticSink = nullptr;
    diagnostics::OutputProbe outputProbe;

    std::array<Tap, numTaps> taps;
    DcBlocker leftDcBlocker, rightDcBlocker;
    Smear smear;
    GlobalSettings global;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> freeze;
    juce::SmoothedValue<float> dryGain, wetGain, outputGain;
    double sampleRate = 44100.0;

    int samplesToChunk = 0;
    bool wasPlaying = false;
    std::optional<juce::int64> expectedSamplePosition;
};

} // namespace astralay::dsp
