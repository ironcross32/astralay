#include "Presets.h"
#include "params/Parameters.h"

namespace astralay::state::Presets
{

namespace
{
    const juce::Identifier rootTag { "AstralayPreset" };
    const juce::Identifier parameterTag { "Parameter" };

    /** Every parameter a preset holds, which leaves out those that are played rather than set. */
    template <typename Callback>
    void forEachParameter (const juce::AudioProcessor& processor, Callback&& callback)
    {
        for (auto* parameter : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && ! params::isPerformanceState (ranged->paramID))
                callback (*ranged);
    }

    /** A macro's value, normalised over the range the macro has in the snapshot rather than the
        one it has now.
    */
    void setMacroValues (History::Snapshot& snapshot, const std::array<float, params::numMacros>& macroValues)
    {
        for (int m = 0; m < params::numMacros; ++m)
            snapshot.values[params::macroId (m)] = params::MacroParameter::toNormalised (macroValues[(size_t) m],
                                                                                         snapshot.macros[(size_t) m].bipolar);
    }

    History::Snapshot defaults (const juce::AudioProcessor& processor)
    {
        History::Snapshot snapshot;
        forEachParameter (processor, [&] (juce::RangedAudioParameter& p) { snapshot.values[p.paramID] = p.getDefaultValue(); });
        setMacroValues (snapshot, {});
        return snapshot;
    }

    juce::String tapParameter (int number, const char* suffix)
    {
        return params::tapId (number - 1, suffix);
    }
}

juce::File userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Astralay")
               .getChildFile ("Presets");
}

std::unique_ptr<juce::XmlElement> toXml (const juce::AudioProcessor& processor, const juce::String& name,
                                         const MacroSettings& macros)
{
    auto xml = std::make_unique<juce::XmlElement> (rootTag);
    xml->setAttribute ("version", version);
    xml->setAttribute ("name", name);
    xml->addChildElement (Macros::toXml (macros).release());

    forEachParameter (processor, [&] (juce::RangedAudioParameter& p)
    {
        auto* child = xml->createNewChildElement (parameterTag);
        child->setAttribute ("id", p.paramID);
        child->setAttribute ("value", (double) p.convertFrom0to1 (p.getValue()));
    });

    return xml;
}

bool fromXml (const juce::XmlElement& xml, const juce::AudioProcessor& processor, History::Snapshot& result)
{
    if (! xml.hasTagName (rootTag))
        return false;

    result = defaults (processor);
    result.presetName = xml.getStringAttribute ("name");
    result.modified = false;
    result.macros = Macros::fromXml (xml.getChildByName (Macros::rootTag));

    std::map<juce::String, juce::RangedAudioParameter*> byId;
    forEachParameter (processor, [&] (juce::RangedAudioParameter& p) { byId[p.paramID] = &p; });

    std::array<float, params::numMacros> macroValues {};

    for (auto* child : xml.getChildWithTagNameIterator (parameterTag))
    {
        const auto found = byId.find (child->getStringAttribute ("id"));

        // Parameters this version doesn't know are ignored, so newer presets still load.
        if (found != byId.end() && child->hasAttribute ("value"))
        {
            auto& p = *found->second;
            const auto stored = (float) child->getDoubleAttribute ("value");

            if (dynamic_cast<params::MacroParameter*> (&p) != nullptr)
            {
                for (int m = 0; m < params::numMacros; ++m)
                    if (p.paramID == params::macroId (m))
                        macroValues[(size_t) m] = stored;

                continue;
            }

            const auto& range = p.getNormalisableRange();
            const auto value = juce::jlimit (range.start, range.end, stored);
            result.values[p.paramID] = p.convertTo0to1 (value);
        }
    }

    setMacroValues (result, macroValues);
    return true;
}

const std::vector<Factory>& factory()
{
    using namespace params;

    static const std::vector<Factory> presets
    {
        { "Init", {} },

        { "Slapback",
          { { tapParameter (1, params::tap::time), "120 ms" },
            { tapParameter (1, params::tap::feedback), "10%" },
            { tapParameter (1, params::tap::highCut), "6 kHz" },
            { global::mix, "30%" },
            { global::smearAmount, "0%" } } },

        { "Ping pong",
          { { tapParameter (1, params::tap::time), "250 ms" },
            { tapParameter (1, params::tap::pan), "100 left" },
            { tapParameter (1, params::tap::feedback), "45%" },
            { tapParameter (2, params::tap::enabled), "On" },
            { tapParameter (2, params::tap::time), "500 ms" },
            { tapParameter (2, params::tap::pan), "100 right" },
            { tapParameter (2, params::tap::feedback), "45%" },
            { global::mix, "40%" } } },

        { "Rhythmic scatter",
          { { global::sync, "On" },
            { tapParameter (1, params::tap::timeSync), "1/8 dotted" },
            { tapParameter (1, params::tap::pan), "40 left" },
            { tapParameter (1, params::tap::feedback), "50%" },
            { tapParameter (1, params::tap::stutterProb), "30%" },
            { tapParameter (1, params::tap::reverseProb), "20%" },
            { tapParameter (2, params::tap::enabled), "On" },
            { tapParameter (2, params::tap::timeSync), "1/4" },
            { tapParameter (2, params::tap::pan), "40 right" },
            { tapParameter (2, params::tap::feedback), "50%" },
            { tapParameter (2, params::tap::stutterProb), "30%" },
            { tapParameter (2, params::tap::crushProb), "15%" },
            { global::placement, "Output and feedback" },
            { global::bufferSync, "1/16" } } },

        { "Frozen grains",
          { { tapParameter (1, params::tap::time), "800 ms" },
            { tapParameter (1, params::tap::feedback), "70%" },
            { tapParameter (1, params::tap::grainProb), "48%" },
            { tapParameter (1, params::tap::pitchProb), "18%" },
            { global::bufferSize, "250 ms" },
            { global::smearAmount, "60%" },
            { global::smearSize, "400 ms" },
            { global::mix, "50%" } } },

        { "Robot choir",
          { { tapParameter (1, params::tap::time), "300 ms" },
            { tapParameter (1, params::tap::feedback), "55%" },
            { tapParameter (1, params::tap::lpcProb), "42%" },
            { tapParameter (1, params::tap::cepsProb), "24%" },
            { tapParameter (1, params::tap::ringProb), "12%" },
            { global::placement, "Output and feedback" },
            { global::bufferSize, "200 ms" },
            { global::lengthMax, "8 chunks" } } },
    };

    return presets;
}

History::Snapshot snapshotFor (const Factory& preset, const juce::AudioProcessor& processor)
{
    auto snapshot = defaults (processor);
    snapshot.presetName = preset.name;
    snapshot.modified = false;

    std::map<juce::String, juce::RangedAudioParameter*> byId;
    forEachParameter (processor, [&] (juce::RangedAudioParameter& p) { byId[p.paramID] = &p; });

    for (const auto& [id, text] : preset.settings)
    {
        const auto found = byId.find (id);
        jassert (found != byId.end());

        if (found != byId.end())
            snapshot.values[id] = found->second->getValueForText (text);
    }

    return snapshot;
}

} // namespace astralay::state::Presets
