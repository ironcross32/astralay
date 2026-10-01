#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "params/Parameters.h"
#include "state/DiagnosticLog.h"
#include "state/History.h"

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

    /** Where the output is hard clipped. Saved with the session but not a host parameter, not part
        of a preset and not undoable.
    */
    astralay::params::OutputClip getOutputClip() const noexcept { return outputClip.load(); }
    void setOutputClip (astralay::params::OutputClip clip) noexcept { outputClip.store (clip); }

    astralay::state::History& getHistory() noexcept { return history; }

    /** The current preset's name, and whether its settings have changed since it was loaded or
        saved. Both are saved with the session.
    */
    juce::String getPresetName() const;
    bool isPresetModified() const;

    /** Load or save presets as undoable steps. Each returns the text to announce. */
    juce::String loadFactoryPreset (int index);
    juce::String loadPresetFile (const juce::File& file);
    juce::String savePresetFile (const juce::File& file);

    /** Sets parameters, by ID and in normalised form, as one undoable step named description.
        mergeWithPrevious is as for History::applyAndRecord.
    */
    void applyEdit (const std::map<juce::String, float>& values, const juce::String& description,
                    bool mergeWithPrevious = false);

    static constexpr int allTaps = -1;

    /** Copies one tap's settings: the one with this parameter suffix, or with an empty suffix all
        of them except whether the tap is on. The copy lasts as long as this instance and isn't
        saved.
    */
    void copyTapSettings (int tapIndex, const juce::String& suffix);

    /** Pastes the copy onto a tap, or onto every tap with allTaps, as one undoable step. suffix
        says what the paste is aimed at, and must match what was copied. Returns false, changing
        nothing, if it doesn't or nothing was copied.
    */
    bool pasteTapSettings (int tapIndex, const juce::String& suffix);

    /** The taps the performance area acts on, as a bit per tap. All of them to begin with; lasts as
        long as this instance and isn't saved.
    */
    juce::uint32 getPerformanceSelection() const noexcept { return performanceSelection; }
    void setPerformanceSelection (juce::uint32 selection) noexcept { performanceSelection = selection & allTapsSelected; }

    static constexpr juce::uint32 allTapsSelected = (1u << astralay::params::numTaps) - 1;

    /** The factor one performance step scales a tap's time by. */
    static constexpr float performanceTimeStep = 1.1f;

    /** Makes the time of every selected tap that is on 10% longer or shorter, keeping their
        ratios, or moves each by one note value while host sync is on. If any of them would pass
        its limit, none move. Returns whether they moved.
    */
    bool stepSelectedTapTimes (int direction, bool mergeWithPrevious);

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
        std::atomic<float>* pitchSpeedMin = nullptr;
        std::atomic<float>* pitchSpeedMax = nullptr;
        std::atomic<float>* pitchMode = nullptr;
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
    void setPresetInfo (const juce::String& name, bool modified);
    juce::String applyPreset (const astralay::state::History::Snapshot& preset);

    juce::AudioProcessorValueTreeState state;
    astralay::state::History history { *this };
    std::array<TapParameters, astralay::params::numTaps> tapParameters;
    GlobalParameters globalParameters;

    /** For each synced stutter slice choice, its index in NoteValues::all(). */
    std::vector<int> stutterNoteIndices;

    astralay::dsp::Engine engine;
    std::atomic<astralay::params::OutputClip> outputClip { astralay::params::OutputClip::plus18 };

    juce::String copiedSuffix;
    std::map<juce::String, float> copiedValues;   // Normalised, by parameter suffix.
    juce::uint32 performanceSelection = allTapsSelected;

   #if ASTRALAY_DIAGNOSTICS
    // Last, so its thread stops before anything it reads is destroyed.
    std::unique_ptr<astralay::state::DiagnosticLog> diagnosticLog;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AstralayProcessor)
};
