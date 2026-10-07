#pragma once

#include <juce_core/juce_core.h>

namespace astralay::state
{
/** The preferences kept in Settings.json, shared by every plugin instance in the process. File
    access is restricted to UI/construction calls. */
class Settings final
{
public:
    static std::shared_ptr<Settings> shared (const juce::File&);

    /** The value stored under key, or void if there is none or the file can't be read. */
    juce::var read (const juce::String& key) const;

    /** Stores one value, leaving the rest of the file as it is. */
    juce::Result write (const juce::String& key, const juce::var& value);

    /** Whether controls give screen readers their help tags. On unless the file says otherwise. */
    bool helpTags() const noexcept { return helpTagsOn.load(); }

    /** Kept for the session even if it can't be saved. */
    juce::Result setHelpTags (bool shouldBeOn);

private:
    explicit Settings (juce::File);
    juce::File file;
    std::atomic<bool> helpTagsOn { true };
};
}
