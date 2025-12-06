#include "PluginProcessor.h"
#include "PluginEditor.h"

LoudnessMeterAudioProcessor::LoudnessMeterAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    dataStore.prepare(kDataUpdateRateHz);
}

LoudnessMeterAudioProcessor::~LoudnessMeterAudioProcessor()
{
    stopTimer();
}

const juce::String LoudnessMeterAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LoudnessMeterAudioProcessor::acceptsMidi() const { return false; }
bool LoudnessMeterAudioProcessor::producesMidi() const { return false; }
bool LoudnessMeterAudioProcessor::isMidiEffect() const { return false; }
double LoudnessMeterAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int LoudnessMeterAudioProcessor::getNumPrograms() { return 1; }
int LoudnessMeterAudioProcessor::getCurrentProgram() { return 0; }
void LoudnessMeterAudioProcessor::setCurrentProgram(int) {}
const juce::String LoudnessMeterAudioProcessor::getProgramName(int) { return {}; }
void LoudnessMeterAudioProcessor::changeProgramName(int, const juce::String&) {}

void LoudnessMeterAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    loudnessMeter.prepare(sampleRate, samplesPerBlock, getTotalNumInputChannels());
    dataStore.reset();
    
    // Start timer for data collection (100ms = 10 Hz)
    startTimerHz(static_cast<int>(kDataUpdateRateHz));
}

void LoudnessMeterAudioProcessor::releaseResources()
{
    stopTimer();
    loudnessMeter.reset();
}

bool LoudnessMeterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Support mono, stereo, and surround configurations
    const auto& mainInput = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    
    // Input and output must match
    if (mainInput != mainOutput)
        return false;
    
    // Support common configurations
    if (mainInput.isDisabled())
        return false;
    
    if (mainInput == juce::AudioChannelSet::mono())
        return true;
    if (mainInput == juce::AudioChannelSet::stereo())
        return true;
    if (mainInput == juce::AudioChannelSet::createLCR())
        return true;
    if (mainInput == juce::AudioChannelSet::quadraphonic())
        return true;
    if (mainInput == juce::AudioChannelSet::create5point0())
        return true;
    if (mainInput == juce::AudioChannelSet::create5point1())
        return true;
    if (mainInput == juce::AudioChannelSet::create7point1())
        return true;
    
    return false;
}

void LoudnessMeterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    
    // Process through loudness meter (doesn't modify audio)
    loudnessMeter.processBlock(buffer);
    
    // Pass audio through unchanged
}

void LoudnessMeterAudioProcessor::timerCallback()
{
    // Called at 10 Hz to store loudness data
    float momentary = loudnessMeter.getMomentaryLoudness();
    float shortTerm = loudnessMeter.getShortTermLoudness();
    
    dataStore.addPoint(momentary, shortTerm);
}

bool LoudnessMeterAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* LoudnessMeterAudioProcessor::createEditor()
{
    return new LoudnessMeterAudioProcessorEditor(*this);
}

void LoudnessMeterAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Store any plugin state here if needed
    juce::ignoreUnused(destData);
}

void LoudnessMeterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restore plugin state here if needed
    juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoudnessMeterAudioProcessor();
}