#pragma once

#include <span>

namespace astralay::state
{

/** The common English words that random preset names are made from (see Presets::randomName).
    All lower case, with no hyphens or spaces.
*/
std::span<const char* const> presetWords();

} // namespace astralay::state
