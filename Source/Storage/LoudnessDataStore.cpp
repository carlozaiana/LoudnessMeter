#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    double bucketDuration = kBaseBucketDuration;
    for (size_t i = 0; i < kLodLevels; ++i)
    {
        lodLevels[i].bucketDuration = bucketDuration;
        lodLevels[i].buckets.reserve(50000 / static_cast<size_t>(std::pow(kLodFactor, i)));
        bucketDuration *= kLodFactor;
    }
    
    for (auto& point : ringBuffer)
    {
        point = LoudnessPoint{-100.0f, -100.0f, 0.0};
    }
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
    lastProcessedCount = 0;
    lastSelectedLod = 0;
    lastTimeRange = 10.0;
    
    std::lock_guard<std::mutex> lock(lodMutex);
    for (auto& level : lodLevels)
    {
        level.buckets.clear();
        level.currentBucket = MinMaxPoint{100.0f, -100.0f, 100.0f, -100.0f, 0.0, 0.0};
        level.hasCurrentBucket = false;
    }
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

void LoudnessDataStore::addPointToLOD(const LoudnessPoint& point)
{
    for (size_t level = 0; level < kLodLevels; ++level)
    {
        auto& lod = lodLevels[level];
        double bucketDuration = lod.bucketDuration;
        
        // Calculate which bucket this point belongs to (fixed time-aligned boundaries)
        double bucketIndex = std::floor(point.timestamp / bucketDuration);
        double bucketStartTime = bucketIndex * bucketDuration;
        double bucketEndTime = bucketStartTime + bucketDuration;
        
        // Check if we need to start a new bucket
        if (!lod.hasCurrentBucket || point.timestamp >= lod.currentBucket.endTime)
        {
            // Finalize previous bucket if it exists
            if (lod.hasCurrentBucket)
            {
                lod.buckets.push_back(lod.currentBucket);
            }
            
            // Start new bucket
            lod.currentBucket = MinMaxPoint{
                point.momentary, point.momentary,
                point.shortTerm, point.shortTerm,
                bucketStartTime, bucketEndTime
            };
            lod.hasCurrentBucket = true;
        }
        else
        {
            // Update current bucket's min/max
            lod.currentBucket.minMomentary = std::min(lod.currentBucket.minMomentary, point.momentary);
            lod.currentBucket.maxMomentary = std::max(lod.currentBucket.maxMomentary, point.momentary);
            lod.currentBucket.minShortTerm = std::min(lod.currentBucket.minShortTerm, point.shortTerm);
            lod.currentBucket.maxShortTerm = std::max(lod.currentBucket.maxShortTerm, point.shortTerm);
        }
    }
}

void LoudnessDataStore::processRingBufferToLOD()
{
    size_t currentCount = totalPointCount.load(std::memory_order_acquire);
    
    if (currentCount <= lastProcessedCount)
        return;
    
    size_t currentWriteIdx = writeIndex.load(std::memory_order_acquire);
    size_t availableInBuffer = std::min(currentCount, kRingBufferSize);
    size_t oldestIndexInBuffer = (currentWriteIdx + kRingBufferSize - availableInBuffer) % kRingBufferSize;
    
    // Calculate how many new points we need to process
    size_t newPoints = currentCount - lastProcessedCount;
    
    // Don't process more than what's in the buffer
    newPoints = std::min(newPoints, availableInBuffer);
    
    // Calculate starting index
    size_t startIdx;
    if (newPoints >= availableInBuffer)
    {
        startIdx = oldestIndexInBuffer;
    }
    else
    {
        startIdx = (currentWriteIdx + kRingBufferSize - newPoints) % kRingBufferSize;
    }
    
    // Process each new point
    for (size_t i = 0; i < newPoints; ++i)
    {
        size_t idx = (startIdx + i) % kRingBufferSize;
        addPointToLOD(ringBuffer[idx]);
    }
    
    lastProcessedCount = currentCount;
}

int LoudnessDataStore::selectLodLevel(double timeRange, int maxPixels, int currentLod) const
{
    if (maxPixels <= 0) return 0;
    
    // Calculate ideal points per pixel (we want 2-4 for smooth rendering)
    double targetPointsPerPixel = 3.0;
    double idealBucketDuration = timeRange / (static_cast<double>(maxPixels) * targetPointsPerPixel);
    
    // Find the best LOD level
    int bestLevel = 0;
    for (int level = 0; level < static_cast<int>(kLodLevels); ++level)
    {
        if (lodLevels[static_cast<size_t>(level)].bucketDuration <= idealBucketDuration)
        {
            bestLevel = level;
        }
    }
    
    // Apply hysteresis to prevent flickering between levels
    if (bestLevel != currentLod)
    {
        double currentBucketDuration = lodLevels[static_cast<size_t>(currentLod)].bucketDuration;
        double bestBucketDuration = lodLevels[static_cast<size_t>(bestLevel)].bucketDuration;
        
        // Only switch if we've moved significantly past the threshold
        if (bestLevel > currentLod)
        {
            // Switching to coarser LOD - require more zoom out
            if (idealBucketDuration < currentBucketDuration * kLodFactor * kLodHysteresis)
            {
                return currentLod; // Stay at current level
            }
        }
        else
        {
            // Switching to finer LOD - require more zoom in
            if (idealBucketDuration > bestBucketDuration * kLodHysteresis)
            {
                return currentLod; // Stay at current level
            }
        }
    }
    
    return bestLevel;
}

LoudnessDataStore::RenderData LoudnessDataStore::getDataForTimeRange(
    double startTime, double endTime, int maxPixels)
{
    RenderData result;
    
    if (maxPixels <= 0 || endTime <= startTime)
        return result;
    
    double timeRange = endTime - startTime;
    
    // Process any new data from ring buffer
    {
        std::lock_guard<std::mutex> lock(lodMutex);
        processRingBufferToLOD();
        
        // Select LOD level with hysteresis
        result.lodLevel = selectLodLevel(timeRange, maxPixels, lastSelectedLod);
        lastSelectedLod = result.lodLevel;
        lastTimeRange = timeRange;
        
        const auto& lod = lodLevels[static_cast<size_t>(result.lodLevel)];
        result.bucketDuration = lod.bucketDuration;
        
        if (lod.buckets.empty() && !lod.hasCurrentBucket)
            return result;
        
        // Find buckets that overlap with the requested time range
        // Using fixed time-aligned boundaries, we can calculate indices directly
        double bucketDuration = lod.bucketDuration;
        int64_t startBucketIdx = static_cast<int64_t>(std::floor(startTime / bucketDuration));
        int64_t endBucketIdx = static_cast<int64_t>(std::ceil(endTime / bucketDuration));
        
        startBucketIdx = std::max(int64_t(0), startBucketIdx);
        
        // Reserve space
        size_t estimatedPoints = static_cast<size_t>(endBucketIdx - startBucketIdx + 1);
        result.points.reserve(std::min(estimatedPoints, lod.buckets.size() + 1));
        
        // Binary search for start position
        auto it = std::lower_bound(lod.buckets.begin(), lod.buckets.end(), startTime,
            [](const MinMaxPoint& bucket, double time) {
                return bucket.endTime <= time;
            });
        
        // Collect buckets in range
        while (it != lod.buckets.end() && it->startTime < endTime)
        {
            result.points.push_back(*it);
            ++it;
        }
        
        // Include current bucket if it overlaps
        if (lod.hasCurrentBucket && 
            lod.currentBucket.startTime < endTime && 
            lod.currentBucket.endTime > startTime)
        {
            result.points.push_back(lod.currentBucket);
        }
    }
    
    return result;
}

double LoudnessDataStore::getCurrentTime() const
{
    return currentTimestamp.load(std::memory_order_acquire);
}