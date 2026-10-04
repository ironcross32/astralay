#include "Units.h"

namespace astralay::Units
{

namespace
{
    juce::String trimmedNumber (float value, int maxDecimals)
    {
        auto s = juce::String (value, maxDecimals);

        if (s.containsChar ('.'))
        {
            s = s.trimCharactersAtEnd ("0");
            s = s.trimCharactersAtEnd (".");
        }

        return s == "-0" ? "0" : s;
    }

    /** Splits "12.5kHz" into 12.5 and "khz". Returns nothing if there's no leading number. */
    std::optional<std::pair<float, juce::String>> splitNumberAndSuffix (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase().removeCharacters (" ");

        int end = 0;
        const auto length = t.length();

        if (end < length && (t[end] == '-' || t[end] == '+'))
            ++end;

        const auto digitsStart = end;
        bool seenDigit = false;

        while (end < length && (juce::CharacterFunctions::isDigit (t[end]) || t[end] == '.'))
        {
            seenDigit = seenDigit || juce::CharacterFunctions::isDigit (t[end]);
            ++end;
        }

        if (! seenDigit || end == digitsStart)
            return std::nullopt;

        return std::make_pair (t.substring (0, end).getFloatValue(), t.substring (end));
    }
}

juce::String format (Unit unit, float value, float decibelFloor)
{
    switch (unit)
    {
        case Unit::milliseconds:
            if (value >= 1000.0f)
                return trimmedNumber (value / 1000.0f, 2) + " s";
            return trimmedNumber (value, value < 10.0f ? 1 : 0) + " ms";

        case Unit::decibels:
            if (value <= decibelFloor)
                return "-inf dB";
            return juce::String (value, 1) + " dB";

        case Unit::percent:
            return trimmedNumber (value, value < 10.0f ? 1 : 0) + "%";

        case Unit::pan:
        {
            const auto amount = juce::roundToInt (std::abs (value));

            if (amount == 0)
                return "centre";

            return juce::String (amount) + (value < 0.0f ? " left" : " right");
        }

        case Unit::hertz:
            if (value >= 1000.0f)
                return trimmedNumber (value / 1000.0f, 2) + " kHz";
            return trimmedNumber (value, value < 10.0f ? 1 : 0) + " Hz";

        case Unit::semitones:
        {
            const auto rounded = std::round (value * 10.0f) / 10.0f;
            const auto sign = rounded > 0.0f ? "+" : "";
            return sign + trimmedNumber (rounded, 1) + " semitones";
        }

        case Unit::semitonesPerPass:
            return trimmedNumber (value, 1) + " semitones per pass";

        case Unit::ratio:
        case Unit::index:
            return trimmedNumber (value, 2);

        case Unit::multiplier:
            return trimmedNumber (value, 1) + "x";

        case Unit::bits:
        {
            const auto bits = juce::roundToInt (value);
            return juce::String (bits) + (bits == 1 ? " bit" : " bits");
        }

        case Unit::grainsPerSecond:
            return trimmedNumber (value, 1) + " per second";

        case Unit::chunks:
        {
            const auto chunks = juce::roundToInt (value);
            return juce::String (chunks) + (chunks == 1 ? " chunk" : " chunks");
        }

        case Unit::macro:
            return trimmedNumber (value, 3);

        case Unit::plain:
            break;
    }

    return trimmedNumber (value, 2);
}

std::optional<float> parse (Unit unit, const juce::String& text, float decibelFloor)
{
    const auto lower = text.trim().toLowerCase();

    if (unit == Unit::decibels && (lower == "-inf" || lower == "-inf db" || lower == "-infinity"))
        return decibelFloor;

    if (unit == Unit::pan)
    {
        const auto compact = lower.removeCharacters (" ");

        if (compact == "c" || compact == "centre" || compact == "center")
            return 0.0f;

        const auto split = splitNumberAndSuffix (compact);

        if (! split)
            return std::nullopt;

        const auto [number, suffix] = *split;

        if (suffix.isEmpty())                        return number;
        if (suffix == "l" || suffix == "left")       return -std::abs (number);
        if (suffix == "r" || suffix == "right")      return std::abs (number);
        return std::nullopt;
    }

    const auto split = splitNumberAndSuffix (lower);

    if (! split)
        return std::nullopt;

    const auto [number, suffix] = *split;

    if (suffix.isEmpty())
        return number;

    switch (unit)
    {
        case Unit::milliseconds:
            if (suffix == "ms") return number;
            if (suffix == "s")  return number * 1000.0f;
            break;

        case Unit::decibels:
            if (suffix == "db") return number;
            break;

        case Unit::percent:
            if (suffix == "%") return number;
            break;

        case Unit::hertz:
            if (suffix == "hz")  return number;
            if (suffix == "khz") return number * 1000.0f;
            break;

        case Unit::semitones:
        case Unit::semitonesPerPass:
            if (suffix == "st" || suffix == "semitone" || suffix == "semitones") return number;
            break;

        case Unit::multiplier:
            if (suffix == "x") return number;
            break;

        case Unit::bits:
            if (suffix == "bit" || suffix == "bits") return number;
            break;

        case Unit::grainsPerSecond:
            if (suffix == "/s" || suffix == "persecond") return number;
            break;

        case Unit::chunks:
            if (suffix == "chunk" || suffix == "chunks") return number;
            break;

        case Unit::pan:
        case Unit::ratio:
        case Unit::index:
        case Unit::macro:
        case Unit::plain:
            break;
    }

    return std::nullopt;
}

} // namespace astralay::Units
