#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Storage/LoudnessDataStore.h"
#include <array>
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
    
    // Drawing to screen
    void drawBackground(juce::Graphics& g);
    void drawBitmapToScreen(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawCurrentValues(juce::Graphics& g);
    void drawZoomInfo(juce::Graphics& g);
    
    // Bitmap management
    void initializeBitmaps();
    void updateBitmaps();
    void renderChunkToBitmap(int lodLevel, double startTime, double endTime);
    
    // Helpers
    float lufsToNormalizedY(float lufs) const;
    int getLodForTimeRange(double timeRange) const;
    
    LoudnessDataStore& dataStore;
    
    // Fixed Y range for bitmap rendering
    static constexpr float kImageMinLufs = -90.0f;
    static constexpr float kImageMaxLufs = 0.0f;
    static constexpr float kImageLufsRange = 90.0f;
    
    // Display delay for buffering
    static constexpr double kDisplayDelay = 0.4; // 400ms
    
    // View state (what the user sees)
    double viewTimeRange{10.0};
    float viewMinLufs{-60.0f};
    float viewMaxLufs{0.0f};
    
    // Limits
    static constexpr double kMinTimeRange = 0.5;
    static constexpr double kMaxTimeRange = 18000.0;
    static constexpr float kMinLufsRange = 6.0f;
    static constexpr float kMaxLufsRange = 90.0f;
    
    // Current values for display boxes
    float currentMomentary{-100.0f};
    float currentShortTerm{-100.0f};
    
    // Mouse
    juce::Point<float> lastMousePos;
    bool isDragging{false};
    
    // Colors
    const juce::Colour bgColour{16, 30, 50};
    const juce::Colour momentaryColour{45, 132, 107};
    const juce::Colour shortTermColour{146, 173, 196};
    const juce::Colour gridColour = juce::Colour(255, 255, 255).withAlpha(0.12f);
    const juce::Colour textColour{200, 200, 200};
    
    // LOD configuration
    static constexpr int kNumLods = 5;
    static constexpr int kBitmapHeight = 512;
    
    struct LodConfig
    {
        double secondsPerPixel;
        double maxTimeRange;
        int bitmapWidth;
    };
    
    static constexpr std::array<LodConfig, kNumLods> kLodConfigs = {{
        { 0.1,    60.0,    1800 },  // LOD 0: 100ms/px, 3 min of pixels, for ≤1 min view
        { 0.5,    300.0,   1800 },  // LOD 1: 500ms/px, 15 min of pixels, for ≤5 min view
        { 2.0,    1200.0,  1800 },  // LOD 2: 2s/px, 1 hr of pixels, for ≤20 min view
        { 10.0,   7200.0,  2160 },  // LOD 3: 10s/px, 6 hr of pixels, for ≤2 hr view
        { 30.0,   18000.0, 1800 }   // LOD 4: 30s/px, 15 hr of pixels, for ≤5 hr view
    }};
    
    struct LodBitmap
    {
        juce::Image image;
        double renderedUpToTime{0.0};
    };
    
    std::array<LodBitmap, kNumLods> lodBitmaps;
    int activeLod{0};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessHistoryDisplay)
};