#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>

class LoudnessDataStore
{
public:
    struct LoudnessPoint
    {
        float momentary{-100.0f};
        float shortTerm{-100.0f};
        double timestamp{0.0};
    };

    struct MinMaxPoint
    {
        float minMomentary{100.0f};
        float maxMomentary{-100.0f};
        float minShortTerm{100.0f};
        float maxShortTerm{-100.0f};
        double timestamp{0.0};
    };

    LoudnessDataStore();
    ~LoudnessDataStore() = default;

    void prepare(double updateRateHz);
    void reset();
    
    // Called from audio thread - lock-free
    void addPoint(float momentary, float shortTerm);
    
    struct RenderData
    {
        std::vector<LoudnessPoint> points;
        std::vector<MinMaxPoint> minMaxPoints;
        bool useMinMax{false};
        int lodLevel{0};
        double zoomFactor{1.0};
    };
    
    // Called from UI thread - uses mutex for LOD access, lock-free for recent data
    RenderData getDataForTimeRange(double startTime, double endTime, int maxPixels);
    
    double getCurrentTime() const;

private:
    // Lock-free ring buffer for recent data (last ~60 seconds at 10Hz = 600 points)
    static constexpr size_t kRingBufferSize = 1024;
    std::array<LoudnessPoint, kRingBufferSize> ringBuffer;
    std::atomic<size_t> writeIndex{0};
    std::atomic<size_t> pointCount{0};
    
    // LOD levels for historical data (protected by mutex)
    static constexpr size_t kLodLevels = 5;
    static constexpr size_t kLodFactor = 4;
    
    struct LodLevel
    {
        std::vector<MinMaxPoint> points;
        size_t reductionFactor{1};
        MinMaxPoint accumulator{100.0f, -100.0f, 100.0f, -100.0f, 0.0};
        size_t accumulatorCount{0};
    };
    
    std::array<LodLevel, kLodLevels> lodLevels;
    std::mutex lodMutex;
    
    // Timing
    double updateRate{10.0};
    std::atomic<double> currentTimestamp{0.0};
    
    // Process ring buffer data into LOD levels
    size_t lastProcessedIndex{0};
    void processRingBufferToLOD();
    
    int selectLodLevel(double timeRange, int maxPixels) const;
    
    // Helper to get points from ring buffer
    void getRecentPoints(double startTime, double endTime, int maxPixels, RenderData& result);
    void getLODPoints(double startTime, double endTime, int maxPixels, RenderData& result);
};