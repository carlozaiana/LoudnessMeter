#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Storage/LoudnessDataStore.h"
#include <atomic>

class LoudnessHistoryDisplay : public juce::Component,
                                private juce::Timer
{
public:
    explicit LoudnessHistoryDisplay(LoudnessDataStore& dataStore);
    ~LoudnessHistoryDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, 
                        const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    void setCurrentLoudness(float momentary, float shortTerm);

private:
    void timerCallback() override;
    
    void drawBackground(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawCurves(juce::Graphics& g);
    void drawCurrentValues(juce::Graphics& g);
    
    float timeToX(double time) const;
    float loudnessToY(float lufs) const;
    
    LoudnessDataStore& dataStore;
    
    double viewStartTime{-10.0};
    double viewTimeRange{10.0};
    float viewMinLufs{-60.0f};
    float viewMaxLufs{0.0f};
    
    static constexpr double kMinTimeRange = 1.0;
    static constexpr double kMaxTimeRange = 18000.0;
    static constexpr float kMinLufsRange = 6.0f;
    static constexpr float kMaxLufsRange = 80.0f;
    
    float currentMomentary{-100.0f};
    float currentShortTerm{-100.0f};
    
    juce::Point<float> lastMousePos;
    bool isDragging{false};
    
    const juce::Colour backgroundColour = juce::Colour(16, 30, 50);
    const juce::Colour momentaryColour = juce::Colour(45, 132, 107);
    const juce::Colour shortTermColour = juce::Colour(146, 173, 196);
    const juce::Colour gridColour = juce::Colour(255, 255, 255).withAlpha(0.12f);
    const juce::Colour textColour = juce::Colour(200, 200, 200);
    
    LoudnessDataStore::RenderData cachedRenderData;
    double cachedStartTime{0.0};
    double cachedEndTime{0.0};
    int cachedWidth{0};
    
    static constexpr double kScrollSmoothing = 0.15;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessHistoryDisplay)
};