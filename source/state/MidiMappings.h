#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <deque>
#include "History.h"

namespace astralay::state
{
/** Instance-owned MIDI bindings. UI/file operations run on the message thread. The audio
    thread only reads pre-resolved parameters and atomic source codes; it never parses JSON,
    allocates routes, touches files or records undo actions. */
class MidiMappings final : private juce::Timer
{
public:
    struct Source
    {
        int channel = 1;
        int controller = 0; // -1 is pitch bend; otherwise an independent CC.
        int code() const noexcept { return (channel - 1) * 129 + controller + 2; }
        static Source fromCode (int code) { return { (code - 1) / 129 + 1, (code - 1) % 129 - 1 }; }
        juce::String name() const;
        bool operator== (const Source&) const = default;
    };
    struct Mapping
    {
        std::map<juce::String, Source> bindings;
        juce::File file;
        juce::String savedContents;
        bool dirty = false;
    };
    enum class Learn { inactive, target, midi };

    MidiMappings (juce::AudioProcessorValueTreeState&, History&, juce::File folder = defaultFolder());
    ~MidiMappings() override;
    static juce::File defaultFolder();
    static float normalisedPitch (int value) noexcept;
    static juce::String filename (const juce::String& input, juce::String& result);

    juce::String logicalTarget (const juce::String& parameterId) const;
    bool eligible (const juce::String& target) const;
    Learn learnState() const noexcept;
    void waitForTarget();
    bool learn (const juce::String& target);
    void cancelLearn();
    // Called on the audio thread, before rendering the event's sample. True when sound changed.
    bool process (const juce::MidiMessage&) noexcept;
    // Also callable before UI/state operations, so a capture is committed without timer latency.
    void flushCapture();
    void setAnnouncer (std::function<void (const juce::String&)> callback)
    { const juce::ScopedLock lock (mappingLock); announce = std::move (callback); }
    std::function<void()> onSoundChanged;
    std::function<void()> onMappingChanged;

    Mapping current() { const juce::ScopedLock lock (mappingLock); flushCapture(); return mapping; }
    bool bound (const juce::String& target);
    void bind (const juce::String&, Source);
    void remove (const juce::String&);
    void clear();
    juce::Result load (const juce::File&);
    juce::Result save (const juce::File&, bool overwrite);
    bool externallyChanged() const;
    juce::Result setDefault();
    juce::Result clearDefault();
    juce::File defaultFile() const;
    const juce::String& startupStatus() const { return startupProblem; }
    const juce::File& folder() const { return directory; }
    std::vector<juce::File> scan() const;
    juce::String serialise (const Mapping&) const;
    juce::Result parse (const juce::String&, Mapping&, bool allowEmpty = false) const;
    std::unique_ptr<juce::XmlElement> projectState();
    void restoreProject (const juce::XmlElement*);

private:
    struct Target
    {
        juce::String id;
        juce::RangedAudioParameter* parameter = nullptr;
        juce::RangedAudioParameter* synced = nullptr;
        std::atomic<int> source { 0 };
    };
    int indexOf (const juce::String&) const;
    void apply (const Mapping&);
    void edit (Mapping, const juce::String&);
    void timerCallback() override;
    juce::Result writeDefault (const juce::String&);
    static juce::Result atomicWrite (const juce::File&, const juce::String&);

    History& history;
    mutable juce::CriticalSection mappingLock; // UI/state only; never acquired by process().
    juce::RangedAudioParameter* sync = nullptr;
    std::deque<Target> targets;
    std::vector<int> activeSources, scratchSources; // Allocated once, used only by the audio thread.
    std::atomic<uint64_t> routeRevision { 2 };
    uint64_t audioRevision = 0;
    std::function<void (const juce::String&)> announce;
    Mapping mapping;
    juce::File directory;
    juce::String startupProblem;
    // 0 inactive, 1 waiting for target, target index + 2 waiting for MIDI.
    // Captured source occupies bits 32..63. A single CAS publishes a complete capture.
    std::atomic<uint64_t> learning { 0 };
    std::atomic<bool> soundChanged { false };
};
}
