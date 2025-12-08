#include "LoudnessDataStore.h"
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    dataBuffer.reserve(kMaxPoints);
}

void LoudnessDataStore::prepare(double updateRateHz)
{
    updateRate = updateRateHz;
}

void LoudnessDataStore::reset()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    dataBuffer.clear();
    currentTimestamp.store(0.0, std::memory_order_release);
}

void LoudnessDataStore::addPoint(float momentary, float shortTerm)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    double timestamp = static_cast<double>(dataBuffer.size()) / updateRate;
    
    if (dataBuffer.size() < kMaxPoints)
    {
        dataBuffer.push_back({momentary, shortTerm, timestamp});
    }
    else
    {
        // Circular overwrite for very long sessions
        size_t idx = dataBuffer.size() % kMaxPoints;
        dataBuffer[idx] = {momentary, shortTerm, timestamp};
    }
    
    currentTimestamp.store(timestamp, std::memory_order_release);
}

double LoudnessDataStore::getCurrentTime() const
{
    return currentTimestamp.load(std::memory_order_acquire);
}

std::vector<LoudnessDataStore::LoudnessPoint> LoudnessDataStore::getPointsInRange(
    double startTime, double endTime) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    std::vector<LoudnessPoint> result;
    
    if (dataBuffer.empty() || endTime <= startTime)
        return result;
    
    // Binary search for start
    auto startIt = std::lower_bound(dataBuffer.begin(), dataBuffer.end(), startTime,
        [](const LoudnessPoint& p, double t) { return p.timestamp < t; });
    
    // Collect points up to endTime
    for (auto it = startIt; it != dataBuffer.end() && it->timestamp < endTime; ++it)
    {
        result.push_back(*it);
    }
    
    return result;
}