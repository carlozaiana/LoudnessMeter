#include "EBU128LoudnessMeter.h"
#include <cmath>

EBU128LoudnessMeter::EBU128LoudnessMeter()
{
    meanSquareBlocks.fill(0.0);
    channelWeights.fill(1.0);
}

void EBU128LoudnessMeter::prepare(double sampleRate, int /*maxBlockSize*/, int channels)
{
    juce::ScopedLock sl(processLock);
    
    currentSampleRate = sampleRate;
    numChannels = std::min(channels, kMaxChannels);
    
    // Calculate filter coefficients for this sample rate
    preFilterCoeffs = calculatePreFilterCoeffs(sampleRate);
    rlbFilterCoeffs = calculateRLBCoeffs(sampleRate);
    
    // Samples per 100ms block
    samplesPerBlock = static_cast<int>(sampleRate * 0.1);
    
    // Set channel weights per ITU-R BS.1770-4
    // L, R, C = 1.0; LFE = 0.0; Ls, Rs = 1.41 (~+1.5 dB)
    channelWeights.fill(1.0);
    if (numChannels >= 4)
        channelWeights[3] = 0.0; // LFE
    if (numChannels >= 5)
        channelWeights[4] = 1.41; // Ls
    if (numChannels >= 6)
        channelWeights[5] = 1.41; // Rs
    
    reset();
}

void EBU128LoudnessMeter::reset()
{
    juce::ScopedLock sl(processLock);
    
    for (auto& state : preFilterStates)
        state = BiquadState{};
    for (auto& state : rlbFilterStates)
        state = BiquadState{};
    
    meanSquareBlocks.fill(0.0);
    currentBlockIndex = 0;
    currentBlockSum = 0.0;
    currentBlockSamples = 0;
    
    momentaryLoudness.store(-100.0f, std::memory_order_relaxed);
    shortTermLoudness.store(-100.0f, std::memory_order_relaxed);
}

EBU128LoudnessMeter::BiquadCoeffs EBU128LoudnessMeter::calculatePreFilterCoeffs(double sampleRate)
{
    // Pre-filter: High shelf at ~1500 Hz with ~4dB boost
    // Coefficients derived from ITU-R BS.1770-4
    
    const double Vh = 1.58486250978759; // 10^(4/20) = ~4dB
    const double Vb = std::sqrt(Vh);
    const double K = std::tan(juce::MathConstants<double>::pi * 1681.974450955533 / sampleRate);
    const double K2 = K * K;
    const double denominator = 1.0 + K / 0.7071752369554196 + K2;
    
    BiquadCoeffs coeffs;
    coeffs.b0 = (Vh + Vb * K / 0.7071752369554196 + K2) / denominator;
    coeffs.b1 = 2.0 * (K2 - Vh) / denominator;
    coeffs.b2 = (Vh - Vb * K / 0.7071752369554196 + K2) / denominator;
    coeffs.a1 = 2.0 * (K2 - 1.0) / denominator;
    coeffs.a2 = (1.0 - K / 0.7071752369554196 + K2) / denominator;
    
    return coeffs;
}

EBU128LoudnessMeter::BiquadCoeffs EBU128LoudnessMeter::calculateRLBCoeffs(double sampleRate)
{
    // RLB weighting: High-pass at 38.13547087602444 Hz
    // Second-order Butterworth high-pass
    
    const double f0 = 38.13547087602444;
    const double Q = 0.5003270373238773;
    const double K = std::tan(juce::MathConstants<double>::pi * f0 / sampleRate);
    const double K2 = K * K;
    const double denominator = 1.0 + K / Q + K2;
    
    BiquadCoeffs coeffs;
    coeffs.b0 = 1.0 / denominator;
    coeffs.b1 = -2.0 / denominator;
    coeffs.b2 = 1.0 / denominator;
    coeffs.a1 = 2.0 * (K2 - 1.0) / denominator;
    coeffs.a2 = (1.0 - K / Q + K2) / denominator;
    
    return coeffs;
}

float EBU128LoudnessMeter::processBiquad(float input, const BiquadCoeffs& coeffs, BiquadState& state)
{
    double output = coeffs.b0 * input + state.z1;
    state.z1 = coeffs.b1 * input - coeffs.a1 * output + state.z2;
    state.z2 = coeffs.b2 * input - coeffs.a2 * output;
    return static_cast<float>(output);
}

float EBU128LoudnessMeter::calculateLoudness(double sumMeanSquare)
{
    if (sumMeanSquare <= 0.0)
        return -100.0f;
    
    // LUFS = -0.691 + 10 * log10(sum of weighted mean squares)
    return static_cast<float>(-0.691 + 10.0 * std::log10(sumMeanSquare));
}

void EBU128LoudnessMeter::processBlock(const juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int channels = std::min(buffer.getNumChannels(), numChannels);
    
    for (int sample = 0; sample < numSamples; ++sample)
    {
        double sampleSum = 0.0;
        
        for (int ch = 0; ch < channels; ++ch)
        {
            float input = buffer.getSample(ch, sample);
            
            // Apply K-weighting filters
            float preFiltered = processBiquad(input, preFilterCoeffs, preFilterStates[ch]);
            float kWeighted = processBiquad(preFiltered, rlbFilterCoeffs, rlbFilterStates[ch]);
            
            // Accumulate weighted squared sample
            sampleSum += channelWeights[ch] * kWeighted * kWeighted;
        }
        
        currentBlockSum += sampleSum;
        currentBlockSamples++;
        
        // Check if we've completed a 100ms block
        if (currentBlockSamples >= samplesPerBlock)
        {
            // Store mean square for this block
            double meanSquare = currentBlockSum / currentBlockSamples;
            meanSquareBlocks[currentBlockIndex] = meanSquare;
            currentBlockIndex = (currentBlockIndex + 1) % kBlocksPerShortTerm;
            
            // Reset accumulator
            currentBlockSum = 0.0;
            currentBlockSamples = 0;
            
            // Calculate Momentary loudness (last 400ms = 4 blocks)
            double momentarySum = 0.0;
            for (int i = 0; i < kBlocksPerMomentary; ++i)
            {
                int idx = (currentBlockIndex - 1 - i + kBlocksPerShortTerm) % kBlocksPerShortTerm;
                momentarySum += meanSquareBlocks[idx];
            }
            momentaryLoudness.store(calculateLoudness(momentarySum / kBlocksPerMomentary), 
                                   std::memory_order_relaxed);
            
            // Calculate Short-term loudness (last 3s = 30 blocks)
            double shortTermSum = 0.0;
            for (int i = 0; i < kBlocksPerShortTerm; ++i)
            {
                shortTermSum += meanSquareBlocks[i];
            }
            shortTermLoudness.store(calculateLoudness(shortTermSum / kBlocksPerShortTerm), 
                                   std::memory_order_relaxed);
        }
    }
}