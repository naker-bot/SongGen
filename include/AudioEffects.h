#pragma once

#include <vector>
#include <memory>

namespace SongGen {

// Single audio effect
class AudioEffect {
public:
    virtual ~AudioEffect() = default;
    virtual void process(std::vector<float>& buffer, float sampleRate) = 0;
    virtual void reset() = 0;
    
    bool enabled = true;
    float mix = 1.0f;  // 0 = dry, 1 = wet
};

// EQ (3-band parametric)
class EQ : public AudioEffect {
public:
    EQ();
    void process(std::vector<float>& buffer, float sampleRate) override;
    void reset() override;
    
    float lowGain = 0.0f;    // dB: -12 to +12
    float midGain = 0.0f;
    float highGain = 0.0f;
    float lowFreq = 250.0f;
    float highFreq = 4000.0f;
    
private:
    // Filter states
    float lowState1 = 0.0f, lowState2 = 0.0f;
    float midState1 = 0.0f, midState2 = 0.0f;
    float highState1 = 0.0f, highState2 = 0.0f;
};

// Compressor
class Compressor : public AudioEffect {
public:
    Compressor();
    void process(std::vector<float>& buffer, float sampleRate) override;
    void reset() override;
    
    float threshold = -20.0f;  // dB
    float ratio = 4.0f;         // 4:1
    float attack = 0.005f;      // seconds
    float release = 0.1f;       // seconds
    float makeupGain = 0.0f;    // dB
    
private:
    float envelope_ = 0.0f;
    float attackCoef_ = 0.0f;
    float releaseCoef_ = 0.0f;
    
    void updateCoefficients(float sampleRate);
};

// Reverb (simple algorithmic)
class Reverb : public AudioEffect {
public:
    Reverb();
    void process(std::vector<float>& buffer, float sampleRate) override;
    void reset() override;
    
    float roomSize = 0.5f;     // 0 to 1
    float damping = 0.5f;      // 0 to 1
    float width = 1.0f;        // stereo width
    
private:
    std::vector<float> combBuffer_;
    std::vector<int> combIndices_;
    std::vector<float> allpassBuffer_;
    std::vector<int> allpassIndices_;
    
    void initializeBuffers(float sampleRate);
};

// Delay/Echo
class Delay : public AudioEffect {
public:
    Delay();
    void process(std::vector<float>& buffer, float sampleRate) override;
    void reset() override;
    
    float delayTime = 0.5f;    // seconds
    float feedback = 0.3f;     // 0 to 1
    
private:
    std::vector<float> delayBuffer_;
    int writeIndex_ = 0;
    int bufferSize_ = 0;
    float currentSampleRate_ = 44100.0f;
    
    void updateBuffer(float sampleRate);
};

// Distortion/Overdrive
class Distortion : public AudioEffect {
public:
    Distortion();
    void process(std::vector<float>& buffer, float sampleRate) override;
    void reset() override;
    
    float drive = 1.0f;        // 1 to 10
    float tone = 0.5f;         // 0 to 1 (low-pass filter)
    
private:
    float filterState_ = 0.0f;
};

// Chorus
class Chorus : public AudioEffect {
public:
    Chorus();
    void process(std::vector<float>& buffer, float sampleRate) override;
    void reset() override;
    
    float rate = 1.0f;         // Hz
    float depth = 0.5f;        // 0 to 1
    
private:
    std::vector<float> delayBuffer_;
    int writeIndex_ = 0;
    float lfoPhase_ = 0.0f;
    float currentSampleRate_ = 44100.0f;
};

// Stereo panner
class StereoPanner : public AudioEffect {
public:
    StereoPanner();
    void process(std::vector<float>& buffer, float sampleRate) override;
    void reset() override;
    
    float pan = 0.0f;          // -1 (left) to +1 (right)
};

// Audio effects chain
class AudioEffectsChain {
public:
    AudioEffectsChain();
    ~AudioEffectsChain() = default;
    
    void process(std::vector<float>& buffer, float sampleRate);
    void reset();
    
    void addEffect(std::shared_ptr<AudioEffect> effect);
    void removeEffect(size_t index);
    void clearEffects();
    
    size_t getEffectCount() const { return effects_.size(); }
    std::shared_ptr<AudioEffect> getEffect(size_t index);
    
private:
    std::vector<std::shared_ptr<AudioEffect>> effects_;
};

// Mix & Master Engine
class MixMasterEngine {
public:
    MixMasterEngine();
    ~MixMasterEngine() = default;
    
    // Mix multiple tracks with individual levels and panning
    std::vector<float> mixTracks(const std::vector<std::vector<float>>& tracks,
                                   const std::vector<float>& levels,
                                   const std::vector<float>& panning);
    
    // Apply mastering chain
    void master(std::vector<float>& audio, float sampleRate);
    
    // Individual processing
    void applyEQ(std::vector<float>& audio, float sampleRate, 
                 float lowGain, float midGain, float highGain);
    void applyCompression(std::vector<float>& audio, float sampleRate,
                         float threshold, float ratio);
    void applyReverb(std::vector<float>& audio, float sampleRate, 
                     float roomSize, float mix);
    void applyLimiter(std::vector<float>& audio, float threshold);
    
    // Normalization
    void normalize(std::vector<float>& audio, float targetLevel = 0.9f);
    
    // Stereo widening
    void stereoWiden(std::vector<float>& audio, float amount);
    
    // Get/set mastering parameters
    void setMasterEQ(float low, float mid, float high);
    void setMasterCompression(float threshold, float ratio);
    void setMasterReverb(float roomSize, float mix);
    void setLimiterThreshold(float threshold);
    
private:
    std::shared_ptr<EQ> masterEQ_;
    std::shared_ptr<Compressor> masterComp_;
    std::shared_ptr<Reverb> masterReverb_;
    
    float limiterThreshold_;
    float reverbMix_;
};

} // namespace SongGen
