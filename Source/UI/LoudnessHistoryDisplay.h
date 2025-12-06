#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include "../Storage/LoudnessDataStore.h"
#include <atomic>

/**
 * High-performance loudness history display with smooth scrolling
 * 
 * Features:
 * - OpenGL-accelerated rendering for smooth performance
 * - Anti-aliased curve rendering
 * - Mouse wheel zoom for X/Y axes
 * - Automatic LOD selection for consistent performance
 */
class LoudnessHistoryDisplay : public juce::Component,
                                public juce::OpenGLRenderer,
                                private juce::Timer
{
public:
    explicit LoudnessHistoryDisplay(LoudnessDataStore& dataStore);
    ~LoudnessHistoryDisplay() override;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, 
                        const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

    // OpenGL overrides
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // Update current loudness values for real-time display
    void setCurrentLoudness(float momentary, float shortTerm);

private:
    void timerCallback() override;
    
    // Rendering helpers
    void drawBackground(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawCurves(juce::Graphics& g);
    void drawCurrentValues(juce::Graphics& g);
    
    // Coordinate conversion
    float timeToX(double time) const;
    float loudnessToY(float lufs) const;
    double xToTime(float x) const;
    float yToLoudness(float y) const;
    
    // Data
    LoudnessDataStore& dataStore;
    
    // View state
    std::atomic<double> viewStartTime{-10.0}; // 10 seconds before current
    std::atomic<double> viewTimeRange{10.0};  // 10 seconds visible
    std::atomic<float> viewMinLufs{-60.0f};
    std::atomic<float> viewMaxLufs{0.0f};
    
    // Zoom limits
    static constexpr double kMinTimeRange = 1.0;      // 1 second minimum
    static constexpr double kMaxTimeRange = 18000.0;  // 5 hours maximum
    static constexpr float kMinLufsRange = 6.0f;      // 6 LUFS minimum range
    static constexpr float kMaxLufsRange = 80.0f;     // 80 LUFS maximum range
    
    // Current loudness for display
    std::atomic<float> currentMomentary{-100.0f};
    std::atomic<float> currentShortTerm{-100.0f};
    
    // Mouse interaction
    juce::Point<float> lastMousePos;
    bool isDragging{false};
    
    // Colors
    const juce::Colour backgroundColour{16, 30, 50};
    const juce::Colour momentaryColour{45, 132, 107};
    const juce::Colour shortTermColour{146, 173, 196};
    const juce::Colour gridColour{255, 255, 255, static_cast<juce::uint8>(30)};
    const juce::Colour textColour{200, 200, 200};
    
    // Cached render data
    LoudnessDataStore::RenderData cachedRenderData;
    double cachedStartTime{0.0};
    double cachedEndTime{0.0};
    int cachedWidth{0};
    bool needsDataUpdate{true};
    
    // OpenGL context
    juce::OpenGLContext openGLContext;
    bool useOpenGL{false};
    
    // Smooth scrolling
    double scrollAnimationTarget{0.0};
    double scrollAnimationCurrent{0.0};
    static constexpr double kScrollSmoothing = 0.15;
    
    // Path cache for anti-aliased rendering
    juce::Path momentaryPath;
    juce::Path shortTermPath;
    juce::Path momentaryEnvelopePath;
    juce::Path shortTermEnvelopePath;
    
    void updatePaths();
    void updateCachedData();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessHistoryDisplay)
};