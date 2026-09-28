#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class AstralayProcessor;

class AstralayEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AstralayEditor (AstralayProcessor&);
    ~AstralayEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label placeholder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AstralayEditor)
};
