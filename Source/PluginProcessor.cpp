#include "PluginProcessor.h"
#include "PluginEditor.h"

LoudnessMeterAudioProcessor::LoudnessMeterAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

LoudnessMeterAudioProcessor::~LoudnessMeterAudioProcessor()
{
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
    dataStore.prepare(10.0); // 10 Hz update rate
    
    // Calculate samples per 100ms update
    samplesPerUpdate = static_cast<int>(sampleRate * 0.1);
    sampleCounter = 0;
    
    isPrepared = true;
}

void LoudnessMeterAudioProcessor::releaseResources()
{
    isPrepared = false;
    loudnessMeter.reset();
}

bool LoudnessMeterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
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
    
    return false;
}

void LoudnessMeterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    
    if (!isPrepared)
        return;
    
    // Process through loudness meter (doesn't modify audio)
    loudnessMeter.processBlock(buffer);
    
    // Update cached values and data store periodically
    sampleCounter += buffer.getNumSamples();
    
    if (sampleCounter >= samplesPerUpdate)
    {
        sampleCounter -= samplesPerUpdate;
        
        float m = loudnessMeter.getMomentaryLoudness();
        float s = loudnessMeter.getShortTermLoudness();
        
        momentaryLoudness.store(m, std::memory_order_release);
        shortTermLoudness.store(s, std::memory_order_release);
        
        dataStore.addPoint(m, s);
    }
}

bool LoudnessMeterAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* LoudnessMeterAudioProcessor::createEditor()
{
    return new LoudnessMeterAudioProcessorEditor(*this);
}

void LoudnessMeterAudioProcessor::getStateInformation(juce::MemoryBlock&)
{
}

void LoudnessMeterAudioProcessor::setStateInformation(const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoudnessMeterAudioProcessor();
}