#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Storage/LoudnessDataStore.h"
#include <array>

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
    
    // Drawing methods
    void drawBackground(juce::Graphics& g);
    void drawFromBitmap(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawCurrentValues(juce::Graphics& g);
    void drawZoomInfo(juce::Graphics& g);
    
    // Bitmap management
    void initializeBitmaps();
    void updateBitmaps();
    void renderCurveChunkToBitmap(int lodLevel, double chunkStartTime, double chunkEndTime);
    void clearBitmapRegion(int lodLevel, int startColumn, int numColumns);
    
    // Coordinate helpers
    float lufsToImageY(float lufs) const;
    float imageYToDisplayY(float imageY) const;
    int timeToColumn(double time, int lodLevel) const;
    int getLodLevelForTimeRange(double timeRange) const;
    
    LoudnessDataStore& dataStore;
    
    // Fixed Y range for bitmap rendering
    static constexpr float kMinLufs = -90.0f;
    static constexpr float kMaxLufs = 0.0f;
    static constexpr float kLufsRange = kMaxLufs - kMinLufs;
    
    // Display delay for smooth rendering (400ms)
    static constexpr double kDisplayDelaySeconds = 0.4;
    
    // View state
    double viewTimeRange{10.0};
    float viewMinLufs{-60.0f};
    float viewMaxLufs{0.0f};
    
    // Zoom limits
    static constexpr double kMinTimeRange = 0.5;
    static constexpr double kMaxTimeRange = 18000.0;
    static constexpr float kMinLufsRange = 6.0f;
    static constexpr float kMaxLufsRange = 90.0f;
    
    // Current loudness values
    float currentMomentary{-100.0f};
    float currentShortTerm{-100.0f};
    
    // Mouse interaction
    juce::Point<float> lastMousePos;
    bool isDragging{false};
    
    // Colors
    const juce::Colour backgroundColour = juce::Colour(16, 30, 50);
    const juce::Colour momentaryColour = juce::Colour(45, 132, 107);
    const juce::Colour shortTermColour = juce::Colour(146, 173, 196);
    const juce::Colour gridColour = juce::Colour(255, 255, 255).withAlpha(0.15f);
    const juce::Colour textColour = juce::Colour(200, 200, 200);
    
    // LOD configuration
    static constexpr int kNumLodLevels = 5;
    static constexpr int kImageHeight = 512;
    
    struct LodConfig
    {
        double secondsPerPixel;
        double maxTimeRange;
        int imageWidth;
        int chunkSizePixels;  // How many pixels to render at once
    };
    
    // LOD 0: 100ms/px, up to 1 min, render 4 pixels (400ms) at a time
    // LOD 1: 500ms/px, up to 5 min, render 4 pixels (2s) at a time
    // LOD 2: 2s/px, up to 20 min, render 4 pixels (8s) at a time
    // LOD 3: 10s/px, up to 2 hrs, render 4 pixels (40s) at a time
    // LOD 4: 30s/px, up to 5 hrs, render 4 pixels (2min) at a time
    static constexpr std::array<LodConfig, kNumLodLevels> kLodConfigs = {{
        { 0.1,   60.0,    1200, 4 },
        { 0.5,   300.0,   1200, 4 },
        { 2.0,   1200.0,  1200, 4 },
        { 10.0,  7200.0,  1440, 4 },
        { 30.0,  18000.0, 1200, 4 }
    }};
    
    struct BitmapStrip
    {
        juce::Image image;
        double lastRenderedTime{0.0};    // Time up to which we've rendered
        int totalColumnsRendered{0};      // Total columns rendered (for ring buffer)
    };
    
    std::array<BitmapStrip, kNumLodLevels> bitmapStrips;
    int currentLodLevel{0};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessHistoryDisplay)
};