#pragma once

#include <juce_core/juce_core.h>
#include <vector>
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

    LoudnessDataStore();
    ~LoudnessDataStore() = default;

    void prepare(double updateRateHz);
    void reset();
    
    void addPoint(float momentary, float shortTerm);
    
    double getCurrentTime() const;
    double getUpdateRate() const { return updateRate; }
    
    std::vector<LoudnessPoint> getPointsInRange(double startTime, double endTime) const;

private:
    static constexpr size_t kMaxPoints = 180000;
    
    mutable std::mutex dataMutex;
    std::vector<LoudnessPoint> dataBuffer;
    
    double updateRate{10.0};
    std::atomic<double> currentTimestamp{0.0};
};