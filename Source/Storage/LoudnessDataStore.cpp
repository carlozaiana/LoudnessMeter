#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    size_t factor = 1;
    for (size_t i = 0; i < kLodLevels; ++i)
    {
        lodLevels[i].reductionFactor = factor;
        lodLevels[i].points.reserve(10000);
        factor *= kLodFactor;
    }
    
    // Initialize ring buffer
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
    pointCount.store(0, std::memory_order_relaxed);
    currentTimestamp.store(0.0, std::memory_order_relaxed);
    lastProcessedIndex = 0;
    
    std::lock_guard<std::mutex> lock(lodMutex);
    for (auto& level : lodLevels)
    {
        level.points.clear();
        level.accumulator = MinMaxPoint{100.0f, -100.0f, 100.0f, -100.0f, 0.0};
        level.accumulatorCount = 0;
    }
}

void LoudnessDataStore::addPoint(float momentary, float shortTerm)
{
    // Lock-free write to ring buffer
    size_t count = pointCount.load(std::memory_order_relaxed);
    double timestamp = static_cast<double>(count) / updateRate;
    
    size_t idx = writeIndex.load(std::memory_order_relaxed);
    
    ringBuffer[idx] = LoudnessPoint{momentary, shortTerm, timestamp};
    
    size_t nextIdx = (idx + 1) % kRingBufferSize;
    writeIndex.store(nextIdx, std::memory_order_release);
    pointCount.fetch_add(1, std::memory_order_relaxed);
    currentTimestamp.store(timestamp, std::memory_order_release);
}

void LoudnessDataStore::processRingBufferToLOD()
{
    size_t currentWrite = writeIndex.load(std::memory_order_acquire);
    size_t count = pointCount.load(std::memory_order_relaxed);
    
    if (count == 0) return;
    
    // Process new points from ring buffer into LOD
    while (lastProcessedIndex != currentWrite)
    {
        const auto& point = ringBuffer[lastProcessedIndex];
        
        // Add to LOD level 0 (full resolution min/max, but we aggregate every 4 points)
        for (size_t level = 0; level < kLodLevels; ++level)
        {
            auto& lod = lodLevels[level];
            
            lod.accumulator.minMomentary = std::min(lod.accumulator.minMomentary, point.momentary);
            lod.accumulator.maxMomentary = std::max(lod.accumulator.maxMomentary, point.momentary);
            lod.accumulator.minShortTerm = std::min(lod.accumulator.minShortTerm, point.shortTerm);
            lod.accumulator.maxShortTerm = std::max(lod.accumulator.maxShortTerm, point.shortTerm);
            lod.accumulator.timestamp = point.timestamp;
            lod.accumulatorCount++;
            
            if (lod.accumulatorCount >= lod.reductionFactor)
            {
                lod.points.push_back(lod.accumulator);
                lod.accumulator = MinMaxPoint{100.0f, -100.0f, 100.0f, -100.0f, 0.0};
                lod.accumulatorCount = 0;
            }
        }
        
        lastProcessedIndex = (lastProcessedIndex + 1) % kRingBufferSize;
    }
}

int LoudnessDataStore::selectLodLevel(double timeRange, int maxPixels) const
{
    if (maxPixels <= 0) return 0;
    
    double pointsNeeded = timeRange * updateRate;
    double pointsPerPixel = pointsNeeded / static_cast<double>(maxPixels);
    
    // We want roughly 2-4 points per pixel for smooth rendering
    for (int level = static_cast<int>(kLodLevels) - 1; level >= 0; --level)
    {
        double effectivePPP = pointsPerPixel / static_cast<double>(lodLevels[static_cast<size_t>(level)].reductionFactor);
        if (effectivePPP >= 1.5)
            return level;
    }
    
    return 0;
}

void LoudnessDataStore::getRecentPoints(double startTime, double endTime, int maxPixels, RenderData& result)
{
    size_t count = pointCount.load(std::memory_order_acquire);
    if (count == 0) return;
    
    size_t currentWrite = writeIndex.load(std::memory_order_acquire);
    size_t available = std::min(count, kRingBufferSize);
    
    // Calculate start index in ring buffer
    size_t startIdx = (currentWrite + kRingBufferSize - available) % kRingBufferSize;
    
    // Collect points in time range
    std::vector<LoudnessPoint> tempPoints;
    tempPoints.reserve(available);
    
    for (size_t i = 0; i < available; ++i)
    {
        size_t idx = (startIdx + i) % kRingBufferSize;
        const auto& point = ringBuffer[idx];
        
        if (point.timestamp >= startTime && point.timestamp <= endTime)
        {
            tempPoints.push_back(point);
        }
    }
    
    if (tempPoints.empty()) return;
    
    // Downsample with min/max preservation if needed
    size_t numPoints = tempPoints.size();
    size_t targetPoints = static_cast<size_t>(maxPixels * 2); // 2 points per pixel for min/max
    
    if (numPoints <= targetPoints)
    {
        result.points = std::move(tempPoints);
        result.useMinMax = false;
    }
    else
    {
        // Bucket the points and compute min/max per bucket
        size_t bucketSize = (numPoints + targetPoints - 1) / targetPoints;
        result.minMaxPoints.reserve(targetPoints);
        
        for (size_t i = 0; i < numPoints; i += bucketSize)
        {
            MinMaxPoint mm{100.0f, -100.0f, 100.0f, -100.0f, 0.0};
            size_t end = std::min(i + bucketSize, numPoints);
            
            for (size_t j = i; j < end; ++j)
            {
                mm.minMomentary = std::min(mm.minMomentary, tempPoints[j].momentary);
                mm.maxMomentary = std::max(mm.maxMomentary, tempPoints[j].momentary);
                mm.minShortTerm = std::min(mm.minShortTerm, tempPoints[j].shortTerm);
                mm.maxShortTerm = std::max(mm.maxShortTerm, tempPoints[j].shortTerm);
            }
            mm.timestamp = tempPoints[(i + end) / 2].timestamp;
            result.minMaxPoints.push_back(mm);
        }
        result.useMinMax = true;
    }
}

void LoudnessDataStore::getLODPoints(double startTime, double endTime, int maxPixels, RenderData& result)
{
    double timeRange = endTime - startTime;
    result.lodLevel = selectLodLevel(timeRange, maxPixels);
    result.zoomFactor = timeRange;
    
    const auto& lod = lodLevels[static_cast<size_t>(result.lodLevel)];
    
    if (lod.points.empty()) return;
    
    double reductionFactor = static_cast<double>(lod.reductionFactor);
    double effectiveRate = updateRate / reductionFactor;
    
    // Find index range
    int64_t startIdx = static_cast<int64_t>(startTime * effectiveRate);
    int64_t endIdx = static_cast<int64_t>(std::ceil(endTime * effectiveRate));
    
    startIdx = std::max(int64_t(0), startIdx);
    endIdx = std::min(static_cast<int64_t>(lod.points.size()), endIdx);
    
    if (startIdx >= endIdx) return;
    
    size_t numPoints = static_cast<size_t>(endIdx - startIdx);
    size_t targetPoints = static_cast<size_t>(maxPixels);
    
    if (numPoints <= targetPoints * 2)
    {
        // Few enough points - copy directly
        result.minMaxPoints.reserve(numPoints);
        for (int64_t i = startIdx; i < endIdx; ++i)
        {
            result.minMaxPoints.push_back(lod.points[static_cast<size_t>(i)]);
        }
    }
    else
    {
        // Further downsample with min/max preservation
        size_t bucketSize = (numPoints + targetPoints - 1) / targetPoints;
        result.minMaxPoints.reserve(targetPoints);
        
        for (size_t i = 0; i < numPoints; i += bucketSize)
        {
            MinMaxPoint mm{100.0f, -100.0f, 100.0f, -100.0f, 0.0};
            size_t end = std::min(i + bucketSize, numPoints);
            
            for (size_t j = i; j < end; ++j)
            {
                size_t idx = static_cast<size_t>(startIdx) + j;
                const auto& p = lod.points[idx];
                mm.minMomentary = std::min(mm.minMomentary, p.minMomentary);
                mm.maxMomentary = std::max(mm.maxMomentary, p.maxMomentary);
                mm.minShortTerm = std::min(mm.minShortTerm, p.minShortTerm);
                mm.maxShortTerm = std::max(mm.maxShortTerm, p.maxShortTerm);
            }
            size_t midIdx = static_cast<size_t>(startIdx) + (i + end) / 2;
            mm.timestamp = lod.points[std::min(midIdx, lod.points.size() - 1)].timestamp;
            result.minMaxPoints.push_back(mm);
        }
    }
    result.useMinMax = true;
}

LoudnessDataStore::RenderData LoudnessDataStore::getDataForTimeRange(
    double startTime, double endTime, int maxPixels)
{
    RenderData result;
    result.zoomFactor = endTime - startTime;
    
    if (maxPixels <= 0 || endTime <= startTime)
        return result;
    
    double currentTime = currentTimestamp.load(std::memory_order_acquire);
    double ringBufferDuration = static_cast<double>(kRingBufferSize) / updateRate;
    double ringBufferStartTime = currentTime - ringBufferDuration;
    
    // If requesting recent data that's still in ring buffer
    if (startTime >= ringBufferStartTime)
    {
        getRecentPoints(startTime, endTime, maxPixels, result);
        return result;
    }
    
    // Otherwise use LOD data
    {
        std::lock_guard<std::mutex> lock(lodMutex);
        processRingBufferToLOD();
        getLODPoints(startTime, endTime, maxPixels, result);
    }
    
    return result;
}

double LoudnessDataStore::getCurrentTime() const
{
    return currentTimestamp.load(std::memory_order_acquire);
}