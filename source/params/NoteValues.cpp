#include "NoteValues.h"

namespace astralay
{

namespace
{
    constexpr double quartersIn44 = 4.0;
    constexpr double maxLengthIn44 = 4.0 * quartersIn44; // 4 bars

    double modifierFactor (NoteValue::Modifier m) noexcept
    {
        switch (m)
        {
            case NoteValue::Modifier::dotted:  return 1.5;
            case NoteValue::Modifier::triplet: return 2.0 / 3.0;
            case NoteValue::Modifier::straight: break;
        }

        return 1.0;
    }

    juce::Array<NoteValue> buildList()
    {
        juce::Array<NoteValue> list;

        const auto add = [&list] (double base, bool isBars)
        {
            for (auto m : { NoteValue::Modifier::triplet, NoteValue::Modifier::straight, NoteValue::Modifier::dotted })
            {
                const NoteValue v { base, isBars, m };

                if (v.lengthInQuarters (quartersIn44) <= maxLengthIn44 + 1.0e-9)
                    list.add (v);
            }
        };

        for (auto denominator : { 64, 32, 16, 8, 4, 2 })
            add (1.0 / denominator, false);

        for (auto bars : { 1, 2, 4 })
            add ((double) bars, true);

        std::stable_sort (list.begin(), list.end(), [] (const NoteValue& a, const NoteValue& b)
        {
            return a.lengthInQuarters (quartersIn44) < b.lengthInQuarters (quartersIn44);
        });

        return list;
    }
}

double NoteValue::lengthInQuarters (double barLengthInQuarters) const noexcept
{
    const auto straightLength = isBars ? base * barLengthInQuarters : base * quartersIn44;
    return straightLength * modifierFactor (modifier);
}

juce::String NoteValue::getLabel() const
{
    juce::String label;

    if (isBars)
    {
        const auto bars = juce::roundToInt (base);
        label << bars << (bars == 1 ? " bar" : " bars");
    }
    else
    {
        label << "1/" << juce::roundToInt (1.0 / base);
    }

    if (modifier == Modifier::dotted)
        label << " dotted";
    else if (modifier == Modifier::triplet)
        label << " triplet";

    return label;
}

namespace NoteValues
{

const juce::Array<NoteValue>& all()
{
    static const auto list = buildList();
    return list;
}

juce::StringArray labels()
{
    juce::StringArray result;

    for (const auto& v : all())
        result.add (v.getLabel());

    return result;
}

juce::StringArray labelsBetween (double minQuarters, double maxQuarters)
{
    juce::StringArray result;

    for (const auto& v : all())
    {
        const auto length = v.lengthInQuarters (quartersIn44);

        if (length >= minQuarters - 1.0e-9 && length <= maxQuarters + 1.0e-9)
            result.add (v.getLabel());
    }

    return result;
}

int firstIndexAtLeast (double minQuarters)
{
    const auto& list = all();

    for (int i = 0; i < list.size(); ++i)
        if (list.getReference (i).lengthInQuarters (quartersIn44) >= minQuarters - 1.0e-9)
            return i;

    return list.size() - 1;
}

int indexOf (const juce::String& label)
{
    const auto& list = all();

    for (int i = 0; i < list.size(); ++i)
        if (list.getReference (i).getLabel() == label)
            return i;

    return -1;
}

int parse (const juce::String& text)
{
    auto t = text.trim().toLowerCase().removeCharacters (" ");

    auto modifier = NoteValue::Modifier::straight;

    if (t.endsWith ("dotted"))       { modifier = NoteValue::Modifier::dotted;  t = t.dropLastCharacters (6); }
    else if (t.endsWith ("triplet")) { modifier = NoteValue::Modifier::triplet; t = t.dropLastCharacters (7); }
    else if (t.endsWith ("d"))       { modifier = NoteValue::Modifier::dotted;  t = t.dropLastCharacters (1); }
    else if (t.endsWith ("t"))       { modifier = NoteValue::Modifier::triplet; t = t.dropLastCharacters (1); }

    NoteValue wanted;
    wanted.modifier = modifier;

    if (t.endsWith ("bars") || t.endsWith ("bar"))
    {
        t = t.upToLastOccurrenceOf ("bar", false, false);

        if (! t.containsOnly ("0123456789") || t.isEmpty())
            return -1;

        wanted.isBars = true;
        wanted.base = t.getIntValue();
    }
    else if (t.startsWith ("1/"))
    {
        const auto denominator = t.fromFirstOccurrenceOf ("/", false, false);

        if (! denominator.containsOnly ("0123456789") || denominator.isEmpty() || denominator.getIntValue() <= 0)
            return -1;

        wanted.base = 1.0 / denominator.getIntValue();
    }
    else
    {
        return -1;
    }

    const auto& list = all();

    for (int i = 0; i < list.size(); ++i)
    {
        const auto& v = list.getReference (i);

        if (v.isBars == wanted.isBars && v.modifier == wanted.modifier
            && juce::approximatelyEqual (v.base, wanted.base))
            return i;
    }

    return -1;
}

} // namespace NoteValues

} // namespace astralay
