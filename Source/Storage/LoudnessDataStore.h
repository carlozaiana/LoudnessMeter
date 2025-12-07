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
        double startTime{0.0};
        double endTime{0.0};
    };

    LoudnessDataStore();
    ~LoudnessDataStore() = default;

    void prepare(double updateRateHz);
    void reset();
    
    // Called from audio thread - lock-free
    void addPoint(float momentary, float shortTerm);
    
    struct RenderData
    {
        std::vector<MinMaxPoint> points;
        int lodLevel{0};
        double bucketDuration{0.1};
    };
    
    // Called from UI thread
    RenderData getDataForTimeRange(double startTime, double endTime, int maxPixels);
    
    double getCurrentTime() const;

private:
    // Lock-free ring buffer for recent data
    static constexpr size_t kRingBufferSize = 2048;
    std::array<LoudnessPoint, kRingBufferSize> ringBuffer;
    std::atomic<size_t> writeIndex{0};
    std::atomic<size_t> totalPointCount{0};
    
    // LOD levels with FIXED time-aligned boundaries
    // LOD 0: 0.1s buckets (native rate)
    // LOD 1: 0.4s buckets
    // LOD 2: 1.6s buckets
    // LOD 3: 6.4s buckets
    // LOD 4: 25.6s buckets
    static constexpr size_t kLodLevels = 5;
    static constexpr double kBaseBucketDuration = 0.1; // 100ms base
    static constexpr double kLodFactor = 4.0;
    
    struct LodLevel
    {
        std::vector<MinMaxPoint> buckets;
        double bucketDuration{0.1};
        
        // Current bucket being accumulated
        MinMaxPoint currentBucket{100.0f, -100.0f, 100.0f, -100.0f, 0.0, 0.0};
        bool hasCurrentBucket{false};
    };
    
    std::array<LodLevel, kLodLevels> lodLevels;
    std::mutex lodMutex;
    
    double updateRate{10.0};
    std::atomic<double> currentTimestamp{0.0};
    
    // Track what's been processed from ring buffer
    size_t lastProcessedCount{0};
    
    void processRingBufferToLOD();
    void addPointToLOD(const LoudnessPoint& point);
    
    // LOD selection with hysteresis
    int selectLodLevel(double timeRange, int maxPixels, int currentLod) const;
    int lastSelectedLod{0};
    double lastTimeRange{10.0};
    static constexpr double kLodHysteresis = 1.5; // 50% hysteresis band
};