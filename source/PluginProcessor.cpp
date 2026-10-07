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

AstralayProcessor::AstralayProcessor (juce::File midiFolder)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "Astralay", astralay::params::createLayout()),
      midiMappings (state, history, std::move (midiFolder))
{
    using namespace astralay::params;

    // The engine reads a parameter that macros can move through its modulated value.
    const auto get = [this] (const juce::String& id) -> std::atomic<float>*
    {
        auto* p = state.getRawParameterValue (id);
        jassert (p != nullptr);

        if (modulationTarget (id).isEmpty())
            return p;

        auto& modulated = modulatedValues.emplace_back();
        modulated.parameter = state.getParameter (id);
        modulated.source = p;
        modulated.value = p->load();
        modulatedIndices[id] = (int) modulatedValues.size() - 1;
        return &modulated.value;
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
    globalParameters.freezeSustain = get (global::freezeSustain);
    globalParameters.mix        = get (global::mix);
    globalParameters.outputGain = get (global::outputGain);
    globalParameters.smearAmount = get (global::smearAmount);
    globalParameters.smearSize  = get (global::smearSize);

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

    for (int m = 0; m < numMacros; ++m)
    {
        macroParameters[(size_t) m] = dynamic_cast<MacroParameter*> (state.getParameter (macroId (m)));
        jassert (macroParameters[(size_t) m] != nullptr);
    }

    // Room for every macro to move every parameter, so the audio thread never allocates.
    pendingRoutes.reserve ((size_t) numMacros * modulatedValues.size());
    activeRoutes.reserve (pendingRoutes.capacity());

    setPresetInfo ("Init", false);
    midiMappings.onSoundChanged = [this]
    {
        if (midiModified.load())
            state.state.setProperty (presetModifiedProperty, true, nullptr);
    };
    midiMappings.onMappingChanged = [this]
    {
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    };

    history.onUserEdit = [this]
    {
        if (! isPresetModified())
            setPresetInfo (getPresetName(), true);
    };

    history.onSnapshotApplied = [this] (const astralay::state::History::Snapshot& snapshot)
    {
        applyMacros (snapshot.macros);
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
    return midiModified.load() || (bool) state.state.getProperty (presetModifiedProperty, false);
}

void AstralayProcessor::setPresetInfo (const juce::String& name, bool modified)
{
    midiModified.store (false);
    state.state.setProperty (presetNameProperty, name, nullptr);
    state.state.setProperty (presetModifiedProperty, modified, nullptr);
}

astralay::state::History::Snapshot AstralayProcessor::captureSnapshot() const
{
    auto snapshot = history.capture (getPresetName(), isPresetModified());
    snapshot.macros = getMacros();
    return snapshot;
}

juce::String AstralayProcessor::applyPreset (const astralay::state::History::Snapshot& preset)
{
    midiMappings.cancelSmoothing();
    const auto before = captureSnapshot();
    lastModulationEdit.clear();
    history.applyAndRecord (before, preset, "load preset " + preset.presetName);
    return "Loaded " + preset.presetName;
}

juce::String AstralayProcessor::loadFactoryPreset (int index)
{
    midiMappings.cancelLearn();
    const auto& presets = astralay::state::Presets::factory();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return "No such preset";

    return applyPreset (astralay::state::Presets::snapshotFor (presets[(size_t) index], *this));
}

juce::String AstralayProcessor::loadPresetFile (const juce::File& file)
{
    midiMappings.cancelLearn();
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
    const auto xml = astralay::state::Presets::toXml (*this, name, getMacros());

    if (! file.getParentDirectory().createDirectory() || ! xml->writeTo (file))
        return "Could not save " + file.getFileName();

    setPresetInfo (name, false);
    return "Saved " + name;
}

void AstralayProcessor::applyEdit (const std::map<juce::String, float>& values, const juce::String& description,
                                   bool mergeWithPrevious)
{
    const auto before = captureSnapshot();
    auto after = before;
    after.modified = true;

    for (const auto& [id, value] : values)
        after.values[id] = value;

    lastModulationEdit.clear();
    history.applyAndRecord (before, after, description, mergeWithPrevious);
}

//==============================================================================
astralay::state::MacroSettings AstralayProcessor::getMacros() const
{
    const juce::ScopedLock lock (macroLock);
    return macros;
}

void AstralayProcessor::applyMacros (const astralay::state::MacroSettings& settings)
{
    using namespace astralay::params;

    {
        const juce::ScopedLock lock (macroLock);
        macros = settings;
    }

    auto hostInfoChanged = false;
    std::vector<Route> routes;

    for (int m = 0; m < numMacros; ++m)
    {
        const auto& macro = settings[(size_t) m];
        auto* parameter = macroParameters[(size_t) m];
        const auto name = astralay::state::macroName (settings, m);

        if (parameter->isBipolar() != macro.bipolar || parameter->getName (1024) != name)
        {
            parameter->setDisplayName (macro.name);
            parameter->setBipolar (macro.bipolar);
            hostInfoChanged = true;
        }

        for (const auto& modulation : macro.modulations)
        {
            const auto found = modulatedIndices.find (modulation.parameterId);

            if (! canModulate (modulation.parameterId) || found == modulatedIndices.end())
                continue;

            routes.push_back ({ m, found->second, modulation.amount });

            // The note value that takes a time's place while host sync is on moves by the same
            // share of its own range, so the modulation holds whichever of the two is in use.
            const auto synced = modulatedIndices.find (syncedCounterpart (modulation.parameterId));

            if (synced != modulatedIndices.end())
            {
                const auto width = [this] (int index)
                {
                    const auto& range = modulatedValues[(size_t) index].parameter->getNormalisableRange();
                    return range.end - range.start;
                };

                routes.push_back ({ m, synced->second, modulation.amount * width (synced->second) / width (found->second) });
            }
        }
    }

    {
        const juce::SpinLock::ScopedLockType lock (routeLock);
        pendingRoutes.assign (routes.begin(), routes.end());
        routesChanged = true;
    }

    // The macro parameters' names and ranges, as the host shows them.
    if (hostInfoChanged)
        updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));

    if (juce::MessageManager::existsAndIsCurrentThread())
        macroChanges.sendSynchronousChangeMessage();
    else
        macroChanges.sendChangeMessage();
}

void AstralayProcessor::editMacros (const astralay::state::MacroSettings& settings, const juce::String& description,
                                    bool mergeWithPrevious, const std::map<juce::String, float>& values)
{
    const auto before = captureSnapshot();
    auto after = before;
    after.modified = true;
    after.macros = settings;

    for (const auto& [id, value] : values)
        after.values[id] = value;

    lastModulationEdit.clear();
    history.applyAndRecord (before, after, description, mergeWithPrevious);
}

void AstralayProcessor::renameMacro (int macroIndex, const juce::String& name)
{
    auto settings = getMacros();
    const auto oldName = astralay::state::macroName (settings, macroIndex);
    const auto trimmed = name.trim().substring (0, 64);
    auto& stored = settings[(size_t) macroIndex].name;

    stored = trimmed == astralay::params::defaultMacroName (macroIndex) ? juce::String() : trimmed;

    if (astralay::state::macroName (settings, macroIndex) != oldName)
        editMacros (settings, "rename " + oldName);
}

void AstralayProcessor::setMacroBipolar (int macroIndex, bool bipolar)
{
    using astralay::params::MacroParameter;

    auto settings = getMacros();
    auto& macro = settings[(size_t) macroIndex];

    if (macro.bipolar == bipolar)
        return;

    macro.bipolar = bipolar;

    const auto value = macroParameters[(size_t) macroIndex]->getMacroValue();
    const auto normalised = MacroParameter::toNormalised (bipolar ? value : juce::jmax (0.0f, value), bipolar);

    editMacros (settings, astralay::state::macroName (settings, macroIndex) + (bipolar ? " bipolar" : " unipolar"), false,
                { { astralay::params::macroId (macroIndex), normalised } });
}

void AstralayProcessor::setModulation (int macroIndex, const juce::String& parameterId, float amount)
{
    const auto found = modulatedIndices.find (parameterId);

    if (! astralay::params::canModulate (parameterId) || found == modulatedIndices.end())
        return;

    auto settings = getMacros();
    auto& modulations = settings[(size_t) macroIndex].modulations;

    // Up to the width of the parameter's range either way.
    const auto& range = modulatedValues[(size_t) found->second].parameter->getNormalisableRange();
    const auto limit = range.end - range.start;
    const auto newAmount = juce::jlimit (-limit, limit, amount);
    const auto remove = std::abs (newAmount) < limit * 1.0e-6f;
    const auto existing = std::find_if (modulations.begin(), modulations.end(),
                                        [&parameterId] (const auto& m) { return m.parameterId == parameterId; });

    if (existing == modulations.end())
    {
        if (remove)
            return;

        modulations.push_back ({ parameterId, newAmount });
    }
    else if (remove)
    {
        modulations.erase (existing);
    }
    else if (juce::approximatelyEqual (existing->amount, newAmount))
    {
        return;
    }
    else
    {
        existing->amount = newAmount;
    }

    const auto edit = juce::String (macroIndex) + ":" + parameterId;
    const auto now = juce::Time::getMillisecondCounter();
    const auto merge = edit == lastModulationEdit && now - lastModulationEditTime <= astralay::state::History::mergeWindowMs;

    editMacros (settings, "modulation of " + modulatedValues[(size_t) found->second].parameter->getName (128)
                              + " by " + astralay::state::macroName (settings, macroIndex), merge);

    lastModulationEdit = edit;
    lastModulationEditTime = now;
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
    midiMappings.prepare (sampleRate);
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

            if (const auto samples = position->getTimeInSamples(); samples.hasValue())
                info.samplePosition = *samples;

            if (const auto ppq = position->getPpqPosition(); ppq.hasValue())
            {
                info.hasPosition = true;
                info.ppq = *ppq;
            }
        }
    }

    return info;
}

void AstralayProcessor::updateModulatedValues()
{
    using namespace astralay::params;

    if (routesChanged.load())
    {
        // If the message thread is in the middle of leaving new routes, they wait for the next block.
        const juce::SpinLock::ScopedTryLockType lock (routeLock);

        if (lock.isLocked())
        {
            std::swap (activeRoutes, pendingRoutes);
            routesChanged = false;
        }
    }

    std::array<float, numMacros> macroValues;

    for (size_t m = 0; m < macroValues.size(); ++m)
        macroValues[m] = macroParameters[m]->getMacroValue();

    for (const auto& route : activeRoutes)
        modulatedValues[(size_t) route.target].offset += macroValues[(size_t) route.macro] * route.amount;

    for (auto& modulated : modulatedValues)
    {
        const auto value = modulated.source->load();
        const auto offset = std::exchange (modulated.offset, 0.0f);

        if (juce::exactlyEqual (offset, 0.0f))
        {
            modulated.value = value;
            continue;
        }

        // Amounts are in the parameter's own unit. Stepped parameters land on their steps.
        const auto& range = modulated.parameter->getNormalisableRange();
        modulated.value = range.snapToLegalValue (juce::jlimit (range.start, range.end, value + offset));
    }
}

void AstralayProcessor::updateEngineSettings (int sampleOffset)
{
    using namespace astralay;

    updateModulatedValues();

    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const auto synced = globalParameters.sync->load() >= 0.5f;
    auto host = readHost();
    if (host.playing)
    {
        host.ppq += (double) sampleOffset * host.bpm / (sampleRate * 60.0);
        if (host.samplePosition) *host.samplePosition += sampleOffset;
    }
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
    g.freezeSustain = load (globalParameters.freezeSustain) >= 0.5f;
    g.mix = load (globalParameters.mix) / 100.0f;
    g.outputGain = juce::Decibels::decibelsToGain (load (globalParameters.outputGain));
    g.clipCeiling = params::outputClipCeiling (outputClip.load());
    g.smearAmount = load (globalParameters.smearAmount) / 100.0f;
    g.smearSeconds = load (globalParameters.smearSize) / 1000.0f;
    g.glitch.stopped = glitchesStopped.load();
    g.glitch.outputAndFeedback = (int) load (globalParameters.placement) == (int) params::GlitchPlacement::outputAndFeedback;
    g.glitch.chunkSamples = juce::jmax (1, (int) std::llround (juce::jmin (chunkSamples, params::maxDelaySeconds * sampleRate)));
    g.glitch.maxSimultaneous = (int) load (globalParameters.maxGlitches);
    g.glitch.lengthChunks = range (globalParameters.lengthMin, globalParameters.lengthMax);
    g.reproducible = load (globalParameters.reproducible) >= 0.5f;
    g.seed = (int) load (globalParameters.seed);
    engine.setGlobalSettings (g);

    dsp::TransportInfo transport;
    transport.playing = host.playing;
    transport.samplePosition = host.samplePosition;
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

void AstralayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto monoInput = getTotalNumInputChannels() == 1;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    int cursor = 0;
    const auto renderTo = [&] (int end)
    {
        while (cursor < end)
        {
            if (midiMappings.refreshSmoothing()) midiModified.store (true);
            const auto count = juce::jmin (end - cursor, midiMappings.samplesUntilSmoothingUpdate());
            updateEngineSettings (cursor);
            engine.process (left + cursor, monoInput ? nullptr : right + cursor,
                            left + cursor, right + cursor, count);
            if (midiMappings.advanceSmoothing (count)) midiModified.store (true);
            cursor += count;
        }
    };
    for (const auto metadata : midi)
    {
        if (metadata.numBytes < 3 || ((metadata.data[0] & 0xf0) != 0xb0 && (metadata.data[0] & 0xf0) != 0xe0)) continue;
        const auto message = metadata.getMessage();
        if (! message.isController() && ! message.isPitchWheel()) continue;
        renderTo (juce::jlimit (0, numSamples, metadata.samplePosition));
        if (midiMappings.process (message)) midiModified.store (true);
    }
    renderTo (numSamples);
    // Leave every MIDI byte and timestamp in the host's buffer untouched.
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
    copy.setProperty (presetModifiedProperty, isPresetModified(), nullptr);

    if (const auto xml = copy.createXml())
    {
        xml->addChildElement (astralay::state::Macros::toXml (getMacros()).release());
        xml->addChildElement (midiMappings.projectState().release());
        copyXmlToBinary (*xml, destData);
    }
}

void AstralayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    midiMappings.restoreProject (xml->getChildByName ("MidiMapping"));
    xml->removeChildElement (xml->getChildByName ("MidiMapping"), true);
    midiModified.store (false);

    // Before the parameters, since a macro's saved value is in the range these settings give it.
    // Sessions saved before there were macros get the default settings. The element is taken out
    // so that it doesn't stay in the parameter state.
    auto* macrosXml = xml->getChildByName (astralay::state::Macros::rootTag);
    applyMacros (astralay::state::Macros::fromXml (macrosXml));
    xml->removeChildElement (macrosXml, true);

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
    lastModulationEdit.clear();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AstralayProcessor();
}
