#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    // Initialize LOD levels with reduction factors
    size_t factor = 1;
    for (int i = 0; i < kLodLevels; ++i)
    {
        lodLevels[i].reductionFactor = factor;
        lodLevels[i].points.reserve(kMemoryThreshold / factor);
        lodLevels[i].minMaxPairs.reserve(kMemoryThreshold / factor);
        factor *= kLodFactor;
    }
}

LoudnessDataStore::~LoudnessDataStore()
{
    if (diskStream)
        diskStream->flush();
}

void LoudnessDataStore::prepare(double updateRateHz)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    updateRate = updateRateHz;
    reset();
}

void LoudnessDataStore::reset()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    for (auto& level : lodLevels)
    {
        level.points.clear();
        level.minMaxPairs.clear();
        level.currentMinMax = MinMaxPair{};
        level.currentCount = 0;
    }
    
    totalPoints.store(0, std::memory_order_relaxed);
    currentTimestamp.store(0.0, std::memory_order_relaxed);
    
    if (diskStream)
    {
        diskStream.reset();
        tempFile.reset();
    }
    pointsOnDisk = 0;
    usingDiskStorage = false;
}

void LoudnessDataStore::addPoint(float momentary, float shortTerm)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    size_t count = totalPoints.load(std::memory_order_relaxed);
    double timestamp = count / updateRate;
    
    LoudnessPoint point{momentary, shortTerm, timestamp};
    
    // Add to LOD level 0 (full resolution)
    if (lodLevels[0].points.size() < kMemoryThreshold || !usingDiskStorage)
    {
        lodLevels[0].points.push_back(point);
        
        // Check if we need to start disk storage
        if (lodLevels[0].points.size() >= kMemoryThreshold && !usingDiskStorage)
        {
            flushToDisk();
        }
    }
    else
    {
        // Write directly to disk
        if (diskStream)
        {
            diskStream->write(&point, sizeof(LoudnessPoint));
            pointsOnDisk++;
        }
    }
    
    // Update LOD levels
    for (int level = 1; level < kLodLevels; ++level)
    {
        auto& lod = lodLevels[level];
        
        // Update running min/max
        lod.currentMinMax.minMomentary = std::min(lod.currentMinMax.minMomentary, momentary);
        lod.currentMinMax.maxMomentary = std::max(lod.currentMinMax.maxMomentary, momentary);
        lod.currentMinMax.minShortTerm = std::min(lod.currentMinMax.minShortTerm, shortTerm);
        lod.currentMinMax.maxShortTerm = std::max(lod.currentMinMax.maxShortTerm, shortTerm);
        lod.currentCount++;
        
        // Check if we've accumulated enough for this LOD level
        if (lod.currentCount >= static_cast<int>(lod.reductionFactor))
        {
            // Store the averaged point
            LoudnessPoint avgPoint;
            avgPoint.momentary = (lod.currentMinMax.minMomentary + lod.currentMinMax.maxMomentary) * 0.5f;
            avgPoint.shortTerm = (lod.currentMinMax.minShortTerm + lod.currentMinMax.maxShortTerm) * 0.5f;
            avgPoint.timestamp = timestamp;
            lod.points.push_back(avgPoint);
            
            // Store min/max pair for envelope rendering
            lod.minMaxPairs.push_back(lod.currentMinMax);
            
            // Reset accumulator
            lod.currentMinMax = MinMaxPair{100.0f, -100.0f, 100.0f, -100.0f};
            lod.currentCount = 0;
        }
    }
    
    totalPoints.fetch_add(1, std::memory_order_relaxed);
    currentTimestamp.store(timestamp, std::memory_order_relaxed);
}

void LoudnessDataStore::flushToDisk()
{
    // Create temporary file
    tempFile = std::make_unique<juce::TemporaryFile>(".loudness_data");
    diskStream = std::make_unique<juce::FileOutputStream>(tempFile->getFile());
    
    if (diskStream->openedOk())
    {
        // Write existing points to disk
        for (const auto& point : lodLevels[0].points)
        {
            diskStream->write(&point, sizeof(LoudnessPoint));
        }
        pointsOnDisk = lodLevels[0].points.size();
        
        // Keep last chunk in memory for fast access
        size_t keepInMemory = std::min(lodLevels[0].points.size(), kMemoryThreshold / 4);
        std::vector<LoudnessPoint> recentPoints(
            lodLevels[0].points.end() - keepInMemory,
            lodLevels[0].points.end()
        );
        lodLevels[0].points = std::move(recentPoints);
        
        usingDiskStorage = true;
    }
}

int LoudnessDataStore::selectLodLevel(double timeRange, int maxPixels) const
{
    // Calculate required resolution: points per pixel
    double pointsNeeded = timeRange * updateRate;
    double pointsPerPixel = pointsNeeded / maxPixels;
    
    // Select LOD level that gives roughly 2-4 points per pixel
    // (ensures smooth rendering without aliasing)
    for (int level = kLodLevels - 1; level >= 0; --level)
    {
        double effectivePointsPerPixel = pointsPerPixel / lodLevels[level].reductionFactor;
        if (effectivePointsPerPixel >= 2.0)
            return level;
    }
    
    return 0; // Full resolution
}

LoudnessDataStore::RenderData LoudnessDataStore::getDataForTimeRange(
    double startTime, double endTime, int maxPixels) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    RenderData result;
    
    if (totalPoints.load(std::memory_order_relaxed) == 0)
        return result;
    
    double timeRange = endTime - startTime;
    result.lodLevel = selectLodLevel(timeRange, maxPixels);
    
    const auto& lod = lodLevels[result.lodLevel];
    
    // Calculate index range
    size_t startIdx = static_cast<size_t>(std::max(0.0, startTime * updateRate / lod.reductionFactor));
    size_t endIdx = static_cast<size_t>(std::ceil(endTime * updateRate / lod.reductionFactor));
    
    if (startIdx >= lod.points.size())
        return result;
    
    endIdx = std::min(endIdx, lod.points.size());
    
    // Determine if we should use min/max bands (when zoomed out significantly)
    double effectivePointsPerPixel = (endIdx - startIdx) / static_cast<double>(maxPixels);
    result.useMinMax = (effectivePointsPerPixel > 4.0) && (result.lodLevel > 0);
    
    if (result.useMinMax && !lod.minMaxPairs.empty())
    {
        // Use min/max envelope for zoomed out view
        size_t mmStartIdx = std::min(startIdx, lod.minMaxPairs.size() - 1);
        size_t mmEndIdx = std::min(endIdx, lod.minMaxPairs.size());
        
        result.minMaxBands.reserve(mmEndIdx - mmStartIdx);
        for (size_t i = mmStartIdx; i < mmEndIdx; ++i)
        {
            result.minMaxBands.push_back(lod.minMaxPairs[i]);
        }
        
        // Also add representative points for line rendering
        size_t step = std::max(size_t(1), (mmEndIdx - mmStartIdx) / maxPixels);
        result.points.reserve(maxPixels);
        for (size_t i = mmStartIdx; i < mmEndIdx; i += step)
        {
            if (i < lod.points.size())
                result.points.push_back(lod.points[i]);
        }
    }
    else
    {
        // Use direct points for zoomed in view
        result.points.reserve(endIdx - startIdx);
        for (size_t i = startIdx; i < endIdx; ++i)
        {
            result.points.push_back(lod.points[i]);
        }
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