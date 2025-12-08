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
    void drawFromImageStrips(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawCurrentValues(juce::Graphics& g);
    void drawZoomInfo(juce::Graphics& g);
    
    // Image strip management
    void initializeImageStrips();
    void updateImageStrips();
    void renderColumnToStrip(int lodLevel, int columnIndex, double startTime, double endTime);
    void downsampleToHigherLODs(int fromColumn);
    
    // Coordinate conversion
    float lufsToImageY(float lufs) const;
    int getLodLevelForTimeRange(double timeRange) const;
    
    LoudnessDataStore& dataStore;
    
    // Fixed Y range for rendering
    static constexpr float kMinLufs = -90.0f;
    static constexpr float kMaxLufs = 0.0f;
    static constexpr float kLufsRange = kMaxLufs - kMinLufs; // 90 dB
    
    // View state
    double viewTimeRange{10.0};      // Visible time range in seconds
    float viewMinLufs{-60.0f};       // Current Y view min (for display scaling)
    float viewMaxLufs{0.0f};         // Current Y view max (for display scaling)
    
    // Zoom limits
    static constexpr double kMinTimeRange = 0.5;
    static constexpr double kMaxTimeRange = 18000.0; // 5 hours
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
    // LOD 0: 100ms per pixel, max 1 minute (600 px)
    // LOD 1: 1s per pixel, max 10 minutes (600 px)
    // LOD 2: 3s per pixel, max 30 minutes (600 px)
    // LOD 3: 12s per pixel, max 120 minutes (600 px)
    // LOD 4: 60s per pixel, max 300 minutes (300 px)
    
    static constexpr int kNumLodLevels = 5;
    static constexpr int kImageHeight = 256; // Fixed height for rendering
    
    struct LodConfig
    {
        double secondsPerPixel;
        double maxTimeRange;
        int stripWidth;
    };
    
    static constexpr std::array<LodConfig, kNumLodLevels> kLodConfigs = {{
        { 0.1,    60.0,   600 },   // LOD 0: 100ms/px, 1 min
        { 1.0,    600.0,  600 },   // LOD 1: 1s/px, 10 min
        { 3.0,    1800.0, 600 },   // LOD 2: 3s/px, 30 min
        { 12.0,   7200.0, 600 },   // LOD 3: 12s/px, 120 min
        { 60.0,   18000.0, 300 }   // LOD 4: 60s/px, 300 min (5 hrs)
    }};
    
    struct ImageStrip
    {
        juce::Image image;
        int currentColumn{0};       // Next column to write
        double lastRenderedTime{0}; // Timestamp of last rendered data
        bool needsFullRedraw{true};
    };
    
    std::array<ImageStrip, kNumLodLevels> imageStrips;
    
    // Tracking
    double lastUpdateTime{0.0};
    int currentLodLevel{0};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessHistoryDisplay)
};