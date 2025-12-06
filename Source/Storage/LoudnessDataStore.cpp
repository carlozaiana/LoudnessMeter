#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    size_t factor = 1;
    for (size_t i = 0; i < kLodLevels; ++i)
    {
        lodLevels[i].reductionFactor = factor;
        lodLevels[i].points.reserve(1000);
        lodLevels[i].minMaxPairs.reserve(1000);
        factor *= kLodFactor;
    }
}

void LoudnessDataStore::prepare(double updateRateHz)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    updateRate = updateRateHz;
}

void LoudnessDataStore::reset()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    for (auto& level : lodLevels)
    {
        level.points.clear();
        level.minMaxPairs.clear();
        level.currentMinMax = MinMaxPair{100.0f, -100.0f, 100.0f, -100.0f};
        level.currentCount = 0;
    }
    
    totalPoints.store(0, std::memory_order_relaxed);
    currentTimestamp.store(0.0, std::memory_order_relaxed);
}

void LoudnessDataStore::addPoint(float momentary, float shortTerm)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    size_t count = totalPoints.load(std::memory_order_relaxed);
    double timestamp = static_cast<double>(count) / updateRate;
    
    LoudnessPoint point{momentary, shortTerm, timestamp};
    
    // Add to LOD level 0
    lodLevels[0].points.push_back(point);
    
    // Update higher LOD levels
    for (size_t level = 1; level < kLodLevels; ++level)
    {
        auto& lod = lodLevels[level];
        
        lod.currentMinMax.minMomentary = std::min(lod.currentMinMax.minMomentary, momentary);
        lod.currentMinMax.maxMomentary = std::max(lod.currentMinMax.maxMomentary, momentary);
        lod.currentMinMax.minShortTerm = std::min(lod.currentMinMax.minShortTerm, shortTerm);
        lod.currentMinMax.maxShortTerm = std::max(lod.currentMinMax.maxShortTerm, shortTerm);
        lod.currentCount++;
        
        if (static_cast<size_t>(lod.currentCount) >= lod.reductionFactor)
        {
            LoudnessPoint avgPoint;
            avgPoint.momentary = (lod.currentMinMax.minMomentary + lod.currentMinMax.maxMomentary) * 0.5f;
            avgPoint.shortTerm = (lod.currentMinMax.minShortTerm + lod.currentMinMax.maxShortTerm) * 0.5f;
            avgPoint.timestamp = timestamp;
            lod.points.push_back(avgPoint);
            lod.minMaxPairs.push_back(lod.currentMinMax);
            
            lod.currentMinMax = MinMaxPair{100.0f, -100.0f, 100.0f, -100.0f};
            lod.currentCount = 0;
        }
    }
    
    totalPoints.fetch_add(1, std::memory_order_relaxed);
    currentTimestamp.store(timestamp, std::memory_order_relaxed);
}

int LoudnessDataStore::selectLodLevel(double timeRange, int maxPixels) const
{
    if (maxPixels <= 0) return 0;
    
    double pointsNeeded = timeRange * updateRate;
    double pointsPerPixel = pointsNeeded / static_cast<double>(maxPixels);
    
    for (int level = static_cast<int>(kLodLevels) - 1; level >= 0; --level)
    {
        double effectivePPP = pointsPerPixel / static_cast<double>(lodLevels[static_cast<size_t>(level)].reductionFactor);
        if (effectivePPP >= 2.0)
            return level;
    }
    
    return 0;
}

LoudnessDataStore::RenderData LoudnessDataStore::getDataForTimeRange(
    double startTime, double endTime, int maxPixels) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    RenderData result;
    
    if (totalPoints.load(std::memory_order_relaxed) == 0 || maxPixels <= 0)
        return result;
    
    double timeRange = endTime - startTime;
    if (timeRange <= 0)
        return result;
    
    result.lodLevel = selectLodLevel(timeRange, maxPixels);
    
    const auto& lod = lodLevels[static_cast<size_t>(result.lodLevel)];
    
    if (lod.points.empty())
        return result;
    
    double reductionFactor = static_cast<double>(lod.reductionFactor);
    
    int64_t startIdx = static_cast<int64_t>(std::max(0.0, startTime * updateRate / reductionFactor));
    int64_t endIdx = static_cast<int64_t>(std::ceil(endTime * updateRate / reductionFactor));
    
    startIdx = std::max(int64_t(0), startIdx);
    endIdx = std::min(static_cast<int64_t>(lod.points.size()), endIdx);
    
    if (startIdx >= endIdx)
        return result;
    
    size_t numPoints = static_cast<size_t>(endIdx - startIdx);
    result.points.reserve(std::min(numPoints, static_cast<size_t>(maxPixels * 2)));
    
    size_t step = std::max(size_t(1), numPoints / static_cast<size_t>(maxPixels));
    
    for (int64_t i = startIdx; i < endIdx; i += static_cast<int64_t>(step))
    {
        result.points.push_back(lod.points[static_cast<size_t>(i)]);
    }
    
    return result;
}

double LoudnessDataStore::getTotalDuration() const
{
    return currentTimestamp.load(std::memory_order_relaxed);
}

double LoudnessDataStore::getCurrentTime() const
{
    return currentTimestamp.load(std::memory_order_relaxed);
}