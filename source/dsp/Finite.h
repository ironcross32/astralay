#pragma once

#include <juce_core/juce_core.h>
#include <cstring>

namespace astralay::dsp
{

/** True for an infinity or a value that is not a number. */
inline bool isNonFinite (float x) noexcept
{
    // By its bits, since fast-math builds may assume every float is finite.
    juce::uint32 bits;
    std::memcpy (&bits, &x, sizeof (bits));
    return (bits & 0x7f800000u) == 0x7f800000u;
}

} // namespace astralay::dsp
