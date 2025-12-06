#include "PluginProcessor.h"
#include "PluginEditor.h"

LoudnessMeterAudioProcessorEditor::LoudnessMeterAudioProcessorEditor(
    LoudnessMeterAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , historyDisplay(p.getDataStore())
    , resizer(this, &constrainer)
{
    // Set size constraints
    constrainer.setSizeLimits(400, 200, 2000, 1000);
    
    addAndMakeVisible(historyDisplay);
    addAndMakeVisible(resizer);
    
    // Set initial size
    setSize(800, 400);
    setResizable(true, true);
    
    // Start update timer for current loudness values (30 Hz)
    startTimerHz(30);
}

LoudnessMeterAudioProcessorEditor::~LoudnessMeterAudioProcessorEditor()
{
    stopTimer();
}

void LoudnessMeterAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(16, 30, 50));
}

void LoudnessMeterAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    
    historyDisplay.setBounds(bounds);
    
    // Position resizer in bottom-right corner
    resizer.setBounds(bounds.getWidth() - 16, bounds.getHeight() - 16, 16, 16);
}

void LoudnessMeterAudioProcessorEditor::timerCallback()
{
    // Update display with current loudness values
    historyDisplay.setCurrentLoudness(
        audioProcessor.getMomentaryLoudness(),
        audioProcessor.getShortTermLoudness()
    );
}