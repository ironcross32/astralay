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
    };

    struct GlobalParameters
    {
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* glide = nullptr;
        std::atomic<float>* freeze = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outputGain = nullptr;
    };

    struct Tempo
    {
        double bpm = astralay::params::fallbackTempo;
        double barLengthInQuarters = 4.0;
    };

    Tempo readTempo() const;
    void updateEngineSettings();

    juce::AudioProcessorValueTreeState state;
    std::array<TapParameters, astralay::params::numTaps> tapParameters;
    GlobalParameters globalParameters;

    astralay::dsp::Engine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AstralayProcessor)
};
