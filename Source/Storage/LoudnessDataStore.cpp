#include "LoudnessDataStore.h"
#include <cmath>
#include <algorithm>

LoudnessDataStore::LoudnessDataStore()
{
    rawData.reserve(kMaxRawPoints);
    
    double duration = 0.1;
    for (int i = 0; i < kNumLods; ++i)
    {
        lodLevels[static_cast<size_t>(i)].bucketDuration = duration;
        lodLevels[static_cast<size_t>(i)].data.reserve(kMaxRawPoints / static_cast<size_t>(std::pow(kLodFactor, i)));
        lodLevels[static_cast<size_t>(i)].currentBucket.reset(0, duration);
        lodLevels[static_cast<size_t>(i)].samplesInBucket = 0;
        duration *= kLodFactor;
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
        lodLevels[static_cast<size_t>(i)].data.clear();
        lodLevels[static_cast<size_t>(i)].bucketDuration = duration;
        lodLevels[static_cast<size_t>(i)].currentBucket.reset(0, duration);
        lodLevels[static_cast<size_t>(i)].samplesInBucket = 0;
        duration *= kLodFactor;
    }
    
    currentTimestamp.store(0.0, std::memory_order_release);
}

void LoudnessDataStore::addPoint(float momentary, float shortTerm)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    double timestamp = static_cast<double>(rawData.size()) * sampleInterval;
    
    LoudnessPoint point{momentary, shortTerm, timestamp};
    
    if (rawData.size() < kMaxRawPoints)
    {
        rawData.push_back(point);
    }
    else
    {
        // Circular buffer for raw data
        rawData[rawData.size() % kMaxRawPoints] = point;
    }
    
    updateLodData(point);
    
    currentTimestamp.store(timestamp, std::memory_order_release);
}

void LoudnessDataStore::updateLodData(const LoudnessPoint& point)
{
    for (int i = 0; i < kNumLods; ++i)
    {
        auto& lod = lodLevels[static_cast<size_t>(i)];
        
        // Check if point belongs to current bucket
        if (point.timestamp >= lod.currentBucket.timeEnd)
        {
            // Save current bucket if it has data
            if (lod.samplesInBucket > 0)
            {
                lod.data.push_back(lod.currentBucket);
            }
            
            // Start new bucket aligned to bucket duration
            double bucketStart = std::floor(point.timestamp / lod.bucketDuration) * lod.bucketDuration;
            lod.currentBucket.reset(bucketStart, bucketStart + lod.bucketDuration);
            lod.samplesInBucket = 0;
        }
        
        lod.currentBucket.addSample(point.momentary, point.shortTerm);
        lod.samplesInBucket++;
    }
}

double LoudnessDataStore::getCurrentTime() const
{
    return currentTimestamp.load(std::memory_order_acquire);
}

int LoudnessDataStore::selectLodLevel(double timeRange, int numBuckets) const
{
    if (numBuckets <= 0)
        return 0;
    
    double idealBucketDuration = timeRange / numBuckets;
    
    // Find the LOD level with bucket duration closest to but not exceeding ideal
    for (int i = kNumLods - 1; i >= 0; --i)
    {
        if (lodLevels[static_cast<size_t>(i)].bucketDuration <= idealBucketDuration * 2.0)
            return i;
    }
    
    return 0;
}

std::vector<LoudnessDataStore::MinMaxPoint> LoudnessDataStore::getMinMaxForRange(
    double startTime, double endTime, int numBuckets) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    std::vector<MinMaxPoint> result;
    
    if (endTime <= startTime || numBuckets <= 0)
        return result;
    
    double timeRange = endTime - startTime;
    int lodLevel = selectLodLevel(timeRange, numBuckets);
    const auto& lod = lodLevels[static_cast<size_t>(lodLevel)];
    
    // Calculate output bucket duration
    double outputBucketDuration = timeRange / numBuckets;
    
    result.reserve(static_cast<size_t>(numBuckets));
    
    // Create output buckets
    for (int i = 0; i < numBuckets; ++i)
    {
        double bucketStart = startTime + i * outputBucketDuration;
        double bucketEnd = bucketStart + outputBucketDuration;
        
        MinMaxPoint outBucket;
        outBucket.reset(bucketStart, bucketEnd);
        
        // Find all LOD buckets that overlap with this output bucket
        for (const auto& lodBucket : lod.data)
        {
            // Check for overlap
            if (lodBucket.timeEnd > bucketStart && lodBucket.timeStart < bucketEnd)
            {
                outBucket.merge(lodBucket);
            }
            
            // Early exit if past the bucket
            if (lodBucket.timeStart >= bucketEnd)
                break;
        }
        
        // Also check current bucket
        if (lod.currentBucket.timeEnd > bucketStart && 
            lod.currentBucket.timeStart < bucketEnd &&
            lod.samplesInBucket > 0)
        {
            outBucket.merge(lod.currentBucket);
        }
        
        result.push_back(outBucket);
    }
    
    return result;
}