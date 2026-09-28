#include "Presets.h"
#include "params/Parameters.h"

namespace astralay::state::Presets
{

namespace
{
    const juce::Identifier rootTag { "AstralayPreset" };
    const juce::Identifier parameterTag { "Parameter" };

    template <typename Callback>
    void forEachParameter (const juce::AudioProcessor& processor, Callback&& callback)
    {
        for (auto* parameter : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                callback (*ranged);
    }

    History::Snapshot defaults (const juce::AudioProcessor& processor)
    {
        History::Snapshot snapshot;
        forEachParameter (processor, [&] (juce::RangedAudioParameter& p) { snapshot.values[p.paramID] = p.getDefaultValue(); });
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

std::unique_ptr<juce::XmlElement> toXml (const juce::AudioProcessor& processor, const juce::String& name)
{
    auto xml = std::make_unique<juce::XmlElement> (rootTag);
    xml->setAttribute ("version", version);
    xml->setAttribute ("name", name);

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

    std::map<juce::String, juce::RangedAudioParameter*> byId;
    forEachParameter (processor, [&] (juce::RangedAudioParameter& p) { byId[p.paramID] = &p; });

    for (auto* child : xml.getChildWithTagNameIterator (parameterTag))
    {
        const auto found = byId.find (child->getStringAttribute ("id"));

        // Parameters this version doesn't know are ignored, so newer presets still load.
        if (found != byId.end() && child->hasAttribute ("value"))
        {
            auto& p = *found->second;
            const auto& range = p.getNormalisableRange();
            const auto value = juce::jlimit (range.start, range.end, (float) child->getDoubleAttribute ("value"));
            result.values[p.paramID] = p.convertTo0to1 (value);
        }
    }

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
            { tapParameter (1, params::tap::stutterProb), "60%" },
            { tapParameter (1, params::tap::reverseProb), "40%" },
            { tapParameter (2, params::tap::enabled), "On" },
            { tapParameter (2, params::tap::timeSync), "1/4" },
            { tapParameter (2, params::tap::pan), "40 right" },
            { tapParameter (2, params::tap::feedback), "50%" },
            { tapParameter (2, params::tap::stutterProb), "60%" },
            { tapParameter (2, params::tap::crushProb), "30%" },
            { global::threshold, "50%" },
            { global::placement, "Output and feedback" },
            { global::bufferSync, "1/16" } } },

        { "Frozen grains",
          { { tapParameter (1, params::tap::time), "800 ms" },
            { tapParameter (1, params::tap::feedback), "70%" },
            { tapParameter (1, params::tap::grainProb), "80%" },
            { tapParameter (1, params::tap::pitchProb), "30%" },
            { global::threshold, "60%" },
            { global::bufferSize, "250 ms" },
            { global::smearAmount, "60%" },
            { global::smearSize, "400 ms" },
            { global::mix, "50%" } } },

        { "Robot choir",
          { { tapParameter (1, params::tap::time), "300 ms" },
            { tapParameter (1, params::tap::feedback), "55%" },
            { tapParameter (1, params::tap::lpcProb), "70%" },
            { tapParameter (1, params::tap::cepsProb), "40%" },
            { tapParameter (1, params::tap::ringProb), "20%" },
            { global::threshold, "60%" },
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
