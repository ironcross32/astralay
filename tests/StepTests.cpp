#include <juce_core/juce_core.h>
#include "params/NoteValues.h"
#include "ui/Steps.h"

class StepTests final : public juce::UnitTest
{
public:
    StepTests() : juce::UnitTest ("Keyboard steps", "Astralay") {}

    void runTest() override
    {
        using namespace astralay;
        using namespace astralay::ui;

        beginTest ("Additive steps follow the spec for each unit");
        {
            const StepContext ms { Unit::milliseconds, 1.0, 5000.0 };
            expectWithinAbsoluteError (stepValue (ms, 500.0, 1, StepSize::normal), 510.0, 1.0e-9);
            expectWithinAbsoluteError (stepValue (ms, 500.0, 1, StepSize::fine), 501.0, 1.0e-9);
            expectWithinAbsoluteError (stepValue (ms, 500.0, -1, StepSize::coarse), 400.0, 1.0e-9);

            const StepContext db { Unit::decibels, -60.0, 6.0 };
            expectWithinAbsoluteError (stepValue (db, 0.0, -1, StepSize::normal), -0.5, 1.0e-9);
            expectWithinAbsoluteError (stepValue (db, 0.0, 1, StepSize::coarse), 3.0, 1.0e-9);

            const StepContext pct { Unit::percent, 0.0, 100.0 };
            expectWithinAbsoluteError (stepValue (pct, 40.0, 1, StepSize::fine), 40.1, 1.0e-9);

            const StepContext st { Unit::semitones, -24.0, 24.0 };
            expectWithinAbsoluteError (stepValue (st, 0.0, 1, StepSize::coarse), 12.0, 1.0e-9);
        }

        beginTest ("Off-grid values snap to the next round value");
        {
            const StepContext ms { Unit::milliseconds, 1.0, 5000.0 };
            expectWithinAbsoluteError (stepValue (ms, 503.7, 1, StepSize::normal), 510.0, 1.0e-9);
            expectWithinAbsoluteError (stepValue (ms, 503.7, -1, StepSize::normal), 500.0, 1.0e-9);
        }

        beginTest ("Steps are clamped to the range");
        {
            const StepContext ms { Unit::milliseconds, 1.0, 5000.0 };
            expectWithinAbsoluteError (stepValue (ms, 4990.0, 1, StepSize::coarse), 5000.0, 1.0e-9);
            expectWithinAbsoluteError (stepValue (ms, 5.0, -1, StepSize::normal), 1.0, 1.0e-9);
        }

        beginTest ("Frequencies step by semitones and octaves");
        {
            const StepContext hz { Unit::hertz, 20.0, 20000.0 };
            expectWithinAbsoluteError (stepValue (hz, 1000.0, 1, StepSize::coarse), 2000.0, 1.0e-6);
            expectWithinAbsoluteError (stepValue (hz, 1000.0, -1, StepSize::coarse), 500.0, 1.0e-6);
            expectWithinAbsoluteError (stepValue (hz, 440.0, 1, StepSize::normal), 440.0 * std::pow (2.0, 1.0 / 12.0), 1.0e-6);
        }

        beginTest ("Note values move one entry, or between straight values when coarse");
        {
            const auto count = NoteValues::all().size();
            StepContext notes { Unit::plain, 0.0, (double) (count - 1) };
            notes.isNoteValue = true;

            const auto quarter = NoteValues::indexOf ("1/4");
            const auto half = NoteValues::indexOf ("1/2");
            const auto eighth = NoteValues::indexOf ("1/8");

            expectEquals ((int) stepValue (notes, quarter, 1, StepSize::normal), quarter + 1);
            expectEquals ((int) stepValue (notes, quarter, 1, StepSize::coarse), half);
            expectEquals ((int) stepValue (notes, quarter, -1, StepSize::coarse), eighth);
            expectEquals ((int) stepValue (notes, count - 1, 1, StepSize::normal), count - 1);
        }

        beginTest ("Integer parameters step by whole numbers");
        {
            StepContext seed { Unit::plain, 0.0, 9999.0 };
            seed.isInteger = true;
            expectEquals ((int) stepValue (seed, 10.0, 1, StepSize::normal), 11);
            expectEquals ((int) stepValue (seed, 10.0, 1, StepSize::fine), 11);
            expectEquals ((int) stepValue (seed, 10.0, 1, StepSize::coarse), 1010);
        }
    }
};

static StepTests stepTests;
