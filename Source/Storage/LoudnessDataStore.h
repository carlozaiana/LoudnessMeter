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
        float momentaryMin{100.0f};
        float momentaryMax{-100.0f};
        float shortTermMin{100.0f};
        float shortTermMax{-100.0f};
        double timeMid{0.0};
        
        bool hasValidMomentary() const { return momentaryMax > -99.0f; }
        bool hasValidShortTerm() const { return shortTermMax > -99.0f; }
        
        void reset()
        {
            momentaryMin = 100.0f;
            momentaryMax = -100.0f;
            shortTermMin = 100.0f;
            shortTermMax = -100.0f;
            timeMid = 0.0;
        }
        
        void addSample(float m, float s, double t)
        {
            if (m > -100.0f)
            {
                momentaryMin = std::min(momentaryMin, m);
                momentaryMax = std::max(momentaryMax, m);
            }
            if (s > -100.0f)
            {
                shortTermMin = std::min(shortTermMin, s);
                shortTermMax = std::max(shortTermMax, s);
            }
            timeMid = t;
        }
        
        void merge(const MinMaxPoint& other)
        {
            if (other.hasValidMomentary())
            {
                if (hasValidMomentary())
                {
                    momentaryMin = std::min(momentaryMin, other.momentaryMin);
                    momentaryMax = std::max(momentaryMax, other.momentaryMax);
                }
                else
                {
                    momentaryMin = other.momentaryMin;
                    momentaryMax = other.momentaryMax;
                }
            }
            if (other.hasValidShortTerm())
            {
                if (hasValidShortTerm())
                {
                    shortTermMin = std::min(shortTermMin, other.shortTermMin);
                    shortTermMax = std::max(shortTermMax, other.shortTermMax);
                }
                else
                {
                    shortTermMin = other.shortTermMin;
                    shortTermMax = other.shortTermMax;
                }
            }
        }
    };
    
    struct QueryResult
    {
        std::vector<MinMaxPoint> points;
        int lodLevel{0};
        double bucketDuration{0.1};
        double queryStartTime{0.0};
        double queryEndTime{0.0};
    };

    LoudnessDataStore();
    ~LoudnessDataStore() = default;

    void prepare(double updateRateHz);
    void reset();
    
    void addPoint(float momentary, float shortTerm);
    
    double getCurrentTime() const;
    double getUpdateRate() const { return updateRate; }
    
    QueryResult getDataForTimeRange(double startTime, double endTime, int maxPoints) const;

private:
    void updateLodLevels(float momentary, float shortTerm, double timestamp);
    int selectLodLevel(double timeRange, int maxPoints) const;
    
    static constexpr size_t kMaxRawPoints = 180000;
    
    mutable std::mutex dataMutex;
    std::vector<LoudnessPoint> rawData;
    
    static constexpr int kNumLods = 6;
    
    struct LodLevel
    {
        std::vector<MinMaxPoint> buckets;
        double bucketDuration{0.1};
        double currentBucketStart{0.0};
        MinMaxPoint currentBucket;
        int samplesInCurrentBucket{0};
    };
    
    std::array<LodLevel, kNumLods> lodLevels;
    
    double updateRate{10.0};
    double sampleInterval{0.1};
    std::atomic<double> currentTimestamp{0.0};
};