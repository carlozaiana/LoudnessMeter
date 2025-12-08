#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    for (auto& point : ringBuffer)
    {
        point = LoudnessPoint{-100.0f, -100.0f, 0.0};
    }
    historyBuffer.reserve(200000); // Reserve for ~5.5 hours at 10Hz
}

void LoudnessDataStore::prepare(double updateRateHz)
{
    updateRate = updateRateHz;
}

void LoudnessDataStore::reset()
{
    writeIndex.store(0, std::memory_order_relaxed);
    totalPointCount.store(0, std::memory_order_relaxed);
    currentTimestamp.store(0.0, std::memory_order_relaxed);
    lastHistorySync = 0;
    
    std::lock_guard<std::mutex> lock(historyMutex);
    historyBuffer.clear();
}

void LoudnessDataStore::addPoint(float momentary, float shortTerm)
{
    size_t count = totalPointCount.load(std::memory_order_relaxed);
    double timestamp = static_cast<double>(count) / updateRate;
    
    size_t idx = writeIndex.load(std::memory_order_relaxed);
    ringBuffer[idx] = LoudnessPoint{momentary, shortTerm, timestamp};
    
    size_t nextIdx = (idx + 1) % kRingBufferSize;
    writeIndex.store(nextIdx, std::memory_order_release);
    totalPointCount.fetch_add(1, std::memory_order_relaxed);
    currentTimestamp.store(timestamp, std::memory_order_release);
}

LoudnessDataStore::LoudnessPoint LoudnessDataStore::getLatestPoint() const
{
    size_t count = totalPointCount.load(std::memory_order_acquire);
    if (count == 0)
        return LoudnessPoint{-100.0f, -100.0f, 0.0};
    
    size_t idx = writeIndex.load(std::memory_order_acquire);
    size_t latestIdx = (idx + kRingBufferSize - 1) % kRingBufferSize;
    return ringBuffer[latestIdx];
}

double LoudnessDataStore::getCurrentTime() const
{
    return currentTimestamp.load(std::memory_order_acquire);
}

void LoudnessDataStore::syncToHistory()
{
    size_t currentCount = totalPointCount.load(std::memory_order_acquire);
    
    if (currentCount <= lastHistorySync)
        return;
    
    size_t currentWriteIdx = writeIndex.load(std::memory_order_acquire);
    size_t availableInBuffer = std::min(currentCount, kRingBufferSize);
    
    // Calculate how many new points to sync
    size_t newPoints = currentCount - lastHistorySync;
    newPoints = std::min(newPoints, availableInBuffer);
    
    // Calculate starting index in ring buffer
    size_t startIdx = (currentWriteIdx + kRingBufferSize - newPoints) % kRingBufferSize;
    
    // Append to history
    for (size_t i = 0; i < newPoints; ++i)
    {
        size_t idx = (startIdx + i) % kRingBufferSize;
        historyBuffer.push_back(ringBuffer[idx]);
    }
    
    lastHistorySync = currentCount;
}

std::vector<LoudnessDataStore::LoudnessPoint> LoudnessDataStore::getPointsInRange(
    double startTime, double endTime) const
{
    std::vector<LoudnessPoint> result;
    
    size_t count = totalPointCount.load(std::memory_order_acquire);
    if (count == 0 || endTime <= startTime)
        return result;
    
    // First check ring buffer for recent data
    size_t currentWriteIdx = writeIndex.load(std::memory_order_acquire);
    size_t availableInBuffer = std::min(count, kRingBufferSize);
    size_t oldestIdx = (currentWriteIdx + kRingBufferSize - availableInBuffer) % kRingBufferSize;
    
    double ringBufferStartTime = ringBuffer[oldestIdx].timestamp;
    
    // If we need data older than ring buffer, check history
    if (startTime < ringBufferStartTime)
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        const_cast<LoudnessDataStore*>(this)->syncToHistory();
        
        // Binary search for start position in history
        auto it = std::lower_bound(historyBuffer.begin(), historyBuffer.end(), startTime,
            [](const LoudnessPoint& p, double t) { return p.timestamp < t; });
        
        while (it != historyBuffer.end() && it->timestamp < endTime && it->timestamp < ringBufferStartTime)
        {
            result.push_back(*it);
            ++it;
        }
    }
    
    // Collect from ring buffer
    for (size_t i = 0; i < availableInBuffer; ++i)
    {
        size_t idx = (oldestIdx + i) % kRingBufferSize;
        const auto& point = ringBuffer[idx];
        
        if (point.timestamp >= endTime)
            break;
        
        if (point.timestamp >= startTime)
        {
            result.push_back(point);
        }
    }
    
    return result;
}

std::vector<LoudnessDataStore::LoudnessPoint> LoudnessDataStore::getPointsSince(double timestamp) const
{
    return getPointsInRange(timestamp, getCurrentTime() + 1.0);
}