#include "PluginEditor.h"
#include "PluginProcessor.h"

AstralayEditor::AstralayEditor (AstralayProcessor& processor)
    : AudioProcessorEditor (processor)
{
    placeholder.setText ("Astralay", juce::dontSendNotification);
    placeholder.setJustificationType (juce::Justification::centred);
    placeholder.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (placeholder);

    setResizable (true, true);
    setResizeLimits (400, 300, 1600, 1200);
    setSize (800, 600);
}

void AstralayEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void AstralayEditor::resized()
{
    placeholder.setBounds (getLocalBounds());
    placeholder.setFont (juce::FontOptions (getHeight() / 12.0f));
}
