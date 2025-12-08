#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Storage/LoudnessDataStore.h"
#include <vector>

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
    
    void updateCachedData();
    void buildPaths();
    
    void drawBackground(juce::Graphics& g);
    void drawCurves(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawCurrentValues(juce::Graphics& g);
    void drawZoomInfo(juce::Graphics& g);
    
    float timeToX(double time) const;
    float lufsToY(float lufs) const;
    
    // Catmull-Rom spline helpers
    void addCatmullRomSpline(juce::Path& path, 
                              const std::vector<juce::Point<float>>& points,
                              bool startPath);
    
    LoudnessDataStore& dataStore;
    
    static constexpr double kDisplayDelay = 0.3;
    
    // View state
    double viewTimeRange{10.0};
    float viewMinLufs{-60.0f};
    float viewMaxLufs{0.0f};
    
    // Calculated display times (updated each frame)
    double displayStartTime{0.0};
    double displayEndTime{0.0};
    
    // View limits
    static constexpr double kMinTimeRange = 0.5;
    static constexpr double kMaxTimeRange = 18000.0;
    static constexpr float kMinLufsRange = 6.0f;
    static constexpr float kMaxLufsRange = 90.0f;
    static constexpr float kAbsoluteMinLufs = -90.0f;
    
    // Current values
    float currentMomentary{-100.0f};
    float currentShortTerm{-100.0f};
    
    // Cached data
    LoudnessDataStore::QueryResult cachedData;
    double lastQueryStartTime{-1.0};
    double lastQueryEndTime{-1.0};
    int lastQueryWidth{0};
    double lastDataTime{-1.0};
    
    // Cached paths (rebuilt when data or view changes)
    juce::Path momentaryFillPath;
    juce::Path momentaryLinePath;
    juce::Path shortTermFillPath;
    juce::Path shortTermLinePath;
    bool pathsValid{false};
    
    // Mouse
    juce::Point<float> lastMousePos;
    bool isDragging{false};
    
    // Colors
    const juce::Colour bgColour{16, 30, 50};
    const juce::Colour momentaryColour{45, 132, 107};
    const juce::Colour shortTermColour{146, 173, 196};
    const juce::Colour gridColour = juce::Colour(255, 255, 255).withAlpha(0.12f);
    const juce::Colour textColour{200, 200, 200};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessHistoryDisplay)
};