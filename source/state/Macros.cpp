#include "Macros.h"

namespace astralay::state
{

juce::String macroName (const MacroSettings& macros, int macroIndex)
{
    const auto& name = macros[(size_t) macroIndex].name;
    return name.isEmpty() ? params::defaultMacroName (macroIndex) : name;
}

namespace Macros
{

std::unique_ptr<juce::XmlElement> toXml (const MacroSettings& macros)
{
    auto xml = std::make_unique<juce::XmlElement> (rootTag);

    for (int m = 0; m < params::numMacros; ++m)
    {
        const auto& macro = macros[(size_t) m];

        if (macro.name.isEmpty() && ! macro.bipolar && macro.modulations.empty())
            continue;

        auto* child = xml->createNewChildElement ("Macro");
        child->setAttribute ("index", m + 1);

        if (macro.name.isNotEmpty())
            child->setAttribute ("name", macro.name);

        if (macro.bipolar)
            child->setAttribute ("bipolar", true);

        for (const auto& modulation : macro.modulations)
        {
            auto* target = child->createNewChildElement ("Modulation");
            target->setAttribute ("id", modulation.parameterId);
            target->setAttribute ("amount", (double) modulation.amount);
        }
    }

    return xml;
}

MacroSettings fromXml (const juce::XmlElement* xml)
{
    MacroSettings macros;

    if (xml == nullptr)
        return macros;

    for (auto* child : xml->getChildWithTagNameIterator ("Macro"))
    {
        const auto m = child->getIntAttribute ("index") - 1;

        if (! juce::isPositiveAndBelow (m, params::numMacros))
            continue;

        auto& macro = macros[(size_t) m];
        macro.name = child->getStringAttribute ("name");
        macro.bipolar = child->getBoolAttribute ("bipolar");
        macro.modulations.clear();

        for (auto* target : child->getChildWithTagNameIterator ("Modulation"))
        {
            const auto id = target->getStringAttribute ("id");
            const auto amount = (float) target->getDoubleAttribute ("amount");

            const auto known = std::any_of (macro.modulations.begin(), macro.modulations.end(),
                                            [&id] (const Modulation& existing) { return existing.parameterId == id; });

            if (params::canModulate (id) && ! known && std::abs (amount) > 0.0f)
                macro.modulations.push_back ({ id, amount });
        }
    }

    return macros;
}

} // namespace Macros

} // namespace astralay::state
