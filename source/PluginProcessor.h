#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "params/Parameters.h"

class AstralayProcessor final : public juce::AudioProcessor
{
public:
    AstralayProcessor();
    ~AstralayProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return astralay::params::maxDelaySeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

    /** The tap shown in the editor (zero-based). Saved with the session but not a host parameter. */
    int getSelectedTap() const;
    void setSelectedTap (int tapIndex);

private:
    struct TapParameters
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* time = nullptr;
        std::atomic<float>* timeSync = nullptr;
        std::atomic<float>* volume = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* feedback = nullptr;
        std::atomic<float>* lowCut = nullptr;
        std::atomic<float>* highCut = nullptr;

        std::array<std::atomic<float>*, astralay::dsp::numGlitchTypes> probability {};
        std::atomic<float>* stutterMin = nullptr;
        std::atomic<float>* stutterMax = nullptr;
        std::atomic<float>* stutterSyncMin = nullptr;
        std::atomic<float>* stutterSyncMax = nullptr;
        std::atomic<float>* grainSizeMin = nullptr;
        std::atomic<float>* grainSizeMax = nullptr;
        std::atomic<float>* grainDensityMin = nullptr;
        std::atomic<float>* grainDensityMax = nullptr;
        std::atomic<float>* pitchMin = nullptr;
        std::atomic<float>* pitchMax = nullptr;
        std::atomic<float>* lpcMin = nullptr;
        std::atomic<float>* lpcMax = nullptr;
        std::atomic<float>* cepstralMin = nullptr;
        std::atomic<float>* cepstralMax = nullptr;
        std::atomic<float>* ringMin = nullptr;
        std::atomic<float>* ringMax = nullptr;
        std::atomic<float>* fmRatioMin = nullptr;
        std::atomic<float>* fmRatioMax = nullptr;
        std::atomic<float>* fmIndexMin = nullptr;
        std::atomic<float>* fmIndexMax = nullptr;
        std::atomic<float>* bitsMin = nullptr;
        std::atomic<float>* bitsMax = nullptr;
        std::atomic<float>* rateMin = nullptr;
        std::atomic<float>* rateMax = nullptr;
    };

    struct GlobalParameters
    {
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* glide = nullptr;
        std::atomic<float>* freeze = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outputGain = nullptr;
        std::atomic<float>* smearAmount = nullptr;
        std::atomic<float>* smearSize = nullptr;

        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* placement = nullptr;
        std::atomic<float>* bufferSize = nullptr;
        std::atomic<float>* bufferSync = nullptr;
        std::atomic<float>* maxGlitches = nullptr;
        std::atomic<float>* lengthMin = nullptr;
        std::atomic<float>* lengthMax = nullptr;
        std::atomic<float>* reproducible = nullptr;
        std::atomic<float>* seed = nullptr;
    };

    /** Tempo and transport from the host, with fallbacks when it provides none. */
    struct HostInfo
    {
        double bpm = astralay::params::fallbackTempo;
        double barLengthInQuarters = 4.0;
        bool playing = false;
        bool hasPosition = false;
        double ppq = 0.0;
    };

    HostInfo readHost() const;
    void updateEngineSettings();

    juce::AudioProcessorValueTreeState state;
    std::array<TapParameters, astralay::params::numTaps> tapParameters;
    GlobalParameters globalParameters;

    /** For each synced stutter slice choice, its index in NoteValues::all(). */
    std::vector<int> stutterNoteIndices;

    astralay::dsp::Engine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AstralayProcessor)
};
