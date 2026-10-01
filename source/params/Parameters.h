#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Units.h"

namespace astralay::params
{

constexpr int numTaps = 16;
constexpr float volumeFloorDb = -60.0f;   // Shown and treated as -inf.
constexpr double maxDelaySeconds = 10.0;  // Synced times longer than this are clamped.
constexpr double fallbackTempo = 120.0;

/** Parameter IDs for the per-tap parameters, without the tap prefix. */
namespace tap
{
    inline constexpr auto enabled  = "enabled";
    inline constexpr auto time     = "time";
    inline constexpr auto timeSync = "timeSync";
    inline constexpr auto volume   = "volume";
    inline constexpr auto pan      = "pan";
    inline constexpr auto feedback = "feedback";
    inline constexpr auto lowCut   = "lowCut";
    inline constexpr auto highCut  = "highCut";

    inline constexpr auto reverseProb = "reverse_prob";

    inline constexpr auto stutterProb    = "stutter_prob";
    inline constexpr auto stutterMin     = "stutter_min";
    inline constexpr auto stutterMax     = "stutter_max";
    inline constexpr auto stutterSyncMin = "stutter_syncMin";
    inline constexpr auto stutterSyncMax = "stutter_syncMax";

    inline constexpr auto grainProb    = "grain_prob";
    inline constexpr auto grainSizeMin = "grain_sizeMin";
    inline constexpr auto grainSizeMax = "grain_sizeMax";
    inline constexpr auto grainDensMin = "grain_densMin";
    inline constexpr auto grainDensMax = "grain_densMax";

    inline constexpr auto pitchProb = "pitch_prob";
    inline constexpr auto pitchMin  = "pitch_min";
    inline constexpr auto pitchMax  = "pitch_max";
    inline constexpr auto pitchSpeedMin = "pitch_speedMin";
    inline constexpr auto pitchSpeedMax = "pitch_speedMax";
    inline constexpr auto pitchMode = "pitch_mode";

    inline constexpr auto lpcProb = "lpc_prob";
    inline constexpr auto lpcMin  = "lpc_min";
    inline constexpr auto lpcMax  = "lpc_max";

    inline constexpr auto cepsProb = "ceps_prob";
    inline constexpr auto cepsMin  = "ceps_min";
    inline constexpr auto cepsMax  = "ceps_max";

    inline constexpr auto ringProb = "ring_prob";
    inline constexpr auto ringMin  = "ring_min";
    inline constexpr auto ringMax  = "ring_max";

    inline constexpr auto fmProb     = "fm_prob";
    inline constexpr auto fmRatioMin = "fm_ratioMin";
    inline constexpr auto fmRatioMax = "fm_ratioMax";
    inline constexpr auto fmIndexMin = "fm_indexMin";
    inline constexpr auto fmIndexMax = "fm_indexMax";

    inline constexpr auto crushProb    = "crush_prob";
    inline constexpr auto crushBitsMin = "crush_bitsMin";
    inline constexpr auto crushBitsMax = "crush_bitsMax";
    inline constexpr auto crushRateMin = "crush_rateMin";
    inline constexpr auto crushRateMax = "crush_rateMax";
}

/** Parameter IDs for the global parameters. */
namespace global
{
    inline constexpr auto sync         = "sync";
    inline constexpr auto glide        = "glide";
    inline constexpr auto freeze       = "freeze";
    inline constexpr auto threshold    = "threshold";
    inline constexpr auto placement    = "glitchPlacement";
    inline constexpr auto bufferSize   = "bufferSize";
    inline constexpr auto bufferSync   = "bufferSync";
    inline constexpr auto maxGlitches  = "maxGlitches";
    inline constexpr auto lengthMin    = "glitchLengthMin";
    inline constexpr auto lengthMax    = "glitchLengthMax";
    inline constexpr auto reproducible = "reproducible";
    inline constexpr auto seed         = "seed";
    inline constexpr auto smearAmount  = "smearAmount";
    inline constexpr auto smearSize    = "smearSize";
    inline constexpr auto mix          = "mix";
    inline constexpr auto outputGain   = "outputGain";
}

/** Full ID of a per-tap parameter, for example tapId (2, tap::feedback) is "t03_feedback".
    tapIndex is zero-based.
*/
juce::String tapId (int tapIndex, const char* suffix);

/** Name shown to hosts and screen readers, for example "Tap 3 Feedback". tapIndex is zero-based. */
juce::String tapParameterName (int tapIndex, const char* suffix);

/** The unit a parameter's value is expressed in, by ID suffix (per-tap) or full ID (global).
    Returns Unit::plain for choice and toggle parameters.
*/
Unit unitFor (const juce::String& suffixOrGlobalId);

/** Every parameter in the plugin, grouped as "Tap 1" to "Tap 16" then "Global". */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Where glitches are applied in each tap, as indices of the glitch placement parameter. */
enum class GlitchPlacement
{
    feedbackPath = 0,   // Only on the signal fed back into the delay line; unheard at 0% feedback.
    outputAndFeedback   // On the delayed signal before it splits to the output and the feedback.
};

/** What a tap's pitch glitch does, as indices of its pitch mode parameter. */
enum class PitchMode
{
    sweep = 0,   // Shifts the audio on every pass, turning back at the ends of the pitch range.
    varispeed    // Changes the tap's delay time, bending the pitch as a tape's speed change would.
};

/** Where the output is hard clipped. Not a parameter: it is saved with the session (as these
    numbers) but can't be automated and isn't part of a preset.
*/
enum class OutputClip
{
    plus18 = 0,   // At +18 dBFS, where hosts such as Reaper mute a track.
    zero,         // At 0 dBFS.
    off
};

/** The linear level an output clip setting clips at, or 0 for no clipping. */
float outputClipCeiling (OutputClip clip) noexcept;

/** Stutter's synced slice choices are a subset of the note values: 1/64 up to 1/4. */
juce::StringArray stutterSyncChoices();

/** Converts a volume parameter value in dB to linear gain, treating the floor as silence. */
float volumeDbToGain (float db) noexcept;

} // namespace astralay::params
