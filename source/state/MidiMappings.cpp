#include "MidiMappings.h"
#include "params/Parameters.h"

namespace astralay::state
{
namespace
{
class MappingChange final : public juce::UndoableAction
{
public:
    MappingChange (std::function<void()> forward, std::function<void()> backward)
        : redo (std::move (forward)), reverse (std::move (backward)) {}
    bool perform() override { redo(); return true; }
    bool undo() override { reverse(); return true; }
private:
    std::function<void()> redo, reverse;
};
bool integer (const juce::var& v) { return v.isInt() || v.isInt64(); }
}

juce::String MidiMappings::Source::name() const
{
    return "Channel " + juce::String (channel) + (controller < 0 ? " pitch bend" : " CC " + juce::String (controller));
}

juce::File MidiMappings::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Astralay").getChildFile ("MIDI Mappings");
}

MidiMappings::MidiMappings (juce::AudioProcessorValueTreeState& state, History& h, juce::File folderToUse)
    : history (h), directory (std::move (folderToUse))
{
    sync = state.getParameter (params::global::sync);
    for (auto* p : state.processor.getParameters())
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (parameter == nullptr) continue;
        const auto id = parameter->paramID;
        if (id.endsWith ("_pitch_mode") || id.endsWith ("_timeSync")
            || id.contains ("_stutter_sync") || id == params::global::bufferSync) continue;
        auto& target = targets.emplace_back();
        target.id = id;
        target.parameter = parameter;
        // buffer size isn't a macro target, but is a logical MIDI time control.
        const auto counterpart = id == params::global::bufferSize ? juce::String (params::global::bufferSync)
                                                                 : params::syncedCounterpart (id);
        target.synced = state.getParameter (counterpart);
    }
    activeSources.resize (targets.size());
    scratchSources.resize (targets.size());
    directory.createDirectory();
    (void) scan();
    const auto file = defaultFile();
    if (file != juce::File())
    {
        Mapping initial;
        const auto contents = file.loadFileAsString();
        const auto result = parse (contents, initial);
        if (file.existsAsFile() && result.wasOk())
        {
            initial.file = file;
            initial.savedContents = contents;
            apply (initial);
        }
        else startupProblem = "Default MIDI mapping could not be loaded: " + file.getFileName();
    }
    startTimer (20);
}

MidiMappings::~MidiMappings() { stopTimer(); }
int MidiMappings::indexOf (const juce::String& id) const
{
    for (size_t i = 0; i < targets.size(); ++i)
        if (targets[i].id == id) return (int) i;
    return -1;
}
bool MidiMappings::eligible (const juce::String& id) const { return indexOf (id) >= 0; }
juce::String MidiMappings::logicalTarget (const juce::String& id) const
{
    for (const auto& target : targets)
        if (target.id == id || (target.synced != nullptr && target.synced->paramID == id)) return target.id;
    return {};
}
MidiMappings::Learn MidiMappings::learnState() const noexcept
{
    const auto value = learning.load();
    return value == 0 || (value >> 32) != 0 ? Learn::inactive : value == 1 ? Learn::target : Learn::midi;
}
void MidiMappings::waitForTarget() { const juce::ScopedLock lock (mappingLock); flushCapture(); learning.store (1); }
bool MidiMappings::learn (const juce::String& id)
{
    const juce::ScopedLock lock (mappingLock);
    flushCapture();
    const auto index = indexOf (id);
    if (index < 0) return false;
    learning.store ((uint64_t) index + 2);
    return true;
}
void MidiMappings::cancelLearn()
{
    const juce::ScopedLock lock (mappingLock);
    flushCapture();
    const auto previous = learning.exchange (0);
    // A capture racing cancellation is completed, never left half applied.
    if ((previous >> 32) != 0)
    {
        const auto index = (size_t) ((previous & 0xffffffffu) - 2);
        bind (targets[index].id, Source::fromCode ((int) (previous >> 32)));
    }
    else if (previous != 0 && announce) announce ("MIDI learn canceled");
}
float MidiMappings::normalisedPitch (int value) noexcept
{
    return value <= 8192 ? (float) value / 16384.0f : 0.5f + (float) (value - 8192) / 16382.0f;
}
bool MidiMappings::process (const juce::MidiMessage& message) noexcept
{
    if (! message.isController() && ! message.isPitchWheel()) return false;
    const Source source { message.getChannel(), message.isController() ? message.getControllerNumber() : -1 };
    const auto code = source.code();
    auto learnValue = learning.load();
    if (learnValue >= 2 && (learnValue >> 32) == 0)
    {
        if (learning.compare_exchange_strong (learnValue, learnValue | ((uint64_t) code << 32)))
            return false; // Suppress this event for every target, including existing bindings.
    }
    const auto value = message.isController() ? (float) message.getControllerValue() / 127.0f
                                               : normalisedPitch (message.getPitchWheelValue());
    const auto captured = learning.load();
    // Publish a whole mapping at once. Never wait for a UI/state writer; retain the previous
    // audio snapshot if it is currently writing. All route slots are atomic to avoid data races.
    const auto revision = routeRevision.load();
    if (revision != audioRevision && (revision & 1) == 0)
    {
        for (size_t i = 0; i < targets.size(); ++i) scratchSources[i] = targets[i].source.load();
        if (routeRevision.load() == revision)
        {
            activeSources.swap (scratchSources);
            audioRevision = revision;
        }
    }
    const auto capturedIndex = (captured >> 32) != 0 ? (int) (captured & 0xffffffffu) - 2 : -1;
    bool changed = false;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        auto& target = targets[i];
        const auto assigned = (int) i == capturedIndex ? (int) (captured >> 32) : activeSources[i];
        if (assigned != code) continue;
        auto* p = target.synced != nullptr && sync->getValue() >= 0.5f ? target.synced : target.parameter;
        const auto snapped = p->convertTo0to1 (p->convertFrom0to1 (value));
        if (! juce::approximatelyEqual (p->getValue(), snapped))
        {
            // No gestures: MIDI is automation, not an Astralay undo transaction.
            p->setValueNotifyingHost (snapped);
            changed = true;
        }
    }
    if (changed) soundChanged.store (true);
    return changed;
}
void MidiMappings::flushCapture()
{
    const juce::ScopedLock lock (mappingLock);
    const auto captured = learning.load();
    if ((captured >> 32) == 0) return;
    const auto index = (size_t) ((captured & 0xffffffffu) - 2);
    const auto source = Source::fromCode ((int) (captured >> 32));
    auto after = mapping;
    after.bindings[targets[index].id] = source;
    after.dirty = true;
    edit (std::move (after), "MIDI mapping");
    learning.store (0);
    const auto name = targets[index].id.startsWith ("macro")
        ? "Macro " + targets[index].id.substring (5) + " " + targets[index].parameter->getName (128)
        : targets[index].parameter->getName (128);
    if (announce) announce (name + " set to " + source.name());
}
void MidiMappings::timerCallback()
{
    flushCapture();
    if (soundChanged.exchange (false) && onSoundChanged) onSoundChanged();
}
void MidiMappings::apply (const Mapping& next)
{
    const juce::ScopedLock lock (mappingLock);
    mapping = next;
    routeRevision.fetch_add (1);
    for (auto& target : targets)
    {
        const auto found = mapping.bindings.find (target.id);
        target.source.store (found == mapping.bindings.end() ? 0 : found->second.code());
    }
    routeRevision.fetch_add (1);
}
void MidiMappings::edit (Mapping next, const juce::String& description)
{
    const auto before = mapping;
    const auto restore = [this] (const Mapping& value)
    {
        apply (value);
        if (onMappingChanged) onMappingChanged();
    };
    history.performAction (new MappingChange ([restore, next] { restore (next); }, [restore, before] { restore (before); }), description);
}
bool MidiMappings::bound (const juce::String& id) { const juce::ScopedLock lock (mappingLock); flushCapture(); return mapping.bindings.count (id) != 0; }
void MidiMappings::bind (const juce::String& id, Source source)
{
    const juce::ScopedLock lock (mappingLock);
    if (! eligible (id) || source.channel < 1 || source.channel > 16 || source.controller < -1 || source.controller > 127) return;
    auto next = mapping;
    next.bindings[id] = source;
    next.dirty = true;
    edit (std::move (next), "MIDI mapping");
}
void MidiMappings::remove (const juce::String& id)
{
    const juce::ScopedLock lock (mappingLock);
    cancelLearn();
    auto next = mapping;
    if (next.bindings.erase (id) == 0) return;
    next.dirty = true;
    if (next.bindings.empty()) next = {};
    edit (std::move (next), "remove MIDI mapping");
}
void MidiMappings::clear() { const juce::ScopedLock lock (mappingLock); cancelLearn(); edit ({}, "clear MIDI mapping"); }

juce::String MidiMappings::serialise (const Mapping& value) const
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "Astralay MIDI mapping");
    root->setProperty ("version", 1);
    juce::Array<juce::var> bindings;
    for (const auto& [id, source] : value.bindings)
    {
        auto item = std::make_unique<juce::DynamicObject>();
        item->setProperty ("target", id);
        item->setProperty ("type", source.controller < 0 ? "pitchBend" : "cc");
        item->setProperty ("channel", source.channel);
        if (source.controller >= 0) item->setProperty ("controller", source.controller);
        bindings.add (juce::var (item.release()));
    }
    root->setProperty ("bindings", bindings);
    return juce::JSON::toString (juce::var (root.release()));
}
juce::Result MidiMappings::parse (const juce::String& text, Mapping& result, bool allowEmpty) const
{
    juce::var root;
    if (juce::JSON::parse (text, root).failed() || ! root.isObject()
        || root["format"].toString() != "Astralay MIDI mapping" || ! integer (root["version"]) || (juce::int64) root["version"] != 1)
        return juce::Result::fail ("Not a supported Astralay MIDI mapping (version 1 required).");
    const auto* array = root["bindings"].getArray();
    if (array == nullptr || (! allowEmpty && array->isEmpty())) return juce::Result::fail ("A mapping file must contain bindings.");
    Mapping parsed;
    for (const auto& item : *array)
    {
        const auto id = item["target"].toString();
        const auto type = item["type"].toString();
        if (! item.isObject() || ! item["target"].isString() || ! eligible (id))
            return juce::Result::fail ("Unknown or ineligible MIDI target: " + id);
        if (! integer (item["channel"]) || (juce::int64) item["channel"] < 1 || (juce::int64) item["channel"] > 16)
            return juce::Result::fail ("MIDI channel must be an integer from 1 to 16.");
        if (type != "cc" && type != "pitchBend") return juce::Result::fail ("Unsupported MIDI source type.");
        if (type == "cc" && (! integer (item["controller"]) || (juce::int64) item["controller"] < 0 || (juce::int64) item["controller"] > 127))
            return juce::Result::fail ("CC number must be an integer from 0 to 127.");
        if (type == "pitchBend" && item.hasProperty ("controller")) return juce::Result::fail ("Pitch bend cannot have a CC number.");
        if (! parsed.bindings.emplace (id, Source { (int) item["channel"], type == "cc" ? (int) item["controller"] : -1 }).second)
            return juce::Result::fail ("Duplicate MIDI target: " + id);
    }
    result = std::move (parsed);
    return juce::Result::ok();
}
juce::Result MidiMappings::load (const juce::File& file)
{
    const juce::ScopedLock lock (mappingLock);
    cancelLearn();
    Mapping next;
    const auto contents = file.loadFileAsString();
    const auto result = parse (contents, next);
    if (result.failed()) return result;
    next.file = file;
    next.savedContents = contents;
    edit (std::move (next), "load MIDI mapping " + file.getFileNameWithoutExtension());
    return juce::Result::ok();
}
juce::Result MidiMappings::atomicWrite (const juce::File& file, const juce::String& text)
{
    if (file.getParentDirectory().createDirectory().failed()) return juce::Result::fail ("Could not create the mapping folder.");
    juce::TemporaryFile temporary (file);
    {
        juce::FileOutputStream stream (temporary.getFile());
        if (! stream.openedOk()) return juce::Result::fail ("Could not create the temporary mapping file.");
        stream.writeText (text, false, false, "\n");
        stream.flush();
        if (stream.getStatus().failed()) return juce::Result::fail ("Could not write the mapping file.");
    }
    if (! temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail ("Could not replace the mapping file.");
    return juce::Result::ok();
}
juce::Result MidiMappings::save (const juce::File& file, bool overwrite)
{
    const juce::ScopedLock lock (mappingLock);
    cancelLearn();
    if (mapping.bindings.empty()) return juce::Result::fail ("An empty mapping cannot be saved.");
    juce::String validated;
    const auto valid = filename (file.getFileName(), validated);
    if (valid.isNotEmpty() || validated != file.getFileName()) return juce::Result::fail (valid.isEmpty() ? "Use a .json filename." : valid);
    if (! overwrite && file.existsAsFile() && (file != mapping.file || externallyChanged()))
        return juce::Result::fail ("Overwrite confirmation required.");
    const auto contents = serialise (mapping);
    const auto result = atomicWrite (file, contents);
    if (result.failed()) return result;
    mapping.file = file;
    mapping.savedContents = file.loadFileAsString(); // Match the bytes after newline normalisation.
    mapping.dirty = false;
    if (onMappingChanged) onMappingChanged();
    (void) scan();
    return result;
}
bool MidiMappings::externallyChanged() const
{
    const juce::ScopedLock lock (mappingLock);
    return mapping.file != juce::File() && (! mapping.file.existsAsFile() || mapping.file.loadFileAsString() != mapping.savedContents);
}
std::vector<juce::File> MidiMappings::scan() const
{
    std::vector<juce::File> files;
    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        Mapping parsed;
        if (parse (file.loadFileAsString(), parsed).wasOk()) files.push_back (file);
    }
    std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b)
    { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return files;
}
juce::String MidiMappings::filename (const juce::String& input, juce::String& result)
{
    auto name = input.trim();
    if (name.containsAnyOf ("<>:\"/\\|?*")) return "Names cannot contain < > : \" / \\ | ? or *.";
    for (auto c : name) if (c < 32 || (c >= 127 && c <= 159)) return "Names cannot contain control characters.";
    if (name.endsWithChar ('.')) return "Names cannot end in a dot.";
    const auto dot = name.lastIndexOfChar ('.');
    if (dot >= 0) name = name.substring (0, dot);
    if (name.isEmpty()) return "Enter a mapping name.";
    if (name.endsWithChar ('.') || name.endsWithChar (' ')) return "Names cannot end in a dot or space before the extension.";
    const auto device = name.upToFirstOccurrenceOf (".", false, false).trimEnd().toUpperCase();
    if (device == "CON" || device == "PRN" || device == "AUX" || device == "NUL" || device == "CLOCK$"
        || device == "CONIN$" || device == "CONOUT$"
        || ((device.startsWith ("COM") || device.startsWith ("LPT")) && device.length() == 4
            && (juce::String ("123456789").containsChar (device[3]) || device[3] == 0x00b9
                || device[3] == 0x00b2 || device[3] == 0x00b3))) return "That name is reserved by Windows. Choose another name.";
    result = name + ".json";
    return {};
}
juce::File MidiMappings::defaultFile() const
{
    const auto root = juce::JSON::parse (directory.getParentDirectory().getChildFile ("MIDI-default.json"));
    const auto path = root["file"].toString();
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}
juce::Result MidiMappings::writeDefault (const juce::String& file)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("file", file);
    return atomicWrite (directory.getParentDirectory().getChildFile ("MIDI-default.json"), juce::JSON::toString (juce::var (object.release())));
}
juce::Result MidiMappings::clearDefault() { return writeDefault ({}); }
juce::Result MidiMappings::setDefault()
{
    const juce::ScopedLock lock (mappingLock);
    flushCapture();
    if (mapping.bindings.empty()) return clearDefault();
    Mapping parsed;
    if (mapping.dirty || mapping.file == juce::File() || externallyChanged()
        || parse (mapping.file.loadFileAsString(), parsed).failed()) return juce::Result::fail ("Save the current MIDI mapping first.");
    return writeDefault (mapping.file.getFullPathName());
}
std::unique_ptr<juce::XmlElement> MidiMappings::projectState()
{
    const juce::ScopedLock lock (mappingLock);
    // Hosts may request state on a worker thread. Include a completed capture without touching
    // the message-thread undo manager; the timer will commit its undo action later.
    auto snapshot = mapping;
    const auto captured = learning.load();
    if ((captured >> 32) != 0)
    {
        snapshot.bindings[targets[(size_t) ((captured & 0xffffffffu) - 2)].id] = Source::fromCode ((int) (captured >> 32));
        snapshot.dirty = true;
    }
    auto xml = std::make_unique<juce::XmlElement> ("MidiMapping");
    xml->setAttribute ("file", snapshot.file == juce::File() ? juce::String() : snapshot.file.getFullPathName());
    xml->setAttribute ("dirty", snapshot.dirty);
    xml->setAttribute ("baseline", snapshot.savedContents);
    xml->addTextElement (serialise (snapshot));
    return xml;
}
void MidiMappings::restoreProject (const juce::XmlElement* xml)
{
    const juce::ScopedLock lock (mappingLock);
    if (learning.exchange (0) != 0 && announce) announce ("MIDI learn canceled");
    soundChanged.store (false);
    Mapping restored;
    if (xml != nullptr && parse (xml->getAllSubText(), restored, true).wasOk() && ! restored.bindings.empty())
    {
        const auto file = xml->getStringAttribute ("file");
        if (juce::File::isAbsolutePath (file)) restored.file = juce::File (file);
        restored.dirty = xml->getBoolAttribute ("dirty");
        restored.savedContents = xml->getStringAttribute ("baseline");
    }
    apply (restored); // Missing legacy state explicitly restores empty, regardless of defaults.
}
}
