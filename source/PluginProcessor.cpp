#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/NoteValues.h"
#include "state/Presets.h"

namespace
{
    constexpr int stateVersion = 1;
    const juce::Identifier selectedTapProperty { "selectedTap" };
    const juce::Identifier presetNameProperty { "presetName" };
    const juce::Identifier presetModifiedProperty { "presetModified" };
    const juce::Identifier outputClipProperty { "outputClip" };
}

AstralayProcessor::AstralayProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "Astralay", astralay::params::createLayout())
{
    using namespace astralay::params;

    const auto get = [this] (const juce::String& id)
    {
        auto* p = state.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (int t = 0; t < numTaps; ++t)
    {
        auto& p = tapParameters[(size_t) t];
        p.enabled  = get (tapId (t, tap::enabled));
        p.time     = get (tapId (t, tap::time));
        p.timeSync = get (tapId (t, tap::timeSync));
        p.volume   = get (tapId (t, tap::volume));
        p.pan      = get (tapId (t, tap::pan));
        p.feedback = get (tapId (t, tap::feedback));
        p.lowCut   = get (tapId (t, tap::lowCut));
        p.highCut  = get (tapId (t, tap::highCut));

        // In GlitchType order.
        const std::array<const char*, astralay::dsp::numGlitchTypes> probabilities
        {
            tap::reverseProb, tap::stutterProb, tap::grainProb, tap::pitchProb, tap::lpcProb,
            tap::cepsProb, tap::ringProb, tap::fmProb, tap::crushProb
        };

        for (size_t i = 0; i < probabilities.size(); ++i)
            p.probability[i] = get (tapId (t, probabilities[i]));

        p.stutterMin      = get (tapId (t, tap::stutterMin));
        p.stutterMax      = get (tapId (t, tap::stutterMax));
        p.stutterSyncMin  = get (tapId (t, tap::stutterSyncMin));
        p.stutterSyncMax  = get (tapId (t, tap::stutterSyncMax));
        p.grainSizeMin    = get (tapId (t, tap::grainSizeMin));
        p.grainSizeMax    = get (tapId (t, tap::grainSizeMax));
        p.grainDensityMin = get (tapId (t, tap::grainDensMin));
        p.grainDensityMax = get (tapId (t, tap::grainDensMax));
        p.pitchMin        = get (tapId (t, tap::pitchMin));
        p.pitchMax        = get (tapId (t, tap::pitchMax));
        p.pitchSpeedMin   = get (tapId (t, tap::pitchSpeedMin));
        p.pitchSpeedMax   = get (tapId (t, tap::pitchSpeedMax));
        p.pitchMode       = get (tapId (t, tap::pitchMode));
        p.lpcMin          = get (tapId (t, tap::lpcMin));
        p.lpcMax          = get (tapId (t, tap::lpcMax));
        p.cepstralMin     = get (tapId (t, tap::cepsMin));
        p.cepstralMax     = get (tapId (t, tap::cepsMax));
        p.ringMin         = get (tapId (t, tap::ringMin));
        p.ringMax         = get (tapId (t, tap::ringMax));
        p.fmRatioMin      = get (tapId (t, tap::fmRatioMin));
        p.fmRatioMax      = get (tapId (t, tap::fmRatioMax));
        p.fmIndexMin      = get (tapId (t, tap::fmIndexMin));
        p.fmIndexMax      = get (tapId (t, tap::fmIndexMax));
        p.bitsMin         = get (tapId (t, tap::crushBitsMin));
        p.bitsMax         = get (tapId (t, tap::crushBitsMax));
        p.rateMin         = get (tapId (t, tap::crushRateMin));
        p.rateMax         = get (tapId (t, tap::crushRateMax));
    }

    globalParameters.sync       = get (global::sync);
    globalParameters.glide      = get (global::glide);
    globalParameters.freeze     = get (global::freeze);
    globalParameters.mix        = get (global::mix);
    globalParameters.outputGain = get (global::outputGain);
    globalParameters.smearAmount = get (global::smearAmount);
    globalParameters.smearSize  = get (global::smearSize);

    globalParameters.threshold    = get (global::threshold);
    globalParameters.placement    = get (global::placement);
    globalParameters.bufferSize   = get (global::bufferSize);
    globalParameters.bufferSync   = get (global::bufferSync);
    globalParameters.maxGlitches  = get (global::maxGlitches);
    globalParameters.lengthMin    = get (global::lengthMin);
    globalParameters.lengthMax    = get (global::lengthMax);
    globalParameters.reproducible = get (global::reproducible);
    globalParameters.seed         = get (global::seed);

    for (const auto& label : stutterSyncChoices())
        stutterNoteIndices.push_back (astralay::NoteValues::indexOf (label));

    setPresetInfo ("Init", false);

    history.onUserEdit = [this]
    {
        if (! isPresetModified())
            setPresetInfo (getPresetName(), true);
    };

    history.onSnapshotApplied = [this] (const astralay::state::History::Snapshot& snapshot)
    {
        setPresetInfo (snapshot.presetName, snapshot.modified);
    };

   #if ASTRALAY_DIAGNOSTICS
    diagnosticLog = astralay::state::DiagnosticLog::createIfRequested (*this);

    if (diagnosticLog != nullptr)
        engine.setDiagnostics (&diagnosticLog->getSink());
   #endif
}

juce::String AstralayProcessor::getPresetName() const
{
    return state.state.getProperty (presetNameProperty, "Init").toString();
}

bool AstralayProcessor::isPresetModified() const
{
    return (bool) state.state.getProperty (presetModifiedProperty, false);
}

void AstralayProcessor::setPresetInfo (const juce::String& name, bool modified)
{
    state.state.setProperty (presetNameProperty, name, nullptr);
    state.state.setProperty (presetModifiedProperty, modified, nullptr);
}

juce::String AstralayProcessor::applyPreset (const astralay::state::History::Snapshot& preset)
{
    const auto before = history.capture (getPresetName(), isPresetModified());
    history.applyAndRecord (before, preset, "load preset " + preset.presetName);
    return "Loaded " + preset.presetName;
}

juce::String AstralayProcessor::loadFactoryPreset (int index)
{
    const auto& presets = astralay::state::Presets::factory();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return "No such preset";

    return applyPreset (astralay::state::Presets::snapshotFor (presets[(size_t) index], *this));
}

juce::String AstralayProcessor::loadPresetFile (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    astralay::state::History::Snapshot preset;

    if (xml == nullptr || ! astralay::state::Presets::fromXml (*xml, *this, preset))
        return "Could not load " + file.getFileName() + ". It isn't an Astralay preset.";

    // The file name is the preset's name, even if it was renamed after saving.
    preset.presetName = file.getFileNameWithoutExtension();
    return applyPreset (preset);
}

juce::String AstralayProcessor::savePresetFile (const juce::File& file)
{
    const auto name = file.getFileNameWithoutExtension();
    const auto xml = astralay::state::Presets::toXml (*this, name);

    if (! file.getParentDirectory().createDirectory() || ! xml->writeTo (file))
        return "Could not save " + file.getFileName();

    setPresetInfo (name, false);
    return "Saved " + name;
}

void AstralayProcessor::applyEdit (const std::map<juce::String, float>& values, const juce::String& description,
                                   bool mergeWithPrevious)
{
    const auto before = history.capture (getPresetName(), isPresetModified());
    auto after = before;
    after.modified = true;

    for (const auto& [id, value] : values)
        after.values[id] = value;

    history.applyAndRecord (before, after, description, mergeWithPrevious);
}

void AstralayProcessor::copyTapSettings (int tapIndex, const juce::String& suffix)
{
    const auto prefix = astralay::params::tapId (tapIndex, "");

    copiedSuffix = suffix;
    copiedValues.clear();

    for (auto* parameter : getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter); withId != nullptr && withId->paramID.startsWith (prefix))
        {
            const auto key = withId->paramID.substring (prefix.length());

            if (suffix.isEmpty() ? key != astralay::params::tap::enabled : key == suffix)
                copiedValues[key] = parameter->getValue();
        }
    }
}

bool AstralayProcessor::pasteTapSettings (int tapIndex, const juce::String& suffix)
{
    if (copiedValues.empty() || suffix != copiedSuffix)
        return false;

    const auto all = tapIndex == allTaps;
    std::map<juce::String, float> values;

    for (int t = all ? 0 : tapIndex; t < (all ? astralay::params::numTaps : tapIndex + 1); ++t)
        for (const auto& [key, value] : copiedValues)
            values[astralay::params::tapId (t, key.toRawUTF8())] = value;

    const auto what = suffix.isEmpty() ? juce::String ("tap")
                                       : astralay::params::tapParameterName (0, suffix.toRawUTF8()).fromFirstOccurrenceOf ("Tap 1 ", false, false);

    applyEdit (values, "paste " + what + (all ? " to all" : ""));
    return true;
}

bool AstralayProcessor::stepSelectedTapTimes (int direction, bool mergeWithPrevious)
{
    using namespace astralay::params;

    const auto synced = globalParameters.sync->load() >= 0.5f;
    std::map<juce::String, float> values;

    for (int t = 0; t < numTaps; ++t)
    {
        if ((performanceSelection & (1u << t)) == 0 || tapParameters[(size_t) t].enabled->load() < 0.5f)
            continue;

        const auto id = tapId (t, synced ? tap::timeSync : tap::time);
        const auto* parameter = state.getParameter (id);
        const auto& range = parameter->getNormalisableRange();
        const auto current = range.convertFrom0to1 (parameter->getValue());

        const auto next = synced ? current + (float) direction
                                 : direction > 0 ? current * performanceTimeStep : current / performanceTimeStep;

        // The whole selection stops at the first limit, so the taps keep their spacing.
        const auto tolerance = (range.end - range.start) * 1.0e-6f;

        if (next > range.end + tolerance || next < range.start - tolerance)
            return false;

        values[id] = range.convertTo0to1 (juce::jlimit (range.start, range.end, next));
    }

    if (values.empty())
        return false;

    applyEdit (values, "tap times", mergeWithPrevious);
    return true;
}

bool AstralayProcessor::stepSmear (bool amount, int direction, bool mergeWithPrevious)
{
    using namespace astralay::params;

    const juce::String id (amount ? global::smearAmount : global::smearSize);
    const auto* parameter = state.getParameter (id);
    const auto& range = parameter->getNormalisableRange();
    const auto current = range.convertFrom0to1 (parameter->getValue());

    const auto step = amount ? performanceSmearAmountStep : performanceSmearSizeStep;
    const auto next = juce::jlimit (range.start, range.end, current + (float) direction * step);

    if (std::abs (next - current) <= (range.end - range.start) * 1.0e-6f)
        return false;

    applyEdit ({ { id, range.convertTo0to1 (next) } }, amount ? "smear amount" : "smear size", mergeWithPrevious);
    return true;
}

void AstralayProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, astralay::params::maxDelaySeconds);
    updateEngineSettings();
    engine.reset();
}

void AstralayProcessor::releaseResources()
{
}

bool AstralayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono in / stereo out and stereo in / stereo out only.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto input = layouts.getMainInputChannelSet();
    return input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo();
}

AstralayProcessor::HostInfo AstralayProcessor::readHost() const
{
    HostInfo info;

    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                info.bpm = *bpm;

            if (const auto signature = position->getTimeSignature(); signature.hasValue() && signature->denominator > 0)
                info.barLengthInQuarters = signature->numerator * 4.0 / signature->denominator;

            info.playing = position->getIsPlaying();

            if (const auto ppq = position->getPpqPosition(); ppq.hasValue())
            {
                info.hasPosition = true;
                info.ppq = *ppq;
            }
        }
    }

    return info;
}

void AstralayProcessor::updateEngineSettings()
{
    using namespace astralay;

    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const auto synced = globalParameters.sync->load() >= 0.5f;
    const auto host = readHost();
    const auto samplesPerQuarter = sampleRate * 60.0 / host.bpm;
    const auto& notes = NoteValues::all();

    const auto noteQuarters = [&] (int noteIndex)
    {
        return notes.getReference (juce::jlimit (0, notes.size() - 1, noteIndex)).lengthInQuarters (host.barLengthInQuarters);
    };

    const auto msToSamples = [sampleRate] (float ms) { return (float) (ms * 0.001 * sampleRate); };
    const auto load = [] (std::atomic<float>* p) { return p->load(); };
    const auto range = [&load] (std::atomic<float>* lo, std::atomic<float>* hi) { return dsp::RandomRange { load (lo), load (hi) }; };

    // Glitch chunks: note values on the beat grid when synced, milliseconds otherwise.
    const auto chunkQuarters = noteQuarters ((int) load (globalParameters.bufferSync));
    const auto chunkSamples = synced ? chunkQuarters * samplesPerQuarter
                                     : load (globalParameters.bufferSize) * 0.001 * sampleRate;

    dsp::GlobalSettings g;
    g.glideSeconds = load (globalParameters.glide) / 1000.0f;
    g.freeze = load (globalParameters.freeze) >= 0.5f;
    g.mix = load (globalParameters.mix) / 100.0f;
    g.outputGain = juce::Decibels::decibelsToGain (load (globalParameters.outputGain));
    g.clipCeiling = params::outputClipCeiling (outputClip.load());
    g.smearAmount = load (globalParameters.smearAmount) / 100.0f;
    g.smearSeconds = load (globalParameters.smearSize) / 1000.0f;
    g.glitch.threshold = load (globalParameters.threshold) / 100.0f;
    g.glitch.outputAndFeedback = (int) load (globalParameters.placement) == (int) params::GlitchPlacement::outputAndFeedback;
    g.glitch.chunkSamples = juce::jmax (1, (int) std::llround (juce::jmin (chunkSamples, params::maxDelaySeconds * sampleRate)));
    g.glitch.maxSimultaneous = (int) load (globalParameters.maxGlitches);
    g.glitch.lengthChunks = range (globalParameters.lengthMin, globalParameters.lengthMax);
    g.reproducible = load (globalParameters.reproducible) >= 0.5f;
    g.seed = (int) load (globalParameters.seed);
    engine.setGlobalSettings (g);

    dsp::TransportInfo transport;
    transport.playing = host.playing;
    transport.hasPosition = host.hasPosition;
    transport.ppq = host.ppq;
    transport.samplesPerQuarter = samplesPerQuarter;
    transport.synced = synced;
    transport.chunkQuarters = chunkQuarters;
    engine.setTransport (transport);

    for (int t = 0; t < params::numTaps; ++t)
    {
        const auto& p = tapParameters[(size_t) t];

        const auto seconds = synced ? noteQuarters ((int) load (p.timeSync)) * 60.0 / host.bpm
                                    : load (p.time) / 1000.0;

        dsp::TapSettings s;
        s.enabled = p.enabled->load() >= 0.5f;
        s.delaySamples = (float) (juce::jmin (seconds, params::maxDelaySeconds) * sampleRate);
        s.gain = params::volumeDbToGain (p.volume->load());
        s.pan = p.pan->load() / 100.0f;
        s.feedback = p.feedback->load() / 100.0f;
        s.lowCutHz = p.lowCut->load();
        s.highCutHz = p.highCut->load();

        auto& glitch = s.glitch;

        for (size_t i = 0; i < p.probability.size(); ++i)
            glitch.probability[i] = load (p.probability[i]) / 100.0f;

        if (synced)
        {
            const auto sliceSamples = [&] (std::atomic<float>* choice)
            {
                const auto index = juce::jlimit (0, (int) stutterNoteIndices.size() - 1, (int) load (choice));
                return (float) (noteQuarters (stutterNoteIndices[(size_t) index]) * samplesPerQuarter);
            };

            glitch.stutterSlice = { sliceSamples (p.stutterSyncMin), sliceSamples (p.stutterSyncMax) };
        }
        else
        {
            glitch.stutterSlice = { msToSamples (load (p.stutterMin)), msToSamples (load (p.stutterMax)) };
        }

        glitch.grainSize = { msToSamples (load (p.grainSizeMin)), msToSamples (load (p.grainSizeMax)) };
        glitch.grainDensity = range (p.grainDensityMin, p.grainDensityMax);
        glitch.pitch = range (p.pitchMin, p.pitchMax);
        glitch.pitchSpeed = range (p.pitchSpeedMin, p.pitchSpeedMax);
        glitch.varispeed = (int) load (p.pitchMode) == (int) params::PitchMode::varispeed;
        glitch.lpcShift = range (p.lpcMin, p.lpcMax);
        glitch.cepstralShift = range (p.cepstralMin, p.cepstralMax);
        glitch.ringFrequency = range (p.ringMin, p.ringMax);
        glitch.fmRatio = range (p.fmRatioMin, p.fmRatioMax);
        glitch.fmIndex = range (p.fmIndexMin, p.fmIndexMax);
        glitch.bits = range (p.bitsMin, p.bitsMax);
        glitch.rateReduction = range (p.rateMin, p.rateMax);

        engine.setTapSettings (t, s);
    }
}

void AstralayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    updateEngineSettings();

    const auto numSamples = buffer.getNumSamples();
    const auto monoInput = getTotalNumInputChannels() == 1;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    engine.process (left, monoInput ? nullptr : right, left, right, numSamples);
}

int AstralayProcessor::getSelectedTap() const
{
    return juce::jlimit (0, astralay::params::numTaps - 1, (int) state.state.getProperty (selectedTapProperty, 0));
}

void AstralayProcessor::setSelectedTap (int tapIndex)
{
    state.state.setProperty (selectedTapProperty, tapIndex, nullptr);
}

juce::AudioProcessorEditor* AstralayProcessor::createEditor()
{
    return new AstralayEditor (*this);
}

void AstralayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("version", stateVersion, nullptr);
    copy.setProperty (outputClipProperty, (int) outputClip.load(), nullptr);

    if (const auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void AstralayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    // replaceState leaves parameters missing from older states at their current values, so start
    // from defaults: anything absent from the saved state then takes its default value.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    state.replaceState (juce::ValueTree::fromXml (*xml));

    // Sessions saved before the output clip existed get its default.
    const auto clip = (int) state.state.getProperty (outputClipProperty, (int) astralay::params::OutputClip::plus18);
    outputClip.store ((astralay::params::OutputClip) juce::jlimit (0, (int) astralay::params::OutputClip::off, clip));

    // The undo history belongs to the settings that were just replaced.
    history.clear();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AstralayProcessor();
}
