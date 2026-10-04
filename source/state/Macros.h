#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "params/Parameters.h"

namespace astralay::state
{

/** One parameter a macro moves. The amount is in the parameter's own unit (milliseconds, percent,
    steps through a list of note values and so on): the macro's value times the amount is added
    to the parameter's value. It is never zero; a modulation set to zero is removed.
*/
struct Modulation
{
    juce::String parameterId;
    float amount = 0.0f;
};

/** A macro's settings, apart from its value, which is a parameter. */
struct Macro
{
    juce::String name;   // Empty until the user renames the macro.
    bool bipolar = false;
    std::vector<Modulation> modulations;   // In the order they were added.
};

using MacroSettings = std::array<Macro, params::numMacros>;

/** The name to show for a macro: the user's, or "Macro 3". macroIndex is zero-based. */
juce::String macroName (const MacroSettings& macros, int macroIndex);

/** Macro settings as XML, for the session state and preset files:

    <Macros>
      <Macro index="1" name="Sweep" bipolar="1">
        <Modulation id="t01_feedback" amount="25"/>
      </Macro>
      ...
    </Macros>

    Amounts are in each parameter's own unit. Macros left at their defaults are omitted.
*/
namespace Macros
{
    inline constexpr auto rootTag = "Macros";

    std::unique_ptr<juce::XmlElement> toXml (const MacroSettings& macros);

    /** Reads what toXml wrote. Anything missing takes its default, so a null element gives the
        default settings; modulations of parameters a macro can't move are dropped.
    */
    MacroSettings fromXml (const juce::XmlElement* xml);
}

} // namespace astralay::state
