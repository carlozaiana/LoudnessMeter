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
    
    // Called from audio thread via timer
    void addPoint(float momentary, float shortTerm);
    
    // Get current data time
    double getCurrentTime() const;
    
    // Get points in time range (inclusive of startTime, exclusive of endTime)
    std::vector<LoudnessPoint> getPointsInRange(double startTime, double endTime) const;
    
    // Get update rate
    double getUpdateRate() const { return updateRate; }

private:
    static constexpr size_t kMaxPoints = 180000; // 5 hours at 10 Hz
    
    mutable std::mutex dataMutex;
    std::vector<LoudnessPoint> dataBuffer;
    
    double updateRate{10.0};
    std::atomic<double> currentTimestamp{0.0};
};