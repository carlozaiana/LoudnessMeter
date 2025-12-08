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

    LoudnessDataStore();
    ~LoudnessDataStore() = default;

    void prepare(double updateRateHz);
    void reset();
    
    // Called from audio thread - lock-free
    void addPoint(float momentary, float shortTerm);
    
    // Get the latest point for real-time display
    LoudnessPoint getLatestPoint() const;
    
    // Get current time
    double getCurrentTime() const;
    
    // Get points in time range for image rendering
    // Returns points that fall within [startTime, endTime)
    std::vector<LoudnessPoint> getPointsInRange(double startTime, double endTime) const;
    
    // Get all points since a given timestamp (for incremental updates)
    std::vector<LoudnessPoint> getPointsSince(double timestamp) const;

private:
    // Lock-free ring buffer for recent data
    static constexpr size_t kRingBufferSize = 4096; // ~6.8 minutes at 10Hz
    std::array<LoudnessPoint, kRingBufferSize> ringBuffer;
    std::atomic<size_t> writeIndex{0};
    std::atomic<size_t> totalPointCount{0};
    
    // Historical storage for older data
    mutable std::mutex historyMutex;
    std::vector<LoudnessPoint> historyBuffer;
    size_t lastHistorySync{0};
    
    double updateRate{10.0};
    std::atomic<double> currentTimestamp{0.0};
    
    void syncToHistory();
};