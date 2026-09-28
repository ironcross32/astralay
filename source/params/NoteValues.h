#pragma once

#include <juce_core/juce_core.h>

namespace astralay
{

/** A musical length used when host sync is on: a note fraction (1/64 to 1/2) or a number of bars
    (1, 2 or 4), optionally dotted or triplet.
*/
struct NoteValue
{
    enum class Modifier { straight, dotted, triplet };

    double base = 0.25;       // Fraction of a whole note, or number of bars when isBars is true.
    bool isBars = false;
    Modifier modifier = Modifier::straight;

    /** Length in quarter notes. barLengthInQuarters comes from the host's time signature (4 in 4/4). */
    double lengthInQuarters (double barLengthInQuarters) const noexcept;

    /** Spoken/visible label, for example "1/8 dotted" or "2 bars". */
    juce::String getLabel() const;
};

/** The ordered list of note values offered by synced controls, shortest first (ordered as in 4/4). */
namespace NoteValues
{
    const juce::Array<NoteValue>& all();

    /** Labels of all(), in order, for use as AudioParameterChoice items. */
    juce::StringArray labels();

    /** Labels of the entries whose 4/4 length lies between the two bounds (inclusive). */
    juce::StringArray labelsBetween (double minQuarters, double maxQuarters);

    /** Index into all() of the first entry at or above minQuarters (4/4). */
    int firstIndexAtLeast (double minQuarters);

    /** Index into all() of the entry with this label, or -1. */
    int indexOf (const juce::String& label);

    /** Parses text such as "1/8", "1/8d", "1/8 dotted", "1/16t", "1 bar", "4 bars" or "2 bars dotted".
        Returns the index into all(), or -1 if the text is not a note value in the list.
    */
    int parse (const juce::String& text);
}

} // namespace astralay
