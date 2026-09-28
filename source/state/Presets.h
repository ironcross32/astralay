#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "History.h"

namespace astralay::state
{

/** Preset files: XML holding every parameter's value by ID, plus a version and the preset name.

    <AstralayPreset version="1" name="...">
      <Parameter id="t01_feedback" value="40"/>
      ...
    </AstralayPreset>

    Values are stored in the parameter's own units (40 for 40%), so the files are readable. On
    load, any parameter missing from the file takes its default.
*/
namespace Presets
{
    constexpr int version = 1;
    inline constexpr auto fileExtension = ".astralay";

    /** Documents/Astralay/Presets on Windows and macOS. Factory presets never live here. */
    juce::File userFolder();

    std::unique_ptr<juce::XmlElement> toXml (const juce::AudioProcessor& processor, const juce::String& name);

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

    History::Snapshot snapshotFor (const Factory& preset, const juce::AudioProcessor& processor);
}

} // namespace astralay::state
