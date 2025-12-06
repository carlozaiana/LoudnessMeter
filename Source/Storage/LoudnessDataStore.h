#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <array>

class LoudnessDataStore
{
public:
    struct LoudnessPoint
    {
        float momentary{-100.0f};
        float shortTerm{-100.0f};
        double timestamp{0.0};
    };

    struct MinMaxPair
    {
        float minMomentary{100.0f};
        float maxMomentary{-100.0f};
        float minShortTerm{100.0f};
        float maxShortTerm{-100.0f};
    };

    LoudnessDataStore();
    ~LoudnessDataStore() = default;

    void prepare(double updateRateHz);
    void reset();
    
    void addPoint(float momentary, float shortTerm);
    
    struct RenderData
    {
        std::vector<LoudnessPoint> points;
        std::vector<MinMaxPair> minMaxBands;
        bool useMinMax{false};
        int lodLevel{0};
    };
    
    RenderData getDataForTimeRange(double startTime, double endTime, int maxPixels) const;
    
    double getTotalDuration() const;
    double getCurrentTime() const;

private:
    static constexpr size_t kMemoryThreshold = 50000;
    static constexpr size_t kLodLevels = 5;
    static constexpr size_t kLodFactor = 4;
    
    struct LodLevel
    {
        std::vector<LoudnessPoint> points;
        std::vector<MinMaxPair> minMaxPairs;
        size_t reductionFactor{1};
        MinMaxPair currentMinMax{100.0f, -100.0f, 100.0f, -100.0f};
        int currentCount{0};
    };
    
    mutable std::mutex dataMutex;
    std::array<LodLevel, kLodLevels> lodLevels;
    
    double updateRate{10.0};
    std::atomic<size_t> totalPoints{0};
    std::atomic<double> currentTimestamp{0.0};
    
    int selectLodLevel(double timeRange, int maxPixels) const;
};