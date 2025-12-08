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
        double timeStart{0.0};
        double timeEnd{0.0};
        
        bool isValid() const { return momentaryMax > -100.0f || shortTermMax > -100.0f; }
        
        void addSample(float m, float s)
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
        }
        
        void merge(const MinMaxPoint& other)
        {
            if (other.momentaryMax > -100.0f)
            {
                momentaryMin = std::min(momentaryMin, other.momentaryMin);
                momentaryMax = std::max(momentaryMax, other.momentaryMax);
            }
            if (other.shortTermMax > -100.0f)
            {
                shortTermMin = std::min(shortTermMin, other.shortTermMin);
                shortTermMax = std::max(shortTermMax, other.shortTermMax);
            }
            timeEnd = std::max(timeEnd, other.timeEnd);
        }
        
        void reset(double start, double end)
        {
            momentaryMin = 100.0f;
            momentaryMax = -100.0f;
            shortTermMin = 100.0f;
            shortTermMax = -100.0f;
            timeStart = start;
            timeEnd = end;
        }
    };

    LoudnessDataStore();
    ~LoudnessDataStore() = default;

    void prepare(double updateRateHz);
    void reset();
    
    void addPoint(float momentary, float shortTerm);
    
    double getCurrentTime() const;
    double getUpdateRate() const { return updateRate; }
    
    // Get min/max data for a time range, sampled to approximately numBuckets
    std::vector<MinMaxPoint> getMinMaxForRange(double startTime, double endTime, int numBuckets) const;

private:
    void updateLodData(const LoudnessPoint& point);
    
    static constexpr size_t kMaxRawPoints = 36000; // 1 hour at 10 Hz
    
    mutable std::mutex dataMutex;
    std::vector<LoudnessPoint> rawData;
    
    // LOD levels for efficient querying
    // LOD 0: 100ms (raw data rate)
    // LOD 1: 400ms (4:1)
    // LOD 2: 1.6s (16:1)
    // LOD 3: 6.4s (64:1)
    // LOD 4: 25.6s (256:1)
    static constexpr int kNumLods = 5;
    static constexpr int kLodFactor = 4;
    
    struct LodLevel
    {
        std::vector<MinMaxPoint> data;
        double bucketDuration{0.1};
        MinMaxPoint currentBucket;
        int samplesInBucket{0};
    };
    
    std::array<LodLevel, kNumLods> lodLevels;
    
    double updateRate{10.0};
    double sampleInterval{0.1};
    std::atomic<double> currentTimestamp{0.0};
    
    int selectLodLevel(double timeRange, int numBuckets) const;
};