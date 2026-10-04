#pragma once

#include <bit>
#include <cstdint>

namespace astralay::dsp
{

/** Derives an independent seed for a tap, wrapping explicitly modulo 2^64. User seeds (0 to
    9999) keep their existing sequences; full-width random seeds may wrap both the multiplication
    and the addition. Reinterpret the resulting bits for JUCE's signed seed API.
*/
constexpr std::int64_t seedForTap (std::int64_t baseSeed, std::uint64_t tapIndex) noexcept
{
    const auto bits = static_cast<std::uint64_t> (baseSeed) * std::uint64_t { 1000003 } + tapIndex;
    return std::bit_cast<std::int64_t> (bits);
}

} // namespace astralay::dsp
