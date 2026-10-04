#pragma once

#include <juce_core/juce_core.h>
#include "params/Units.h"

namespace astralay::ui
{

enum class StepSize { normal, fine, coarse };

/** Describes what a slider controls, so keyboard steps can follow the value's unit. */
struct StepContext
{
    Unit unit = Unit::plain;
    double minimum = 0.0;
    double maximum = 1.0;
    bool isInteger = false;     // Int parameters: steps are whole numbers.
    bool isNoteValue = false;   // Choice among note values: steps move through the list.
    int firstNoteIndex = 0;     // For note values: index into NoteValues::all() of choice 0.
    bool isOffset = false;      // An amount added to a value: frequencies step by 10 / 1 / 100 Hz.
};

/** Returns the value one keyboard step from current in the given direction (+1 or -1),
    clamped to the range. Steps follow the spec: dB 0.5 / 0.1 / 3, ms 10 / 1 / 100,
    percent 1 / 0.1 / 10, semitones 1 / 0.1 / 12, note values one entry at a time with coarse
    steps jumping between straight values. Frequencies step musically: a semitone, 10 cents or
    an octave.
*/
double stepValue (const StepContext& context, double current, int direction, StepSize size);

/** The size of a normal step at the current value, for screen readers' own increment commands. */
double normalStepSize (const StepContext& context, double current);

} // namespace astralay::ui
