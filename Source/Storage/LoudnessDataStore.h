#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

/**
 * Hierarchical Level-of-Detail storage for loudness data
 * 
 * Design for 5 hours at 10 Hz = 180,000 samples:
 * - Level 0: Full resolution (every sample)
 * - Level 1: 4:1 reduction (min/max pairs preserved)
 * - Level 2: 16:1 reduction
 * - Level 3: 64:1 reduction
 * - Level 4: 256:1 reduction
 * 
 * Memory-mapped files for data beyond memory threshold
 */
class LoudnessDataStore
{
public:
    struct LoudnessPoint
    {
        float momentary{-100.0f};
        float shortTerm{-100.0f};
        double timestamp{0.0}; // seconds from start
    };

    struct MinMaxPair
    {
        float minMomentary{0.0f};
        float maxMomentary{-100.0f};
        float minShortTerm{0.0f};
        float maxShortTerm{-100.0f};
    };

    LoudnessDataStore();
    ~LoudnessDataStore();

    void prepare(double updateRateHz);
    void reset();
    
    // Add new data point (called from timer thread)
    void addPoint(float momentary, float shortTerm);
    
    // Query data for rendering (called from UI thread)
    // Returns points between startTime and endTime at appropriate LOD
    struct RenderData
    {
        std::vector<LoudnessPoint> points;
        std::vector<MinMaxPair> minMaxBands; // For zoomed out view
        bool useMinMax{false};
        int lodLevel{0};
    };
    
    RenderData getDataForTimeRange(double startTime, double endTime, int maxPixels) const;
    
    // Get total duration in seconds
    double getTotalDuration() const;
    
    // Get current write position time
    double getCurrentTime() const;

private:
    static constexpr size_t kMemoryThreshold = 50000; // Points before spilling to disk
    static constexpr int kLodLevels = 5;
    static constexpr int kLodFactor = 4; // Reduction factor between levels
    
    struct LodLevel
    {
        std::vector<LoudnessPoint> points;
        std::vector<MinMaxPair> minMaxPairs;
        size_t reductionFactor{1};
        
        // Accumulator for building min/max
        MinMaxPair currentMinMax;
        int currentCount{0};
    };
    
    std::array<LodLevel, kLodLevels> lodLevels;
    
    double updateRate{10.0};
    std::atomic<size_t> totalPoints{0};
    std::atomic<double> currentTimestamp{0.0};
    
    mutable std::mutex dataMutex;
    
    // Disk storage for long sessions
    std::unique_ptr<juce::TemporaryFile> tempFile;
    std::unique_ptr<juce::FileOutputStream> diskStream;
    size_t pointsOnDisk{0};
    bool usingDiskStorage{false};
    
    void flushToDisk();
    void buildLodLevel(int level);
    int selectLodLevel(double timeRange, int maxPixels) const;
};