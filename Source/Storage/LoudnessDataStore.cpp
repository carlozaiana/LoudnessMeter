#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    double duration = 0.1;
    for (int i = 0; i < kNumLods; ++i)
    {
        lodLevels[static_cast<size_t>(i)].bucketDuration = duration;
        lodLevels[static_cast<size_t>(i)].buckets.reserve(10000);
        lodLevels[static_cast<size_t>(i)].currentBucket.reset();
        lodLevels[static_cast<size_t>(i)].currentBucketStart = -1.0;
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
    
    totalSampleCount = 0;
    
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
    
    double timestamp = static_cast<double>(totalSampleCount) * sampleInterval;
    totalSampleCount++;
    
    updateLodLevels(momentary, shortTerm, timestamp);
    
    currentTimestamp.store(timestamp, std::memory_order_release);
}

void LoudnessDataStore::updateLodLevels(float momentary, float shortTerm, double timestamp)
{
    for (int i = 0; i < kNumLods; ++i)
    {
        auto& lod = lodLevels[static_cast<size_t>(i)];
        
        double bucketIndex = std::floor(timestamp / lod.bucketDuration);
        double bucketStart = bucketIndex * lod.bucketDuration;
        
        if (bucketStart > lod.currentBucketStart)
        {
            if (lod.samplesInCurrentBucket > 0)
            {
                lod.buckets.push_back(lod.currentBucket);
            }
            
            lod.currentBucket.reset();
            lod.currentBucketStart = bucketStart;
            lod.samplesInCurrentBucket = 0;
        }
        
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
    
    double idealBucketDuration = timeRange / static_cast<double>(targetPoints);
    
    for (int i = 0; i < kNumLods; ++i)
    {
        if (lodLevels[static_cast<size_t>(i)].bucketDuration >= idealBucketDuration)
        {
            return i;
        }
    }
    
    return kNumLods - 1;
}

LoudnessDataStore::QueryResult LoudnessDataStore::getDataForDisplay(
    double startTime, double endTime, int targetPoints) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    QueryResult result;
    result.dataStartTime = startTime;
    result.dataEndTime = endTime;
    
    if (endTime <= startTime || targetPoints <= 0)
        return result;
    
    double timeRange = endTime - startTime;
    result.lodLevel = selectLodLevel(timeRange, targetPoints);
    
    const auto& lod = lodLevels[static_cast<size_t>(result.lodLevel)];
    result.bucketDuration = lod.bucketDuration;
    
    if (lod.buckets.empty() && lod.samplesInCurrentBucket == 0)
        return result;
    
    double searchStart = startTime - lod.bucketDuration;
    double searchEnd = endTime + lod.bucketDuration;
    
    size_t startIdx = 0;
    size_t endIdx = lod.buckets.size();
    
    if (!lod.buckets.empty())
    {
        auto itStart = std::lower_bound(lod.buckets.begin(), lod.buckets.end(), searchStart,
            [](const MinMaxPoint& bucket, double time) {
                return bucket.timeMid < time;
            });
        startIdx = static_cast<size_t>(std::distance(lod.buckets.begin(), itStart));
        
        auto itEnd = std::upper_bound(lod.buckets.begin(), lod.buckets.end(), searchEnd,
            [](double time, const MinMaxPoint& bucket) {
                return time < bucket.timeMid;
            });
        endIdx = static_cast<size_t>(std::distance(lod.buckets.begin(), itEnd));
    }
    
    size_t numPoints = (endIdx > startIdx) ? (endIdx - startIdx) : 0;
    result.points.reserve(numPoints + 1);
    
    for (size_t i = startIdx; i < endIdx; ++i)
    {
        result.points.push_back(lod.buckets[i]);
    }
    
    if (lod.samplesInCurrentBucket > 0)
    {
        double currentMid = lod.currentBucketStart + lod.bucketDuration * 0.5;
        if (currentMid >= searchStart && currentMid <= searchEnd)
        {
            MinMaxPoint currentCopy = lod.currentBucket;
            currentCopy.timeMid = currentMid;
            result.points.push_back(currentCopy);
        }
    }
    
    if (!result.points.empty())
    {
        result.dataStartTime = result.points.front().timeMid - lod.bucketDuration * 0.5;
        result.dataEndTime = result.points.back().timeMid + lod.bucketDuration * 0.5;
    }
    
    return result;
}