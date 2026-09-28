#include "Steps.h"
#include "params/NoteValues.h"

namespace astralay::ui
{

namespace
{
    struct Increments
    {
        double normal, fine, coarse;
    };

    Increments additiveIncrements (Unit unit)
    {
        switch (unit)
        {
            case Unit::decibels:        return { 0.5, 0.1, 3.0 };
            case Unit::milliseconds:    return { 10.0, 1.0, 100.0 };
            case Unit::percent:         return { 1.0, 0.1, 10.0 };
            case Unit::pan:             return { 1.0, 0.1, 10.0 };
            case Unit::semitones:       return { 1.0, 0.1, 12.0 };
            case Unit::ratio:           return { 0.1, 0.01, 1.0 };
            case Unit::index:           return { 0.1, 0.01, 1.0 };
            case Unit::multiplier:      return { 1.0, 0.1, 8.0 };
            case Unit::grainsPerSecond: return { 1.0, 0.1, 10.0 };
            case Unit::bits:
            case Unit::chunks:
            case Unit::hertz:
            case Unit::plain:
                break;
        }

        return { 1.0, 0.1, 10.0 };
    }

    double pick (const Increments& i, StepSize size)
    {
        switch (size)
        {
            case StepSize::fine:   return i.fine;
            case StepSize::coarse: return i.coarse;
            case StepSize::normal: break;
        }

        return i.normal;
    }

    bool isStraightNote (int choiceIndex, int firstNoteIndex)
    {
        const auto& all = NoteValues::all();
        const auto index = firstNoteIndex + choiceIndex;
        return juce::isPositiveAndBelow (index, all.size())
            && all.getReference (index).modifier == NoteValue::Modifier::straight;
    }

    double stepNoteValue (const StepContext& c, double current, int direction, StepSize size)
    {
        const auto last = juce::roundToInt (c.maximum);
        auto index = juce::roundToInt (current);

        if (size != StepSize::coarse)
            return juce::jlimit (0, last, index + direction);

        // Coarse: move to the next straight value in that direction.
        for (index += direction; index >= 0 && index <= last; index += direction)
            if (isStraightNote (index, c.firstNoteIndex))
                return index;

        return direction > 0 ? last : 0;
    }
}

double stepValue (const StepContext& c, double current, int direction, StepSize size)
{
    jassert (direction == 1 || direction == -1);

    if (c.isNoteValue)
        return stepNoteValue (c, current, direction, size);

    double next;

    if (c.isInteger)
    {
        const auto coarse = juce::jmax (1.0, std::round ((c.maximum - c.minimum) / 10.0));
        next = std::round (current) + direction * (size == StepSize::coarse ? coarse : 1.0);
    }
    else if (c.unit == Unit::hertz)
    {
        const Increments semitones { 1.0, 0.1, 12.0 };
        next = current * std::pow (2.0, direction * pick (semitones, size) / 12.0);
    }
    else
    {
        const auto increment = pick (additiveIncrements (c.unit), size);

        // Snap to the step grid so repeated presses land on round values.
        const auto snapped = std::round (current / increment) * increment;
        const auto onGrid = std::abs (snapped - current) < increment * 1.0e-3;
        next = onGrid ? current + direction * increment
                      : (direction > 0 ? std::ceil (current / increment) : std::floor (current / increment)) * increment;
    }

    return juce::jlimit (c.minimum, c.maximum, next);
}

double normalStepSize (const StepContext& c, double current)
{
    if (c.isNoteValue || c.isInteger)
        return 1.0;

    if (c.unit == Unit::hertz)
        return juce::jmax (0.01, current * (std::pow (2.0, 1.0 / 12.0) - 1.0));

    return additiveIncrements (c.unit).normal;
}

} // namespace astralay::ui
