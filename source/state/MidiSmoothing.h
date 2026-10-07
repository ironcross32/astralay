#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Settings.h"

namespace astralay::state
{
/** Shared, process-local preference. File access is restricted to UI/construction calls. */
class MidiSmoothing final
{
public:
    enum class Mode { off, linearFast, linearSlow, exponentialFast, exponentialSlow };
    static std::shared_ptr<MidiSmoothing> shared (const juce::File&);
    static juce::String label (Mode);
    static double seconds (Mode mode) noexcept
    { return mode == Mode::linearSlow || mode == Mode::exponentialSlow ? 0.080 : 0.020; }
    static bool linear (Mode mode) noexcept
    { return mode == Mode::linearFast || mode == Mode::linearSlow; }
    Mode get() const noexcept { return mode.load(); }
    juce::Result set (Mode);
private:
    explicit MidiSmoothing (const juce::File&);
    std::shared_ptr<Settings> settings;
    std::atomic<Mode> mode { Mode::off };
};
}
