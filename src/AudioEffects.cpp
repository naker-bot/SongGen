#include "../include/AudioEffects.h"
#include <cmath>
#include <algorithm>

namespace SongGen {

// Helper functions
static float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

static float linearToDb(float linear) {
    return 20.0f * std::log10(std::max(linear, 0.00001f));
}

// EQ Implementation
EQ::EQ() {
    reset();
}

void EQ::process(std::vector<float>& buffer, float sampleRate) {
    if (!enabled || buffer.empty()) return;
    
    float lowMult = dbToLinear(lowGain);
    float midMult = dbToLinear(midGain);
    float highMult = dbToLinear(highGain);
    
    // Simple shelf filters
    float lowCoef = 2.0f * M_PI * lowFreq / sampleRate;
    float highCoef = 2.0f * M_PI * highFreq / sampleRate;
    
    for (size_t i = 0; i < buffer.size(); ++i) {
        float sample = buffer[i];
        
        // Low shelf
        lowState1 += lowCoef * (sample - lowState1);
        float low = lowState1 * lowMult;
        
        // High shelf
        highState1 += highCoef * (sample - highState1);
        float high = (sample - highState1) * highMult;
        
        // Mid
        float mid = (sample - low - high) * midMult;
        
        buffer[i] = low + mid + high;
    }
}

void EQ::reset() {
    lowState1 = lowState2 = 0.0f;
    midState1 = midState2 = 0.0f;
    highState1 = highState2 = 0.0f;
}

// Compressor Implementation
Compressor::Compressor() {
    reset();
}

void Compressor::updateCoefficients(float sampleRate) {
    attackCoef_ = std::exp(-1.0f / (attack * sampleRate));
    releaseCoef_ = std::exp(-1.0f / (release * sampleRate));
}

void Compressor::process(std::vector<float>& buffer, float sampleRate) {
    if (!enabled || buffer.empty()) return;
    
    updateCoefficients(sampleRate);
    
    float thresholdLinear = dbToLinear(threshold);
    float makeup = dbToLinear(makeupGain);
    
    for (size_t i = 0; i < buffer.size(); ++i) {
        float inputLevel = std::abs(buffer[i]);
        
        // Envelope follower
        if (inputLevel > envelope_) {
            envelope_ += (1.0f - attackCoef_) * (inputLevel - envelope_);
        } else {
            envelope_ += (1.0f - releaseCoef_) * (inputLevel - envelope_);
        }
        
        // Calculate gain reduction
        float gain = 1.0f;
        if (envelope_ > thresholdLinear) {
            float excess = envelope_ / thresholdLinear;
            gain = std::pow(excess, 1.0f / ratio - 1.0f);
        }
        
        buffer[i] *= gain * makeup;
    }
}

void Compressor::reset() {
    envelope_ = 0.0f;
}

// Reverb Implementation
Reverb::Reverb() {
    combBuffer_.resize(8192, 0.0f);
    combIndices_.resize(4, 0);
    allpassBuffer_.resize(2048, 0.0f);
    allpassIndices_.resize(2, 0);
    reset();
}

void Reverb::process(std::vector<float>& buffer, float sampleRate) {
    if (!enabled || buffer.empty()) return;
    
    // Simplified Freeverb-style reverb
    const int combLengths[] = {1116, 1188, 1277, 1356};
    const int allpassLengths[] = {225, 341};
    
    for (size_t i = 0; i < buffer.size(); ++i) {
        float input = buffer[i];
        float output = 0.0f;
        
        // Comb filters
        for (int c = 0; c < 4; ++c) {
            int& idx = combIndices_[c];
            int len = combLengths[c];
            
            float combOut = combBuffer_[idx];
            output += combOut;
            
            combBuffer_[idx] = input + combOut * roomSize * damping;
            idx = (idx + 1) % len;
        }
        
        output *= 0.25f;
        
        // Allpass filters
        for (int a = 0; a < 2; ++a) {
            int& idx = allpassIndices_[a];
            int len = allpassLengths[a];
            
            float allpassOut = allpassBuffer_[idx];
            allpassBuffer_[idx] = output + allpassOut * 0.5f;
            output = allpassOut - output * 0.5f;
            
            idx = (idx + 1) % len;
        }
        
        buffer[i] = input * (1.0f - mix) + output * mix;
    }
}

void Reverb::reset() {
    std::fill(combBuffer_.begin(), combBuffer_.end(), 0.0f);
    std::fill(allpassBuffer_.begin(), allpassBuffer_.end(), 0.0f);
    std::fill(combIndices_.begin(), combIndices_.end(), 0);
    std::fill(allpassIndices_.begin(), allpassIndices_.end(), 0);
}

// Delay Implementation
Delay::Delay() {
    delayBuffer_.resize(88200, 0.0f);  // 2 seconds at 44.1kHz
    reset();
}

void Delay::updateBuffer(float sampleRate) {
    if (sampleRate != currentSampleRate_) {
        bufferSize_ = (int)(delayTime * sampleRate);
        if (bufferSize_ > (int)delayBuffer_.size()) {
            delayBuffer_.resize(bufferSize_, 0.0f);
        }
        currentSampleRate_ = sampleRate;
    }
}

void Delay::process(std::vector<float>& buffer, float sampleRate) {
    if (!enabled || buffer.empty()) return;
    
    updateBuffer(sampleRate);
    int delayLength = (int)(delayTime * sampleRate);
    
    for (size_t i = 0; i < buffer.size(); ++i) {
        int readIndex = (writeIndex_ - delayLength + bufferSize_) % bufferSize_;
        float delayed = delayBuffer_[readIndex];
        
        delayBuffer_[writeIndex_] = buffer[i] + delayed * feedback;
        buffer[i] = buffer[i] * (1.0f - mix) + delayed * mix;
        
        writeIndex_ = (writeIndex_ + 1) % bufferSize_;
    }
}

void Delay::reset() {
    std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
    writeIndex_ = 0;
}

// Distortion Implementation
Distortion::Distortion() {
    reset();
}

void Distortion::process(std::vector<float>& buffer, float sampleRate) {
    if (!enabled || buffer.empty()) return;
    
    float toneCoef = tone * 0.5f;
    
    for (size_t i = 0; i < buffer.size(); ++i) {
        // Soft clipping
        float driven = buffer[i] * drive;
        float distorted = std::tanh(driven);
        
        // Tone control (simple low-pass)
        filterState_ += toneCoef * (distorted - filterState_);
        
        buffer[i] = buffer[i] * (1.0f - mix) + filterState_ * mix;
    }
}

void Distortion::reset() {
    filterState_ = 0.0f;
}

// Chorus Implementation
Chorus::Chorus() {
    delayBuffer_.resize(8820, 0.0f);  // 200ms at 44.1kHz
    reset();
}

void Chorus::process(std::vector<float>& buffer, float sampleRate) {
    if (!enabled || buffer.empty()) return;
    
    for (size_t i = 0; i < buffer.size(); ++i) {
        // LFO
        lfoPhase_ += rate / sampleRate;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
        
        float lfo = std::sin(2.0f * M_PI * lfoPhase_);
        float modDelay = (5.0f + depth * 10.0f * lfo) * sampleRate / 1000.0f;
        
        int readIndex = (int)(writeIndex_ - modDelay);
        if (readIndex < 0) readIndex += delayBuffer_.size();
        
        float delayed = delayBuffer_[readIndex % delayBuffer_.size()];
        
        delayBuffer_[writeIndex_] = buffer[i];
        buffer[i] = buffer[i] * 0.5f + delayed * 0.5f;
        
        writeIndex_ = (writeIndex_ + 1) % delayBuffer_.size();
    }
}

void Chorus::reset() {
    std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
    writeIndex_ = 0;
    lfoPhase_ = 0.0f;
}

// StereoPanner Implementation
StereoPanner::StereoPanner() {}

void StereoPanner::process(std::vector<float>& buffer, float sampleRate) {
    if (!enabled || buffer.empty()) return;
    
    // Simple equal-power panning
    float leftGain = std::cos((pan + 1.0f) * M_PI * 0.25f);
    float rightGain = std::sin((pan + 1.0f) * M_PI * 0.25f);
    
    // Assume stereo interleaved
    for (size_t i = 0; i < buffer.size(); i += 2) {
        if (i + 1 < buffer.size()) {
            float mono = (buffer[i] + buffer[i + 1]) * 0.5f;
            buffer[i] = mono * leftGain;
            buffer[i + 1] = mono * rightGain;
        }
    }
}

void StereoPanner::reset() {}

// AudioEffectsChain Implementation
AudioEffectsChain::AudioEffectsChain() {}

void AudioEffectsChain::process(std::vector<float>& buffer, float sampleRate) {
    for (auto& effect : effects_) {
        if (effect && effect->enabled) {
            effect->process(buffer, sampleRate);
        }
    }
}

void AudioEffectsChain::reset() {
    for (auto& effect : effects_) {
        if (effect) {
            effect->reset();
        }
    }
}

void AudioEffectsChain::addEffect(std::shared_ptr<AudioEffect> effect) {
    effects_.push_back(effect);
}

void AudioEffectsChain::removeEffect(size_t index) {
    if (index < effects_.size()) {
        effects_.erase(effects_.begin() + index);
    }
}

void AudioEffectsChain::clearEffects() {
    effects_.clear();
}

std::shared_ptr<AudioEffect> AudioEffectsChain::getEffect(size_t index) {
    if (index < effects_.size()) {
        return effects_[index];
    }
    return nullptr;
}

// MixMasterEngine Implementation
MixMasterEngine::MixMasterEngine() 
    : limiterThreshold_(-0.3f), reverbMix_(0.2f) {
    
    masterEQ_ = std::make_shared<EQ>();
    masterComp_ = std::make_shared<Compressor>();
    masterReverb_ = std::make_shared<Reverb>();
    
    // Default mastering settings
    masterEQ_->lowGain = 0.0f;
    masterEQ_->midGain = 0.0f;
    masterEQ_->highGain = 1.0f;  // Slight high boost
    
    masterComp_->threshold = -10.0f;
    masterComp_->ratio = 3.0f;
    masterComp_->makeupGain = 2.0f;
    
    masterReverb_->roomSize = 0.3f;
    masterReverb_->mix = reverbMix_;
}

std::vector<float> MixMasterEngine::mixTracks(const std::vector<std::vector<float>>& tracks,
                                                const std::vector<float>& levels,
                                                const std::vector<float>& panning) {
    if (tracks.empty()) return {};
    
    size_t maxLength = 0;
    for (const auto& track : tracks) {
        maxLength = std::max(maxLength, track.size());
    }
    
    std::vector<float> mixed(maxLength, 0.0f);
    
    for (size_t t = 0; t < tracks.size(); ++t) {
        float level = t < levels.size() ? levels[t] : 1.0f;
        float pan = t < panning.size() ? panning[t] : 0.0f;
        
        for (size_t i = 0; i < tracks[t].size(); ++i) {
            mixed[i] += tracks[t][i] * level;
        }
    }
    
    return mixed;
}

void MixMasterEngine::master(std::vector<float>& audio, float sampleRate) {
    if (audio.empty()) return;
    
    // Mastering chain
    masterEQ_->process(audio, sampleRate);
    masterComp_->process(audio, sampleRate);
    masterReverb_->process(audio, sampleRate);
    applyLimiter(audio, limiterThreshold_);
    normalize(audio, 0.95f);
}

void MixMasterEngine::applyEQ(std::vector<float>& audio, float sampleRate,
                                float lowGain, float midGain, float highGain) {
    EQ eq;
    eq.lowGain = lowGain;
    eq.midGain = midGain;
    eq.highGain = highGain;
    eq.process(audio, sampleRate);
}

void MixMasterEngine::applyCompression(std::vector<float>& audio, float sampleRate,
                                         float threshold, float ratio) {
    Compressor comp;
    comp.threshold = threshold;
    comp.ratio = ratio;
    comp.process(audio, sampleRate);
}

void MixMasterEngine::applyReverb(std::vector<float>& audio, float sampleRate,
                                    float roomSize, float mix) {
    Reverb reverb;
    reverb.roomSize = roomSize;
    reverb.mix = mix;
    reverb.process(audio, sampleRate);
}

void MixMasterEngine::applyLimiter(std::vector<float>& audio, float threshold) {
    for (auto& sample : audio) {
        sample = std::clamp(sample, -threshold, threshold);
    }
}

void MixMasterEngine::normalize(std::vector<float>& audio, float targetLevel) {
    float maxLevel = 0.0f;
    for (float sample : audio) {
        maxLevel = std::max(maxLevel, std::abs(sample));
    }
    
    if (maxLevel > 0.0001f) {
        float gain = targetLevel / maxLevel;
        for (auto& sample : audio) {
            sample *= gain;
        }
    }
}

void MixMasterEngine::stereoWiden(std::vector<float>& audio, float amount) {
    // Mid/Side processing for stereo widening
    for (size_t i = 0; i < audio.size(); i += 2) {
        if (i + 1 < audio.size()) {
            float mid = (audio[i] + audio[i + 1]) * 0.5f;
            float side = (audio[i] - audio[i + 1]) * 0.5f;
            
            side *= (1.0f + amount);  // Widen
            
            audio[i] = mid + side;
            audio[i + 1] = mid - side;
        }
    }
}

void MixMasterEngine::setMasterEQ(float low, float mid, float high) {
    masterEQ_->lowGain = low;
    masterEQ_->midGain = mid;
    masterEQ_->highGain = high;
}

void MixMasterEngine::setMasterCompression(float threshold, float ratio) {
    masterComp_->threshold = threshold;
    masterComp_->ratio = ratio;
}

void MixMasterEngine::setMasterReverb(float roomSize, float mix) {
    masterReverb_->roomSize = roomSize;
    masterReverb_->mix = mix;
    reverbMix_ = mix;
}

void MixMasterEngine::setLimiterThreshold(float threshold) {
    limiterThreshold_ = threshold;
}

} // namespace SongGen
