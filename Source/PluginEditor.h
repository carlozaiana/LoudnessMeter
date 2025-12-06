#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/LoudnessHistoryDisplay.h"

class LoudnessMeterAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit LoudnessMeterAudioProcessorEditor(LoudnessMeterAudioProcessor&);
    ~LoudnessMeterAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    
    LoudnessMeterAudioProcessor& audioProcessor;
    
    LoudnessHistoryDisplay historyDisplay;
    
    // Resize constraints
    juce::ComponentBoundsConstrainer constrainer;
    juce::ResizableCornerComponent resizer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessMeterAudioProcessorEditor)
};