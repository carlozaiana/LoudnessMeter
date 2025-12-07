#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Storage/LoudnessDataStore.h"

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
    void drawZoomInfo(juce::Graphics& g);
    
    void rebuildPaths();
    
    float timeToX(double time) const;
    float loudnessToY(float lufs) const;
    
    LoudnessDataStore& dataStore;
    
    // View state
    double viewStartTime{-10.0};
    double viewTimeRange{10.0};
    float viewMinLufs{-60.0f};
    float viewMaxLufs{0.0f};
    
    // Zoom limits
    static constexpr double kMinTimeRange = 0.5;
    static constexpr double kMaxTimeRange = 18000.0;
    static constexpr float kMinLufsRange = 6.0f;
    static constexpr float kMaxLufsRange = 80.0f;
    
    // Current loudness
    float currentMomentary{-100.0f};
    float currentShortTerm{-100.0f};
    
    // Mouse interaction
    juce::Point<float> lastMousePos;
    bool isDragging{false};
    
    // Zoom state - disable auto-scroll during zoom
    bool isZooming{false};
    double zoomCooldownTime{0.0};
    static constexpr double kZoomCooldownDuration = 0.3; // seconds after zoom before auto-scroll resumes
    
    // Colors
    const juce::Colour backgroundColour = juce::Colour(16, 30, 50);
    const juce::Colour momentaryColour = juce::Colour(45, 132, 107);
    const juce::Colour shortTermColour = juce::Colour(146, 173, 196);
    const juce::Colour gridColour = juce::Colour(255, 255, 255).withAlpha(0.15f);
    const juce::Colour textColour = juce::Colour(200, 200, 200);
    
    // Cached render data
    LoudnessDataStore::RenderData cachedRenderData;
    
    // Cached paths
    juce::Path momentaryLinePath;
    juce::Path shortTermLinePath;
    juce::Path momentaryFillPath;
    juce::Path shortTermFillPath;
    
    // State tracking
    double lastViewStartTime{0.0};
    double lastViewTimeRange{0.0};
    float lastViewMinLufs{-60.0f};
    float lastViewMaxLufs{0.0f};
    int lastWidth{0};
    int lastHeight{0};
    double lastDataTime{0.0};
    bool pathsNeedRebuild{true};
    
    // Time tracking for zoom cooldown
    double lastTimerTime{0.0};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessHistoryDisplay)
};