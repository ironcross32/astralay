#include "Settings.h"

namespace astralay::state
{
std::shared_ptr<Settings> Settings::shared (const juce::File& file)
{
    static juce::CriticalSection lock;
    static std::map<juce::String, std::weak_ptr<Settings>> instances;
    const juce::ScopedLock guard (lock);
    auto& weak = instances[file.getFullPathName()];
    auto result = weak.lock();
    if (! result) { result.reset (new Settings (file)); weak = result; }
    return result;
}
Settings::Settings (juce::File settings) : file (std::move (settings))
{
    if (const auto stored = read ("helpTags"); stored.isBool()) helpTagsOn.store ((bool) stored);
}
juce::var Settings::read (const juce::String& key) const
{
    const auto root = juce::JSON::parse (file);
    return root.isObject() ? root[juce::Identifier (key)] : juce::var();
}
juce::Result Settings::write (const juce::String& key, const juce::var& value)
{
    auto root = juce::JSON::parse (file);
    if (! root.isObject()) root = juce::var (new juce::DynamicObject());
    root.getDynamicObject()->setProperty (key, value);
    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("Settings could not be saved");
    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (juce::JSON::toString (root))
        || ! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Settings could not be saved");
    return juce::Result::ok();
}
juce::Result Settings::setHelpTags (bool shouldBeOn)
{
    helpTagsOn.store (shouldBeOn);
    return write ("helpTags", shouldBeOn);
}
}
