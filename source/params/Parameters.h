#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Units.h"

namespace astralay::params
{

constexpr int numTaps = 16;
constexpr int numMacros = 8;
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

/** The unit of any parameter, by its full ID. */
Unit unitForId (const juce::String& parameterId);

/** ID of a macro's value parameter, for example macroId (2) is "macro3". macroIndex is zero-based. */
juce::String macroId (int macroIndex);

/** The name a macro has until the user renames it, for example "Macro 3". macroIndex is zero-based. */
juce::String defaultMacroName (int macroIndex);

/** Whether a macro can be given this parameter to move. True for the parameters that have a
    slider, apart from the glitch engine's, the output gain, the macros themselves, and the note
    values that stand in for a time while host sync is on, which follow the time's modulation.
*/
bool canModulate (const juce::String& parameterId);

/** The note-value parameter that takes a time's place while host sync is on, for example
    "t01_timeSync" for "t01_time", or an empty string if there is none. A macro that moves the
    time moves the note value too, by the same share of its range.
*/
juce::String syncedCounterpart (const juce::String& parameterId);

/** The parameter a slider showing this one sets a macro's amount for: the parameter itself, the
    time a synced note value stands in for, or an empty string if a macro can't move it.
*/
juce::String modulationTarget (const juce::String& parameterId);

/** A macro's value: 0 to 1, or -1 to 1 while the macro is bipolar. Either range covers the whole
    of the normalised value, so a host's knob or automation lane always uses its full travel.
    Hosts see the macro under the name the user gave it.
*/
class MacroParameter final : public juce::RangedAudioParameter
{
public:
    explicit MacroParameter (int macroIndex);

    /** Switches the range. The normalised value stays as it is, so the macro's value changes. */
    void setBipolar (bool shouldBeBipolar);
    bool isBipolar() const noexcept { return bipolar.load(); }

    /** An empty name restores the default. */
    void setDisplayName (const juce::String& newName);

    /** The value in the macro's current range. Safe to call on the audio thread. */
    float getMacroValue() const noexcept { return fromNormalised (value.load(), bipolar.load()); }

    static float toNormalised (float macroValue, bool isBipolar) noexcept;
    static float fromNormalised (float normalised, bool isBipolar) noexcept;

    const juce::NormalisableRange<float>& getNormalisableRange() const override;
    juce::String getName (int maximumStringLength) const override;
    float getValue() const override { return value.load(); }
    void setValue (float newValue) override { value.store (juce::jlimit (0.0f, 1.0f, newValue)); }
    float getDefaultValue() const override { return toNormalised (0.0f, bipolar.load()); }
    juce::String getText (float normalisedValue, int maximumStringLength) const override;
    float getValueForText (const juce::String& text) const override;

private:
    const int index;
    std::atomic<float> value { 0.0f };
    std::atomic<bool> bipolar { false };
    juce::String displayName;
    juce::CriticalSection nameLock;
};

/** Every parameter in the plugin, grouped as "Tap 1" to "Tap 16", "Global", then "Macros". */
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
