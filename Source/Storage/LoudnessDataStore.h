#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>

class LoudnessDataStore
{
public:
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
    };
    
    struct QueryResult
    {
        std::vector<MinMaxPoint> points;
        int lodLevel{0};
        double bucketDuration{0.1};
        double dataStartTime{0.0};
        double dataEndTime{0.0};
    };

    LoudnessDataStore();
    ~LoudnessDataStore() = default;

    void prepare(double updateRateHz);
    void reset();
    
    void addPoint(float momentary, float shortTerm);
    
    double getCurrentTime() const;
    
    QueryResult getDataForDisplay(double startTime, double endTime, int targetPoints) const;

private:
    void updateLodLevels(float momentary, float shortTerm, double timestamp);
    
    static constexpr int kNumLods = 6;
    
    struct LodLevel
    {
        std::vector<MinMaxPoint> buckets;
        double bucketDuration{0.1};
        double currentBucketStart{-1.0};
        MinMaxPoint currentBucket;
        int samplesInCurrentBucket{0};
    };
    
    mutable std::mutex dataMutex;
    std::array<LodLevel, kNumLods> lodLevels;
    
    double updateRate{10.0};
    double sampleInterval{0.1};
    size_t totalSampleCount{0};
    std::atomic<double> currentTimestamp{0.0};
    
    int selectLodLevel(double timeRange, int targetPoints) const;
};