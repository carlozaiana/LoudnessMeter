#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    // Initialize LOD levels with exponentially increasing bucket durations
    double duration = 0.1; // 100ms base
    for (int i = 0; i < kNumLods; ++i)
    {
        lodLevels[static_cast<size_t>(i)].bucketDuration = duration;
        lodLevels[static_cast<size_t>(i)].buckets.reserve(10000);
        lodLevels[static_cast<size_t>(i)].currentBucket.reset();
        lodLevels[static_cast<size_t>(i)].currentBucketStart = -1.0;
        lodLevels[static_cast<size_t>(i)].samplesInCurrentBucket = 0;
        duration *= 4.0; // Each level is 4x coarser
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
    
    double duration = sampleInterval;
    for (int i = 0; i < kNumLods; ++i)
    {
        auto& lod = lodLevels[static_cast<size_t>(i)];
        lod.buckets.clear();
        lod.bucketDuration = duration;
        lod.currentBucket.reset();
        lod.currentBucketStart = -1.0;
        lod.samplesInCurrentBucket = 0;
        duration *= 4.0;
    }
    
    currentTimestamp.store(0.0, std::memory_order_release);
}

void LoudnessDataStore::addPoint(float momentary, float shortTerm)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    size_t totalSamples = 0;
    if (!lodLevels[0].buckets.empty() || lodLevels[0].samplesInCurrentBucket > 0)
    {
        totalSamples = lodLevels[0].buckets.size() * 
            static_cast<size_t>(lodLevels[0].bucketDuration / sampleInterval) +
            static_cast<size_t>(lodLevels[0].samplesInCurrentBucket);
    }
    
    double timestamp = static_cast<double>(totalSamples) * sampleInterval;
    
    updateLodLevels(momentary, shortTerm, timestamp);
    
    currentTimestamp.store(timestamp, std::memory_order_release);
}

void LoudnessDataStore::updateLodLevels(float momentary, float shortTerm, double timestamp)
{
    for (int i = 0; i < kNumLods; ++i)
    {
        auto& lod = lodLevels[static_cast<size_t>(i)];
        
        // Calculate which bucket this timestamp belongs to (time-aligned)
        double bucketIndex = std::floor(timestamp / lod.bucketDuration);
        double bucketStart = bucketIndex * lod.bucketDuration;
        
        // Check if we need to start a new bucket
        if (bucketStart > lod.currentBucketStart)
        {
            // Finalize previous bucket if it has data
            if (lod.samplesInCurrentBucket > 0)
            {
                lod.buckets.push_back(lod.currentBucket);
            }
            
            // Start new bucket
            lod.currentBucket.reset();
            lod.currentBucketStart = bucketStart;
            lod.samplesInCurrentBucket = 0;
        }
        
        // Add sample to current bucket
        double bucketMid = bucketStart + lod.bucketDuration * 0.5;
        lod.currentBucket.addSample(momentary, shortTerm, bucketMid);
        lod.samplesInCurrentBucket++;
    }
}

double LoudnessDataStore::getCurrentTime() const
{
    return currentTimestamp.load(std::memory_order_acquire);
}

int LoudnessDataStore::selectLodLevel(double timeRange, int targetPoints) const
{
    if (targetPoints <= 0)
        return 0;
    
    // We want approximately targetPoints points for this time range
    // So the ideal bucket duration is timeRange / targetPoints
    double idealBucketDuration = timeRange / static_cast<double>(targetPoints);
    
    // Find the LOD level with bucket duration >= idealBucketDuration
    // This ensures we get <= targetPoints (coarse enough)
    // Start from finest (LOD 0) and go coarser until we find a match
    
    for (int i = 0; i < kNumLods; ++i)
    {
        if (lodLevels[static_cast<size_t>(i)].bucketDuration >= idealBucketDuration)
        {
            return i;
        }
    }
    
    // If even the coarsest LOD is too fine, use the coarsest
    return kNumLods - 1;
}

LoudnessDataStore::QueryResult LoudnessDataStore::getDataForDisplay(
    double startTime, double endTime, int targetPoints) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    QueryResult result;
    
    if (endTime <= startTime || targetPoints <= 0)
        return result;
    
    double timeRange = endTime - startTime;
    result.lodLevel = selectLodLevel(timeRange, targetPoints);
    
    const auto& lod = lodLevels[static_cast<size_t>(result.lodLevel)];
    result.bucketDuration = lod.bucketDuration;
    
    if (lod.buckets.empty() && lod.samplesInCurrentBucket == 0)
        return result;
    
    // Binary search for start index
    double searchStart = startTime - lod.bucketDuration;
    
    size_t startIdx = 0;
    if (!lod.buckets.empty())
    {
        auto it = std::lower_bound(lod.buckets.begin(), lod.buckets.end(), searchStart,
            [](const MinMaxPoint& bucket, double time) {
                return bucket.timeMid < time;
            });
        startIdx = static_cast<size_t>(std::distance(lod.buckets.begin(), it));
    }
    
    // Binary search for end index
    double searchEnd = endTime + lod.bucketDuration;
    
    size_t endIdx = lod.buckets.size();
    if (!lod.buckets.empty())
    {
        auto it = std::upper_bound(lod.buckets.begin(), lod.buckets.end(), searchEnd,
            [](double time, const MinMaxPoint& bucket) {
                return time < bucket.timeMid;
            });
        endIdx = static_cast<size_t>(std::distance(lod.buckets.begin(), it));
    }
    
    // Calculate expected number of points
    size_t numPoints = (endIdx > startIdx) ? (endIdx - startIdx) : 0;
    
    // Reserve and copy
    result.points.reserve(numPoints + 1);
    
    for (size_t i = startIdx; i < endIdx; ++i)
    {
        const auto& bucket = lod.buckets[i];
        if (bucket.timeMid >= startTime - lod.bucketDuration && 
            bucket.timeMid <= endTime + lod.bucketDuration)
        {
            result.points.push_back(bucket);
        }
    }
    
    // Add current bucket if in range
    if (lod.samplesInCurrentBucket > 0)
    {
        double currentMid = lod.currentBucketStart + lod.bucketDuration * 0.5;
        if (currentMid >= startTime - lod.bucketDuration && 
            currentMid <= endTime + lod.bucketDuration)
        {
            result.points.push_back(lod.currentBucket);
        }
    }
    
    return result;
}