#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace astralay
{

/** The unit a parameter's value is expressed in. Decides how values are shown to the user and how
    typed text is interpreted.
*/
enum class Unit
{
    milliseconds,
    decibels,       // The range minimum is shown as "-inf".
    percent,
    pan,            // -100 (left) to 100 (right).
    hertz,
    semitones,
    semitonesPerPass,   // How fast a pitch sweep moves; never negative, so shown without a sign.
    ratio,
    index,
    multiplier,     // Sample-rate reduction, shown as "8.0x".
    bits,
    grainsPerSecond,
    chunks,
    plain
};

namespace Units
{
    /** Formats a value for display and for screen readers, for example "500 ms", "-3.0 dB",
        "35 left", "1.20 kHz" or "+7.0 semitones". decibelFloor is the value shown as "-inf".
    */
    juce::String format (Unit unit, float value, float decibelFloor = -60.0f);

    /** Parses typed text in the given unit. Accepts plain numbers in the unit's own terms plus
        optional suffixes (ms, s, Hz, kHz, dB, %, st, x, L, R, C, "left", "right", "centre").
        "-inf" is accepted for decibels and returns decibelFloor. Returns nothing if the text is not
        understood. Does not check the value against any range.
    */
    std::optional<float> parse (Unit unit, const juce::String& text, float decibelFloor = -60.0f);
}

} // namespace astralay
