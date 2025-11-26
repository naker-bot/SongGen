#pragma once

#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace SongGen {

// Captured rhythmic pattern from microphone
struct CapturedRhythm {
    std::vector<float> hitTimes;        // Time stamps of detected hits (in seconds)
    std::vector<float> hitVelocities;   // Relative strength of each hit (0-1)
    float totalDuration;
    float detectedTempo;
    int timeSignature;                  // Detected time signature (4, 3, etc.)
    std::string pattern;                // Pattern notation: "X..X.X.." etc.
    
    CapturedRhythm() : totalDuration(0.0f), detectedTempo(120.0f), timeSignature(4) {}
    
    // Convert to beat-relative timing
    std::vector<float> getBeats() const;
    
    // Pattern similarity (0-1, 1 = identical)
    float similarityTo(const CapturedRhythm& other) const;
};

// Captured melodic pattern from microphone
struct CapturedMelody {
    std::vector<float> noteTimes;       // When each note starts
    std::vector<float> noteDurations;   // How long each note lasts
    std::vector<float> frequencies;     // Detected frequencies (not sampled audio!)
    std::vector<int> midiNotes;         // Converted to MIDI notes
    std::vector<float> noteVelocities;  // Relative loudness
    
    float totalDuration;
    std::string scale;                  // Detected scale (Major, Minor, etc.)
    int keyCenter;                      // Root note
    
    CapturedMelody() : totalDuration(0.0f), keyCenter(0) {}
    
    // Analyze intervals between notes
    std::vector<int> getIntervals() const;
    
    // Get melodic contour (up/down/same)
    std::string getContour() const;  // "UUDSUDSS" etc. (U=up, D=down, S=same)
    
    // Pattern similarity
    float similarityTo(const CapturedMelody& other) const;
};

// Analysis result of captured pattern
struct PatternAnalysis {
    // Rhythm analysis
    float groove;              // How "groovy" is the pattern (0-1)
    float syncopation;         // Amount of syncopation (0-1)
    float complexity;          // Pattern complexity (0-1)
    float humanFeel;           // How "human" vs rigid (0-1)
    
    // Melody analysis
    float melodicInterest;     // How interesting the melody is (0-1)
    float tension;             // Harmonic tension (0-1)
    float motionQuality;       // Smooth vs jumpy (0-1)
    
    // Overall
    std::string description;   // Human-readable analysis
    float creativity;          // Overall creativity score (0-1)
    
    PatternAnalysis() : groove(0.5f), syncopation(0.5f), complexity(0.5f),
                        humanFeel(0.5f), melodicInterest(0.5f), tension(0.5f),
                        motionQuality(0.5f), creativity(0.5f) {}
};

// Pattern library entry
struct LearnedPattern {
    std::string id;
    std::string name;
    std::string type;          // "rhythm" or "melody"
    
    CapturedRhythm rhythm;
    CapturedMelody melody;
    PatternAnalysis analysis;
    
    float userRating;          // User feedback: 0-1 (how good it sounds)
    int useCount;              // How often used in generation
    std::vector<std::string> tags;  // "funky", "aggressive", "smooth", etc.
    
    LearnedPattern() : userRating(0.5f), useCount(0) {}
};

class PatternCaptureEngine {
public:
    PatternCaptureEngine();
    ~PatternCaptureEngine();
    
    // Start/stop capturing from microphone
    bool startCapture(const std::string& mode);  // "rhythm" or "melody"
    void stopCapture();
    bool isCapturing() const { return capturing_; }
    
    // Get captured patterns
    CapturedRhythm getCapturedRhythm() const { return capturedRhythm_; }
    CapturedMelody getCapturedMelody() const { return capturedMelody_; }
    
    // Analyze captured pattern
    PatternAnalysis analyzeRhythm(const CapturedRhythm& rhythm);
    PatternAnalysis analyzeMelody(const CapturedMelody& melody);
    
    // Learn from pattern (add to library)
    std::string learnPattern(const std::string& name, 
                            const CapturedRhythm* rhythm,
                            const CapturedMelody* melody,
                            float userRating = 0.7f);
    
    // Find similar patterns in library
    std::vector<LearnedPattern> findSimilarPatterns(const CapturedRhythm& rhythm, int maxResults = 5);
    std::vector<LearnedPattern> findSimilarPatterns(const CapturedMelody& melody, int maxResults = 5);
    
    // Get all learned patterns
    std::vector<LearnedPattern> getAllPatterns() const { return patternLibrary_; }
    LearnedPattern getPattern(const std::string& id) const;
    
    // Pattern library management
    bool saveLibrary(const std::string& path);
    bool loadLibrary(const std::string& path);
    void clearLibrary();
    
    // Real-time feedback during capture
    void setProgressCallback(std::function<void(float, const std::string&)> callback) {
        progressCallback_ = callback;
    }
    
    // Get current audio level (for visual feedback)
    float getCurrentLevel() const { return currentLevel_; }
    
    // Metronome for rhythm capture
    void enableMetronome(bool enable, float bpm = 120.0f);
    
    // Real-time analysis (public for audio callback)
    void processAudioFrame(const float* samples, int numSamples);
    
private:
    bool capturing_;
    std::string captureMode_;  // "rhythm" or "melody"
    
    CapturedRhythm capturedRhythm_;
    CapturedMelody capturedMelody_;
    
    std::vector<LearnedPattern> patternLibrary_;
    
    // Audio capture
    void* audioDevice_;  // Platform-specific audio device
    float sampleRate_;
    std::vector<float> audioBuffer_;
    float currentLevel_;
    
    // Metronome
    bool metronomeEnabled_;
    float metronomeBPM_;
    float metronomePhase_;
    
    // Callbacks
    std::function<void(float, const std::string&)> progressCallback_;
    
    // Audio processing thread
    void audioThreadFunc();
    bool audioThreadRunning_;
    
    // Onset detection (for rhythm)
    bool detectOnset(const float* samples, int numSamples);
    float calculateOnsetStrength(const float* samples, int numSamples);
    
    // Pitch detection (for melody)
    float detectPitch(const float* samples, int numSamples);
    int frequencyToMIDI(float frequency);
    
    // Pattern analysis helpers
    float analyzeGroove(const CapturedRhythm& rhythm);
    float analyzeSyncopation(const CapturedRhythm& rhythm);
    float analyzeHumanFeel(const CapturedRhythm& rhythm);
    float analyzeMelodicInterest(const CapturedMelody& melody);
    float analyzeHarmonicTension(const CapturedMelody& melody);
    std::string analyzeScale(const CapturedMelody& melody);
    
    // Tempo detection
    float detectTempo(const std::vector<float>& hitTimes);
    
    // Previous frame data for onset detection
    std::vector<float> prevSpectrum_;
    float prevEnergy_;
};

// Creative Pattern Analogies Engine
class PatternAnalogyEngine {
public:
    PatternAnalogyEngine();
    ~PatternAnalogyEngine() = default;
    
    // Find creative analogies between patterns
    struct Analogy {
        std::string description;
        LearnedPattern sourcePattern;
        LearnedPattern targetPattern;
        float analogyStrength;    // How strong the analogy is (0-1)
        std::string relationship;  // "variation", "inversion", "augmentation", etc.
    };
    
    // Discover analogies in pattern library
    std::vector<Analogy> findAnalogies(const std::vector<LearnedPattern>& patterns);
    
    // Apply analogy to create new pattern
    CapturedRhythm applyRhythmAnalogy(const CapturedRhythm& source, const std::string& transformation);
    CapturedMelody applyMelodyAnalogy(const CapturedMelody& source, const std::string& transformation);
    
    // Transformations
    CapturedRhythm invertRhythm(const CapturedRhythm& rhythm);        // Swap strong/weak beats
    CapturedRhythm augmentRhythm(const CapturedRhythm& rhythm);       // Make slower
    CapturedRhythm diminishRhythm(const CapturedRhythm& rhythm);      // Make faster
    CapturedRhythm reverseRhythm(const CapturedRhythm& rhythm);       // Reverse timing
    
    CapturedMelody invertMelody(const CapturedMelody& melody);        // Invert intervals
    CapturedMelody retrograde(const CapturedMelody& melody);          // Reverse melody
    CapturedMelody transpose(const CapturedMelody& melody, int semitones);
    CapturedMelody expandIntervals(const CapturedMelody& melody, float factor);
    
private:
    float calculatePatternSimilarity(const LearnedPattern& a, const LearnedPattern& b);
    std::string identifyRelationship(const LearnedPattern& a, const LearnedPattern& b);
};

} // namespace SongGen
