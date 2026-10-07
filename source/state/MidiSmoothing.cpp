#include "MidiSmoothing.h"

namespace astralay::state
{
namespace
{
constexpr const char* keys[] { "off", "linearFast", "linearSlow", "exponentialFast", "exponentialSlow" };
}
std::shared_ptr<MidiSmoothing> MidiSmoothing::shared (const juce::File& file)
{
    static juce::CriticalSection lock;
    static std::map<juce::String, std::weak_ptr<MidiSmoothing>> preferences;
    const juce::ScopedLock guard (lock);
    auto& weak = preferences[file.getFullPathName()];
    auto result = weak.lock();
    if (! result) { result.reset (new MidiSmoothing (file)); weak = result; }
    return result;
}
MidiSmoothing::MidiSmoothing (juce::File settings) : file (std::move (settings))
{
    const auto root = juce::JSON::parse (file);
    if (! root.isObject() || ! root["midiSmoothing"].isString()) return;
    for (int i = 0; i < 5; ++i)
        if (root["midiSmoothing"].toString() == keys[i]) mode.store ((Mode) i);
}
juce::String MidiSmoothing::label (Mode value)
{
    static const char* labels[] { "Off", "Linear Fast", "Linear Slow", "Exponential Fast", "Exponential Slow" };
    return labels[(int) value];
}
juce::Result MidiSmoothing::set (Mode value)
{
    if (! juce::isPositiveAndBelow ((int) value, 5)) return juce::Result::fail ("Invalid MIDI smoothing choice");
    mode.store (value); // Keep the session preference even if persistence fails.
    auto root = juce::JSON::parse (file);
    if (! root.isObject()) root = juce::var (new juce::DynamicObject());
    root.getDynamicObject()->setProperty ("midiSmoothing", keys[(int) value]);
    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("MIDI smoothing preference could not be saved");
    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (juce::JSON::toString (root))
        || ! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("MIDI smoothing preference could not be saved");
    return juce::Result::ok();
}
}
