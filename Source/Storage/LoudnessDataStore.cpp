#include "LoudnessDataStore.h"
#include <algorithm>
#include <cmath>

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
        // Circular overwrite
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
    
    // Estimate start index
    size_t startIdx = 0;
    if (startTime > 0)
    {
        startIdx = static_cast<size_t>(std::max(0.0, startTime * updateRate));
        startIdx = std::min(startIdx, dataBuffer.size());
    }
    
    // Collect points
    result.reserve(static_cast<size_t>((endTime - startTime) * updateRate) + 10);
    
    for (size_t i = startIdx; i < dataBuffer.size(); ++i)
    {
        const auto& p = dataBuffer[i];
        if (p.timestamp >= endTime)
            break;
        if (p.timestamp >= startTime)
            result.push_back(p);
    }
    
    return result;
}

LoudnessDataStore::LoudnessPoint LoudnessDataStore::getPointAtTime(double time) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    if (dataBuffer.empty() || time < 0)
        return {-100.0f, -100.0f, 0.0};
    
    size_t idx = static_cast<size_t>(time * updateRate);
    if (idx >= dataBuffer.size())
        idx = dataBuffer.size() - 1;
    
    return dataBuffer[idx];
}