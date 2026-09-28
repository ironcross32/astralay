#include "PluginProcessor.h"
#include "PluginEditor.h"

AstralayProcessor::AstralayProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AstralayProcessor::prepareToPlay (double, int)
{
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

void AstralayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputs = getTotalNumInputChannels();
    const auto numOutputs = getTotalNumOutputChannels();

    // Pass-through for now. With a mono input, duplicate it to both outputs.
    if (numInputs == 1 && numOutputs > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
    else
        for (auto channel = numInputs; channel < numOutputs; ++channel)
            buffer.clear (channel, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* AstralayProcessor::createEditor()
{
    return new AstralayEditor (*this);
}

void AstralayProcessor::getStateInformation (juce::MemoryBlock&)
{
}

void AstralayProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AstralayProcessor();
}
