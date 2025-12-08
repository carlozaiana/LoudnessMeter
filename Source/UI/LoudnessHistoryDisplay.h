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
    
    void drawBackground(juce::Graphics& g);
    void drawBitmapToScreen(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawCurrentValues(juce::Graphics& g);
    void drawZoomInfo(juce::Graphics& g);
    
    void initializeBitmaps();
    void updateBitmaps();
    void renderChunkToBitmap(int lodLevel, double startTime, double endTime);
    void blitTempToRingBuffer(int lodLevel, int startColumn, int numColumns);
    
    float lufsToNormalizedY(float lufs) const;
    int getLodForTimeRange(double timeRange) const;
    
    LoudnessDataStore& dataStore;
    
    static constexpr float kImageMinLufs = -90.0f;
    static constexpr float kImageMaxLufs = 0.0f;
    static constexpr float kImageLufsRange = 90.0f;
    
    static constexpr double kDisplayDelay = 0.4;
    
    double viewTimeRange{10.0};
    float viewMinLufs{-60.0f};
    float viewMaxLufs{0.0f};
    
    static constexpr double kMinTimeRange = 0.5;
    static constexpr double kMaxTimeRange = 18000.0;
    static constexpr float kMinLufsRange = 6.0f;
    static constexpr float kMaxLufsRange = 90.0f;
    
    float currentMomentary{-100.0f};
    float currentShortTerm{-100.0f};
    
    juce::Point<float> lastMousePos;
    bool isDragging{false};
    
    const juce::Colour bgColour{16, 30, 50};
    const juce::Colour momentaryColour{45, 132, 107};
    const juce::Colour shortTermColour{146, 173, 196};
    const juce::Colour gridColour = juce::Colour(255, 255, 255).withAlpha(0.12f);
    const juce::Colour textColour{200, 200, 200};
    
    static constexpr int kNumLods = 5;
    static constexpr int kBitmapHeight = 512;
    static constexpr int kChunkSize = 8;
    
    struct LodConfig
    {
        double secondsPerPixel;
        double maxTimeRange;
        int bitmapWidth;
    };
    
    static constexpr std::array<LodConfig, kNumLods> kLodConfigs = {{
        { 0.1,    60.0,    2400 },
        { 0.5,    300.0,   2400 },
        { 2.0,    1200.0,  2400 },
        { 10.0,   7200.0,  2880 },
        { 30.0,   18000.0, 2400 }
    }};
    
    struct LodBitmap
    {
        juce::Image image;
        juce::Image tempChunk;
        double renderedUpToTime{0.0};
        int totalColumnsRendered{0};
    };
    
    std::array<LodBitmap, kNumLods> lodBitmaps;
    int activeLod{0};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessHistoryDisplay)
};