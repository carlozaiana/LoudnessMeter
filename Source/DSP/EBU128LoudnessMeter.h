#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

/**
 * EBU R128 Loudness Meter with true K-weighting
 * 
 * Implements the two-stage K-weighting filter:
 * 1. Pre-filter (shelving): High-frequency boost to account for acoustic effects of the head
 * 2. RLB (Revised Low-frequency B-curve): High-pass to reduce low frequency content
 */
class EBU128LoudnessMeter
{
public:
    EBU128LoudnessMeter();
    ~EBU128LoudnessMeter() = default;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void processBlock(const juce::AudioBuffer<float>& buffer);

    // Thread-safe getters (called from UI thread)
    float getMomentaryLoudness() const { return momentaryLoudness.load(std::memory_order_relaxed); }
    float getShortTermLoudness() const { return shortTermLoudness.load(std::memory_order_relaxed); }

private:
    // K-weighting filter coefficients
    struct BiquadCoeffs
    {
        double b0{1.0}, b1{0.0}, b2{0.0};
        double a1{0.0}, a2{0.0};
    };

    struct BiquadState
    {
        double z1{0.0}, z2{0.0};
    };

    // Calculate pre-filter coefficients (high shelf)
    BiquadCoeffs calculatePreFilterCoeffs(double sampleRate);
    
    // Calculate RLB filter coefficients (high pass)
    BiquadCoeffs calculateRLBCoeffs(double sampleRate);
    
    // Process sample through biquad filter
    float processBiquad(float input, const BiquadCoeffs& coeffs, BiquadState& state);
    
    // Calculate loudness from mean square values
    float calculateLoudness(double sumMeanSquare);

    double currentSampleRate{48000.0};
    int numChannels{2};
    
    // Filter coefficients (same for all channels)
    BiquadCoeffs preFilterCoeffs;
    BiquadCoeffs rlbFilterCoeffs;
    
    // Filter states per channel (max 8 channels)
    static constexpr int kMaxChannels = 8;
    std::array<BiquadState, kMaxChannels> preFilterStates;
    std::array<BiquadState, kMaxChannels> rlbFilterStates;
    
    // Channel weights per ITU-R BS.1770
    std::array<double, kMaxChannels> channelWeights;
    
    // Ring buffers for gated measurements
    // 400ms blocks for momentary (updated every 100ms with 75% overlap)
    // 3s for short-term (updated every 100ms)
    static constexpr int kBlocksPerMomentary = 4;   // 400ms = 4 x 100ms blocks
    static constexpr int kBlocksPerShortTerm = 30;  // 3s = 30 x 100ms blocks
    
    std::array<double, kBlocksPerShortTerm> meanSquareBlocks;
    int currentBlockIndex{0};
    
    // Accumulator for current 100ms block
    double currentBlockSum{0.0};
    int currentBlockSamples{0};
    int samplesPerBlock{4800}; // 100ms at 48kHz
    
    // Output values (atomic for thread safety)
    std::atomic<float> momentaryLoudness{-100.0f};
    std::atomic<float> shortTermLoudness{-100.0f};
    
    juce::CriticalSection processLock;
};