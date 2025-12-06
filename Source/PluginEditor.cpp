#include "PluginProcessor.h"
#include "PluginEditor.h"

LoudnessMeterAudioProcessorEditor::LoudnessMeterAudioProcessorEditor(
    LoudnessMeterAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
{
    // Set size constraints
    constrainer.setSizeLimits(400, 200, 2000, 1000);
    
    // Create components after setting up the editor
    historyDisplay = std::make_unique<LoudnessHistoryDisplay>(p.getDataStore());
    addAndMakeVisible(*historyDisplay);
    
    resizer = std::make_unique<juce::ResizableCornerComponent>(this, &constrainer);
    addAndMakeVisible(*resizer);
    
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
    
    if (historyDisplay)
        historyDisplay->setBounds(bounds);
    
    if (resizer)
        resizer->setBounds(bounds.getWidth() - 16, bounds.getHeight() - 16, 16, 16);
}

void LoudnessMeterAudioProcessorEditor::timerCallback()
{
    if (historyDisplay)
    {
        historyDisplay->setCurrentLoudness(
            audioProcessor.getMomentaryLoudness(),
            audioProcessor.getShortTermLoudness()
        );
    }
}