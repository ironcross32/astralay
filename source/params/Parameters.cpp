#include "Parameters.h"
#include "NoteValues.h"

namespace astralay::params
{

namespace
{
    using Range = juce::NormalisableRange<float>;
    using Group = juce::AudioProcessorParameterGroup;

    Range linear (float min, float max, float interval = 0.0f)
    {
        return { min, max, interval };
    }

    Range skewed (float min, float max, float centre)
    {
        Range r { min, max };
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name,
                                                          Range range, float defaultValue, Unit unit)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withStringFromValueFunction ([unit] (float v, int) { return Units::format (unit, v, volumeFloorDb); })
                              .withValueFromStringFunction ([unit, defaultValue] (const juce::String& text)
                                                            { return Units::parse (unit, text, volumeFloorDb).value_or (defaultValue); });

        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, defaultValue, attributes);
    }

    std::unique_ptr<juce::AudioParameterInt> makeInt (const juce::String& id, const juce::String& name,
                                                      int min, int max, int defaultValue, Unit unit)
    {
        auto attributes = juce::AudioParameterIntAttributes()
                              .withStringFromValueFunction ([unit] (int v, int) { return Units::format (unit, (float) v); })
                              .withValueFromStringFunction ([unit, defaultValue] (const juce::String& text)
                                                            { return juce::roundToInt (Units::parse (unit, text).value_or ((float) defaultValue)); });

        return std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, min, max, defaultValue, attributes);
    }

    std::unique_ptr<juce::AudioParameterBool> makeBool (const juce::String& id, const juce::String& name, bool defaultValue, int version = 1)
    {
        auto attributes = juce::AudioParameterBoolAttributes()
                              .withStringFromValueFunction ([] (bool v, int) { return v ? "On" : "Off"; })
                              .withValueFromStringFunction ([] (const juce::String& text)
                                                            {
                                                                const auto t = text.trim().toLowerCase();
                                                                return t == "on" || t == "1" || t == "yes" || t == "true";
                                                            });

        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, version }, name, defaultValue, attributes);
    }

    /** A choice among note values. choices must be a contiguous run of NoteValues::labels(). */
    std::unique_ptr<juce::AudioParameterChoice> makeNoteChoice (const juce::String& id, const juce::String& name,
                                                                const juce::StringArray& choices, const juce::String& defaultLabel)
    {
        const auto defaultIndex = juce::jmax (0, choices.indexOf (defaultLabel));

        auto attributes = juce::AudioParameterChoiceAttributes()
                              .withValueFromStringFunction ([choices, defaultIndex] (const juce::String& text)
                                                            {
                                                                const auto parsed = NoteValues::parse (text);

                                                                if (parsed < 0)
                                                                    return defaultIndex;

                                                                const auto index = choices.indexOf (NoteValues::all()[parsed].getLabel());
                                                                return index >= 0 ? index : defaultIndex;
                                                            });

        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, defaultIndex, attributes);
    }

    std::unique_ptr<Group> makeGroup (const juce::String& id, const juce::String& name)
    {
        return std::make_unique<Group> (id, name, " ");
    }

    std::unique_ptr<Group> createTapGroup (int t)
    {
        const auto id   = [t] (const char* suffix) { return tapId (t, suffix); };
        const auto name = [t] (const char* suffix) { return tapParameterName (t, suffix); };
        const auto unit = [] (const char* suffix) { return unitFor (suffix); };

        const auto f = [&] (const char* suffix, Range range, float defaultValue)
        {
            return makeFloat (id (suffix), name (suffix), range, defaultValue, unit (suffix));
        };

        auto group = makeGroup ("tap" + juce::String (t + 1), "Tap " + juce::String (t + 1));

        group->addChild (makeBool (id (tap::enabled), name (tap::enabled), t == 0),
                         f (tap::time, skewed (1.0f, 5000.0f, 500.0f), 500.0f),
                         makeNoteChoice (id (tap::timeSync), name (tap::timeSync), NoteValues::labels(), "1/4"),
                         f (tap::volume, linear (volumeFloorDb, 6.0f), 0.0f),
                         f (tap::pan, linear (-100.0f, 100.0f), 0.0f),
                         f (tap::feedback, linear (0.0f, 100.0f), 40.0f),
                         f (tap::lowCut, skewed (20.0f, 2000.0f, 200.0f), 20.0f),
                         f (tap::highCut, skewed (1000.0f, 20000.0f, 4472.0f), 20000.0f));

        const auto probability = [&] (const char* suffix) { return f (suffix, linear (0.0f, 100.0f), 0.0f); };

        auto reverse = makeGroup ("tap" + juce::String (t + 1) + "_reverse", "Reverse");
        reverse->addChild (probability (tap::reverseProb));

        auto stutter = makeGroup ("tap" + juce::String (t + 1) + "_stutter", "Stutter");
        stutter->addChild (probability (tap::stutterProb),
                           f (tap::stutterMin, skewed (5.0f, 250.0f, 35.0f), 20.0f),
                           f (tap::stutterMax, skewed (5.0f, 250.0f, 35.0f), 120.0f),
                           makeNoteChoice (id (tap::stutterSyncMin), name (tap::stutterSyncMin), stutterSyncChoices(), "1/32"),
                           makeNoteChoice (id (tap::stutterSyncMax), name (tap::stutterSyncMax), stutterSyncChoices(), "1/8"));

        auto grain = makeGroup ("tap" + juce::String (t + 1) + "_grain", "Granularize");
        grain->addChild (probability (tap::grainProb),
                         f (tap::grainSizeMin, skewed (5.0f, 200.0f, 32.0f), 20.0f),
                         f (tap::grainSizeMax, skewed (5.0f, 200.0f, 32.0f), 80.0f),
                         f (tap::grainDensMin, skewed (1.0f, 100.0f, 10.0f), 10.0f),
                         f (tap::grainDensMax, skewed (1.0f, 100.0f, 10.0f), 40.0f));

        auto pitch = makeGroup ("tap" + juce::String (t + 1) + "_pitch", "Pitch");
        pitch->addChild (probability (tap::pitchProb),
                         f (tap::pitchMin, linear (-24.0f, 24.0f), -12.0f),
                         f (tap::pitchMax, linear (-24.0f, 24.0f), 12.0f),
                         f (tap::pitchSpeedMin, linear (0.0f, 24.0f), 0.0f),
                         f (tap::pitchSpeedMax, linear (0.0f, 24.0f), 12.0f),
                         std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id (tap::pitchMode), 1 }, name (tap::pitchMode),
                                                                       juce::StringArray { "Sweep", "Varispeed" }, 0));

        auto lpc = makeGroup ("tap" + juce::String (t + 1) + "_lpc", "LPC formant");
        lpc->addChild (probability (tap::lpcProb),
                       f (tap::lpcMin, linear (-12.0f, 12.0f), -5.0f),
                       f (tap::lpcMax, linear (-12.0f, 12.0f), 5.0f));

        auto ceps = makeGroup ("tap" + juce::String (t + 1) + "_ceps", "Cepstral formant");
        ceps->addChild (probability (tap::cepsProb),
                        f (tap::cepsMin, linear (-12.0f, 12.0f), -5.0f),
                        f (tap::cepsMax, linear (-12.0f, 12.0f), 5.0f));

        auto ring = makeGroup ("tap" + juce::String (t + 1) + "_ring", "Ring modulation");
        ring->addChild (probability (tap::ringProb),
                        f (tap::ringMin, skewed (1.0f, 5000.0f, 70.0f), 30.0f),
                        f (tap::ringMax, skewed (1.0f, 5000.0f, 70.0f), 800.0f));

        auto fm = makeGroup ("tap" + juce::String (t + 1) + "_fm", "Frequency modulation");
        fm->addChild (probability (tap::fmProb),
                      f (tap::fmRatioMin, skewed (0.25f, 16.0f, 2.0f), 0.5f),
                      f (tap::fmRatioMax, skewed (0.25f, 16.0f, 2.0f), 3.0f),
                      f (tap::fmIndexMin, linear (0.0f, 10.0f), 0.5f),
                      f (tap::fmIndexMax, linear (0.0f, 10.0f), 4.0f));

        auto crush = makeGroup ("tap" + juce::String (t + 1) + "_crush", "Bit crusher");
        crush->addChild (probability (tap::crushProb),
                         makeInt (id (tap::crushBitsMin), name (tap::crushBitsMin), 1, 16, 4, Unit::bits),
                         makeInt (id (tap::crushBitsMax), name (tap::crushBitsMax), 1, 16, 10, Unit::bits),
                         f (tap::crushRateMin, skewed (1.0f, 64.0f, 8.0f), 1.0f),
                         f (tap::crushRateMax, skewed (1.0f, 64.0f, 8.0f), 8.0f));

        group->addChild (std::move (reverse), std::move (stutter), std::move (grain), std::move (pitch),
                         std::move (lpc), std::move (ceps), std::move (ring), std::move (fm), std::move (crush));

        return group;
    }

    std::unique_ptr<Group> createGlobalGroup()
    {
        using namespace global;

        const auto f = [] (const char* id, const char* name, Range range, float defaultValue)
        {
            return makeFloat (id, name, range, defaultValue, unitFor (id));
        };

        auto timing = makeGroup ("timing", "Timing");
        timing->addChild (makeBool (sync, "Host Sync", false),
                          f (glide, "Glide Time", skewed (0.0f, 2000.0f, 300.0f), 100.0f),
                          makeBool (freeze, "Freeze", false));

        auto engine = makeGroup ("glitchEngine", "Glitch engine");
        engine->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { placement, 1 }, "Glitch Placement",
                                                                        juce::StringArray { "Feedback path", "Output and feedback" }, 0),
                          f (bufferSize, "Buffer Size", skewed (10.0f, 2000.0f, 250.0f), 125.0f),
                          makeNoteChoice (bufferSync, "Synced Buffer Size", NoteValues::labels(), "1/16"),
                          makeInt (maxGlitches, "Maximum Simultaneous Glitches", 1, 4, 2, Unit::plain),
                          makeInt (lengthMin, "Minimum Glitch Length", 1, 16, 1, Unit::chunks),
                          makeInt (lengthMax, "Maximum Glitch Length", 1, 16, 4, Unit::chunks),
                          makeBool (reproducible, "Reproducible Randomness", false),
                          makeInt (seed, "Seed", 0, 9999, 0, Unit::plain));

        auto output = makeGroup ("output", "Output");
        output->addChild (f (smearAmount, "Smear Amount", linear (0.0f, 100.0f), 10.0f),
                          f (smearSize, "Smear Size", skewed (10.0f, 500.0f, 150.0f), 200.0f),
                          f (mix, "Mix", linear (0.0f, 100.0f), 50.0f),
                          f (outputGain, "Output Gain", linear (-24.0f, 12.0f), -3.0f));

        auto group = makeGroup ("global", "Global");
        group->addChild (std::move (timing), std::move (engine), std::move (output));
        return group;
    }
}

juce::String tapId (int tapIndex, const char* suffix)
{
    return "t" + juce::String (tapIndex + 1).paddedLeft ('0', 2) + "_" + suffix;
}

juce::String tapParameterName (int tapIndex, const char* suffix)
{
    static const std::map<juce::String, juce::String> names
    {
        { tap::enabled,  "Enabled" },
        { tap::time,     "Time" },
        { tap::timeSync, "Synced Time" },
        { tap::volume,   "Volume" },
        { tap::pan,      "Pan" },
        { tap::feedback, "Feedback" },
        { tap::lowCut,   "Low Cut" },
        { tap::highCut,  "High Cut" },

        { tap::reverseProb, "Reverse Probability" },

        { tap::stutterProb,    "Stutter Probability" },
        { tap::stutterMin,     "Stutter Minimum Slice" },
        { tap::stutterMax,     "Stutter Maximum Slice" },
        { tap::stutterSyncMin, "Stutter Minimum Synced Slice" },
        { tap::stutterSyncMax, "Stutter Maximum Synced Slice" },

        { tap::grainProb,    "Granularize Probability" },
        { tap::grainSizeMin, "Granularize Minimum Grain Size" },
        { tap::grainSizeMax, "Granularize Maximum Grain Size" },
        { tap::grainDensMin, "Granularize Minimum Density" },
        { tap::grainDensMax, "Granularize Maximum Density" },

        { tap::pitchProb, "Pitch Probability" },
        { tap::pitchMin,  "Pitch Minimum" },
        { tap::pitchMax,  "Pitch Maximum" },
        { tap::pitchSpeedMin, "Pitch Minimum Speed" },
        { tap::pitchSpeedMax, "Pitch Maximum Speed" },
        { tap::pitchMode, "Pitch Mode" },

        { tap::lpcProb, "LPC Formant Probability" },
        { tap::lpcMin,  "LPC Formant Minimum Shift" },
        { tap::lpcMax,  "LPC Formant Maximum Shift" },

        { tap::cepsProb, "Cepstral Formant Probability" },
        { tap::cepsMin,  "Cepstral Formant Minimum Shift" },
        { tap::cepsMax,  "Cepstral Formant Maximum Shift" },

        { tap::ringProb, "Ring Modulation Probability" },
        { tap::ringMin,  "Ring Modulation Minimum Frequency" },
        { tap::ringMax,  "Ring Modulation Maximum Frequency" },

        { tap::fmProb,     "Frequency Modulation Probability" },
        { tap::fmRatioMin, "Frequency Modulation Minimum Ratio" },
        { tap::fmRatioMax, "Frequency Modulation Maximum Ratio" },
        { tap::fmIndexMin, "Frequency Modulation Minimum Index" },
        { tap::fmIndexMax, "Frequency Modulation Maximum Index" },

        { tap::crushProb,    "Bit Crusher Probability" },
        { tap::crushBitsMin, "Bit Crusher Minimum Bit Depth" },
        { tap::crushBitsMax, "Bit Crusher Maximum Bit Depth" },
        { tap::crushRateMin, "Bit Crusher Minimum Rate Reduction" },
        { tap::crushRateMax, "Bit Crusher Maximum Rate Reduction" },
    };

    const auto it = names.find (suffix);
    jassert (it != names.end());

    return "Tap " + juce::String (tapIndex + 1) + " " + (it != names.end() ? it->second : juce::String (suffix));
}

Unit unitFor (const juce::String& id)
{
    static const std::map<juce::String, Unit> units
    {
        { tap::time,     Unit::milliseconds },
        { tap::volume,   Unit::decibels },
        { tap::pan,      Unit::pan },
        { tap::feedback, Unit::percent },
        { tap::lowCut,   Unit::hertz },
        { tap::highCut,  Unit::hertz },

        { tap::reverseProb, Unit::percent },
        { tap::stutterProb, Unit::percent },
        { tap::grainProb,   Unit::percent },
        { tap::pitchProb,   Unit::percent },
        { tap::lpcProb,     Unit::percent },
        { tap::cepsProb,    Unit::percent },
        { tap::ringProb,    Unit::percent },
        { tap::fmProb,      Unit::percent },
        { tap::crushProb,   Unit::percent },

        { tap::stutterMin,   Unit::milliseconds },
        { tap::stutterMax,   Unit::milliseconds },
        { tap::grainSizeMin, Unit::milliseconds },
        { tap::grainSizeMax, Unit::milliseconds },
        { tap::grainDensMin, Unit::grainsPerSecond },
        { tap::grainDensMax, Unit::grainsPerSecond },
        { tap::pitchMin,     Unit::semitones },
        { tap::pitchMax,     Unit::semitones },
        { tap::pitchSpeedMin, Unit::semitonesPerPass },
        { tap::pitchSpeedMax, Unit::semitonesPerPass },
        { tap::lpcMin,       Unit::semitones },
        { tap::lpcMax,       Unit::semitones },
        { tap::cepsMin,      Unit::semitones },
        { tap::cepsMax,      Unit::semitones },
        { tap::ringMin,      Unit::hertz },
        { tap::ringMax,      Unit::hertz },
        { tap::fmRatioMin,   Unit::ratio },
        { tap::fmRatioMax,   Unit::ratio },
        { tap::fmIndexMin,   Unit::index },
        { tap::fmIndexMax,   Unit::index },
        { tap::crushBitsMin, Unit::bits },
        { tap::crushBitsMax, Unit::bits },
        { tap::crushRateMin, Unit::multiplier },
        { tap::crushRateMax, Unit::multiplier },

        { global::glide,       Unit::milliseconds },
        { global::bufferSize,  Unit::milliseconds },
        { global::lengthMin,   Unit::chunks },
        { global::lengthMax,   Unit::chunks },
        { global::smearAmount, Unit::percent },
        { global::smearSize,   Unit::milliseconds },
        { global::mix,         Unit::percent },
        { global::outputGain,  Unit::decibels },
    };

    const auto it = units.find (id);
    return it != units.end() ? it->second : Unit::plain;
}

juce::StringArray stutterSyncChoices()
{
    return NoteValues::labelsBetween (0.0, 1.0);
}

juce::String macroId (int macroIndex)
{
    return "macro" + juce::String (macroIndex + 1);
}

juce::String defaultMacroName (int macroIndex)
{
    return "Macro " + juce::String (macroIndex + 1);
}

namespace
{
    /** Per-tap IDs are "t01_" and so on. */
    bool isTapId (const juce::String& id)
    {
        return id.length() > 4 && id[0] == 't' && id[3] == '_' && id.substring (1, 3).containsOnly ("0123456789");
    }
}

Unit unitForId (const juce::String& id)
{
    return unitFor (isTapId (id) ? id.substring (4) : id);
}

namespace
{
    /** Each per-tap time and the note value that takes its place while host sync is on. */
    constexpr std::array<std::pair<const char*, const char*>, 3> syncedPairs
    { {
        { tap::time, tap::timeSync },
        { tap::stutterMin, tap::stutterSyncMin },
        { tap::stutterMax, tap::stutterSyncMax },
    } };
}

juce::String syncedCounterpart (const juce::String& id)
{
    if (isTapId (id))
        for (const auto& [unsynced, synced] : syncedPairs)
            if (id.substring (4) == unsynced)
                return id.substring (0, 4) + synced;

    return {};
}

juce::String modulationTarget (const juce::String& id)
{
    if (isTapId (id))
        for (const auto& [unsynced, synced] : syncedPairs)
            if (id.substring (4) == synced)
                return id.substring (0, 4) + unsynced;

    return canModulate (id) ? id : juce::String();
}

bool canModulate (const juce::String& id)
{
    // Whether a tap is on and its pitch mode have no slider.
    if (isTapId (id))
    {
        const auto suffix = id.substring (4);

        for (const auto& pair : syncedPairs)
            if (suffix == pair.second)
                return false;

        return suffix != tap::enabled && suffix != tap::pitchMode;
    }

    return id == global::glide || id == global::smearAmount || id == global::smearSize || id == global::mix;
}

//==============================================================================
MacroParameter::MacroParameter (int macroIndex)
    : RangedAudioParameter (juce::ParameterID { macroId (macroIndex), 1 }, defaultMacroName (macroIndex)),
      index (macroIndex)
{
}

void MacroParameter::setBipolar (bool shouldBeBipolar)
{
    if (bipolar.exchange (shouldBeBipolar) == shouldBeBipolar)
        return;

    // Listeners, the parameter store among them, hold the value in the old range.
    sendValueChangedMessageToListeners (getValue());
}

void MacroParameter::setDisplayName (const juce::String& newName)
{
    const juce::ScopedLock lock (nameLock);
    displayName = newName;
}

float MacroParameter::toNormalised (float macroValue, bool isBipolar) noexcept
{
    return juce::jlimit (0.0f, 1.0f, isBipolar ? (macroValue + 1.0f) * 0.5f : macroValue);
}

float MacroParameter::fromNormalised (float normalised, bool isBipolar) noexcept
{
    return isBipolar ? normalised * 2.0f - 1.0f : normalised;
}

const juce::NormalisableRange<float>& MacroParameter::getNormalisableRange() const
{
    static const Range unipolarRange { 0.0f, 1.0f }, bipolarRange { -1.0f, 1.0f };
    return bipolar.load() ? bipolarRange : unipolarRange;
}

juce::String MacroParameter::getName (int maximumStringLength) const
{
    const juce::ScopedLock lock (nameLock);
    return (displayName.isEmpty() ? defaultMacroName (index) : displayName).substring (0, maximumStringLength);
}

juce::String MacroParameter::getText (float normalisedValue, int) const
{
    return Units::format (Unit::macro, fromNormalised (normalisedValue, bipolar.load()));
}

float MacroParameter::getValueForText (const juce::String& text) const
{
    return toNormalised (Units::parse (Unit::macro, text).value_or (0.0f), bipolar.load());
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int t = 0; t < numTaps; ++t)
        layout.add (createTapGroup (t));

    layout.add (createGlobalGroup());

    // After the rest, so that adding the macros left every earlier parameter where it was.
    auto macros = makeGroup ("macros", "Macros");

    for (int m = 0; m < numMacros; ++m)
        macros->addChild (std::make_unique<MacroParameter> (m));

    layout.add (std::move (macros));
    // Append new parameters so every existing host parameter index remains stable. The editor
    // places this beside Freeze in Global/Timing; version 2 also preserves AU automation order.
    layout.add (makeBool (global::freezeSustain, "Freeze Sustain", false, 2));
    return layout;
}

float outputClipCeiling (OutputClip clip) noexcept
{
    switch (clip)
    {
        // A hair under +18 dBFS, so that rounding can't carry a clipped peak over a host's limit there.
        case OutputClip::plus18: return juce::Decibels::decibelsToGain (18.0f) * 0.9999f;
        case OutputClip::zero:   return 1.0f;
        case OutputClip::off:    break;
    }

    return 0.0f;
}

float volumeDbToGain (float db) noexcept
{
    return db <= volumeFloorDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}

} // namespace astralay::params
