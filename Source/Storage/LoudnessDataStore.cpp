#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    rawData.reserve(kMaxRawPoints);
    
    // Initialize LOD levels with increasing bucket durations
    // LOD 0: 100ms, LOD 1: 500ms, LOD 2: 2s, LOD 3: 8s, LOD 4: 32s, LOD 5: 128s
    double duration = 0.1;
    for (int i = 0; i < kNumLods; ++i)
    {
        lodLevels[static_cast<size_t>(i)].bucketDuration = duration;
        lodLevels[static_cast<size_t>(i)].buckets.reserve(20000);
        lodLevels[static_cast<size_t>(i)].currentBucket.reset();
        lodLevels[static_cast<size_t>(i)].currentBucketStart = 0.0;
        lodLevels[static_cast<size_t>(i)].samplesInCurrentBucket = 0;
        duration *= 4.0;
    }
}

void LoudnessDataStore::prepare(double updateRateHz)
{
    updateRate = updateRateHz;
    sampleInterval = 1.0 / updateRate;
}

void LoudnessDataStore::reset()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    rawData.clear();
    
    double duration = sampleInterval;
    for (int i = 0; i < kNumLods; ++i)
    {
        auto& lod = lodLevels[static_cast<size_t>(i)];
        lod.buckets.clear();
        lod.bucketDuration = duration;
        lod.currentBucket.reset();
        lod.currentBucketStart = 0.0;
        lod.samplesInCurrentBucket = 0;
        duration *= 4.0;
    }
    
    currentTimestamp.store(0.0, std::memory_order_release);
}

void LoudnessDataStore::addPoint(float momentary, float shortTerm)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    double timestamp = static_cast<double>(rawData.size()) * sampleInterval;
    
    if (rawData.size() < kMaxRawPoints)
    {
        rawData.push_back({momentary, shortTerm, timestamp});
    }
    
    updateLodLevels(momentary, shortTerm, timestamp);
    
    currentTimestamp.store(timestamp, std::memory_order_release);
}

void LoudnessDataStore::updateLodLevels(float momentary, float shortTerm, double timestamp)
{
    for (int i = 0; i < kNumLods; ++i)
    {
        auto& lod = lodLevels[static_cast<size_t>(i)];
        
        // Calculate which bucket this timestamp belongs to
        double bucketIndex = std::floor(timestamp / lod.bucketDuration);
        double bucketStart = bucketIndex * lod.bucketDuration;
        
        // If this is a new bucket, finalize the previous one
        if (bucketStart > lod.currentBucketStart && lod.samplesInCurrentBucket > 0)
        {
            lod.currentBucket.timeMid = lod.currentBucketStart + lod.bucketDuration * 0.5;
            lod.buckets.push_back(lod.currentBucket);
            lod.currentBucket.reset();
            lod.samplesInCurrentBucket = 0;
        }
        
        lod.currentBucketStart = bucketStart;
        lod.currentBucket.addSample(momentary, shortTerm, bucketStart + lod.bucketDuration * 0.5);
        lod.samplesInCurrentBucket++;
    }
}

double LoudnessDataStore::getCurrentTime() const
{
    return currentTimestamp.load(std::memory_order_acquire);
}

int LoudnessDataStore::selectLodLevel(double timeRange, int maxPoints) const
{
    if (maxPoints <= 0)
        return 0;
    
    double idealBucketDuration = timeRange / static_cast<double>(maxPoints);
    
    // Find the highest LOD level (coarsest) that still gives enough detail
    for (int i = kNumLods - 1; i >= 0; --i)
    {
        if (lodLevels[static_cast<size_t>(i)].bucketDuration <= idealBucketDuration)
            return i;
    }
    
    return 0;
}

LoudnessDataStore::QueryResult LoudnessDataStore::getDataForTimeRange(
    double startTime, double endTime, int maxPoints) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    QueryResult result;
    result.queryStartTime = startTime;
    result.queryEndTime = endTime;
    
    if (endTime <= startTime || maxPoints <= 0)
        return result;
    
    double timeRange = endTime - startTime;
    result.lodLevel = selectLodLevel(timeRange, maxPoints);
    
    const auto& lod = lodLevels[static_cast<size_t>(result.lodLevel)];
    result.bucketDuration = lod.bucketDuration;
    
    if (lod.buckets.empty() && lod.samplesInCurrentBucket == 0)
        return result;
    
    // Binary search to find start index
    size_t startIdx = 0;
    size_t endIdx = lod.buckets.size();
    
    if (!lod.buckets.empty())
    {
        // Find first bucket with timeMid >= startTime - bucketDuration
        double searchStart = startTime - lod.bucketDuration;
        
        auto it = std::lower_bound(lod.buckets.begin(), lod.buckets.end(), searchStart,
            [](const MinMaxPoint& bucket, double time) {
                return bucket.timeMid < time;
            });
        
        startIdx = static_cast<size_t>(std::distance(lod.buckets.begin(), it));
        
        // Find last bucket with timeMid <= endTime + bucketDuration
        double searchEnd = endTime + lod.bucketDuration;
        
        auto itEnd = std::upper_bound(lod.buckets.begin(), lod.buckets.end(), searchEnd,
            [](double time, const MinMaxPoint& bucket) {
                return time < bucket.timeMid;
            });
        
        endIdx = static_cast<size_t>(std::distance(lod.buckets.begin(), itEnd));
    }
    
    // Reserve space
    result.points.reserve(endIdx - startIdx + 2);
    
    // Copy relevant buckets
    for (size_t i = startIdx; i < endIdx; ++i)
    {
        result.points.push_back(lod.buckets[i]);
    }
    
    // Add current bucket if it's in range
    if (lod.samplesInCurrentBucket > 0)
    {
        double currentMid = lod.currentBucketStart + lod.bucketDuration * 0.5;
        if (currentMid >= startTime - lod.bucketDuration && currentMid <= endTime + lod.bucketDuration)
        {
            MinMaxPoint current = lod.currentBucket;
            current.timeMid = currentMid;
            result.points.push_back(current);
        }
    }
    
    return result;
}