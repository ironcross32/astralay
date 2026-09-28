#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/NoteValues.h"

namespace
{
    constexpr int stateVersion = 1;
    const juce::Identifier selectedTapProperty { "selectedTap" };
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
    }

    globalParameters.sync       = get (global::sync);
    globalParameters.glide      = get (global::glide);
    globalParameters.freeze     = get (global::freeze);
    globalParameters.mix        = get (global::mix);
    globalParameters.outputGain = get (global::outputGain);
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

AstralayProcessor::Tempo AstralayProcessor::readTempo() const
{
    Tempo tempo;

    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                tempo.bpm = *bpm;

            if (const auto signature = position->getTimeSignature(); signature.hasValue() && signature->denominator > 0)
                tempo.barLengthInQuarters = signature->numerator * 4.0 / signature->denominator;
        }
    }

    return tempo;
}

void AstralayProcessor::updateEngineSettings()
{
    using namespace astralay;

    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const auto synced = globalParameters.sync->load() >= 0.5f;
    const auto tempo = synced ? readTempo() : Tempo {};
    const auto& notes = NoteValues::all();

    dsp::GlobalSettings g;
    g.glideSeconds = globalParameters.glide->load() / 1000.0f;
    g.freeze = globalParameters.freeze->load() >= 0.5f;
    g.mix = globalParameters.mix->load() / 100.0f;
    g.outputGain = juce::Decibels::decibelsToGain (globalParameters.outputGain->load());
    engine.setGlobalSettings (g);

    for (int t = 0; t < params::numTaps; ++t)
    {
        const auto& p = tapParameters[(size_t) t];

        double seconds;

        if (synced)
        {
            const auto index = juce::jlimit (0, notes.size() - 1, (int) p.timeSync->load());
            const auto quarters = notes.getReference (index).lengthInQuarters (tempo.barLengthInQuarters);
            seconds = quarters * 60.0 / tempo.bpm;
        }
        else
        {
            seconds = p.time->load() / 1000.0;
        }

        dsp::TapSettings s;
        s.enabled = p.enabled->load() >= 0.5f;
        s.delaySamples = (float) (juce::jmin (seconds, params::maxDelaySeconds) * sampleRate);
        s.gain = params::volumeDbToGain (p.volume->load());
        s.pan = p.pan->load() / 100.0f;
        s.feedback = p.feedback->load() / 100.0f;
        s.lowCutHz = p.lowCut->load();
        s.highCutHz = p.highCut->load();
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
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AstralayProcessor();
}
