#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "History.h"

namespace astralay::state
{

/** Preset files: XML holding every parameter's value by ID and the macro settings (see Macros.h),
    plus a version and the preset name.

    <AstralayPreset version="1" name="...">
      <Macros> ... </Macros>
      <Parameter id="t01_feedback" value="40"/>
      ...
    </AstralayPreset>

    Values are stored in the parameter's own units (40 for 40%), so the files are readable. On
    load, any parameter missing from the file takes its default, and so do the macro settings.
*/
namespace Presets
{
    constexpr int version = 1;
    inline constexpr auto fileExtension = ".astralay";

    /** The most characters a preset name can have. */
    constexpr int maxNameLength = 64;

    /** Documents/Astralay/Presets on Windows and macOS. Factory presets never live here. */
    juce::File userFolder();

    /** The preset files in a folder, in alphabetical order ignoring case. A preset's name is its
        file's name without the extension. Folders inside the folder are ignored.
    */
    juce::Array<juce::File> userPresets (const juce::File& folder);

    /** A name as it can be used for a file: without surrounding spaces or the characters file
        names can't hold. Empty if that leaves nothing usable.
    */
    juce::String legalName (const juce::String& typed);

    /** The file a preset of this name is saved to. The name must be a legal one. */
    juce::File fileFor (const juce::File& folder, const juce::String& name);

    /** Two or three random words joined by hyphens, such as "hollow-lantern". */
    juce::String randomName (juce::Random& random);

    /** A random name that no preset in the folder has. */
    juce::String unusedRandomName (const juce::File& folder, juce::Random& random);

    std::unique_ptr<juce::XmlElement> toXml (const juce::AudioProcessor& processor, const juce::String& name,
                                             const MacroSettings& macros = {});

    /** Reads a preset into a snapshot, with defaults for anything missing. Returns false if the
        XML isn't an Astralay preset.
    */
    bool fromXml (const juce::XmlElement& xml, const juce::AudioProcessor& processor, History::Snapshot& result);

    /** A factory preset: settings that differ from the defaults, written as the text a user would
        type into each control (for example "250 ms", "1/8 dotted", "100 left", "On").
    */
    struct Factory
    {
        juce::String name;
        std::vector<std::pair<juce::String, juce::String>> settings;
    };

    const std::vector<Factory>& factory();

    /** The index of the factory preset that is the plugin's defaults. Loading it gives a random
        name rather than its own, as a new instance has.
    */
    constexpr int initIndex = 0;

    History::Snapshot snapshotFor (const Factory& preset, const juce::AudioProcessor& processor);
}

} // namespace astralay::state
