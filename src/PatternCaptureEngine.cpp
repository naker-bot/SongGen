#include "../include/PatternCaptureEngine.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
#include <map>
#include <portaudio.h>

namespace SongGen {

// CapturedRhythm methods
std::vector<float> CapturedRhythm::getBeats() const {
    if (hitTimes.empty() || detectedTempo == 0.0f) return {};
    
    float beatDuration = 60.0f / detectedTempo;
    std::vector<float> beats;
    
    for (float time : hitTimes) {
        beats.push_back(time / beatDuration);
    }
    
    return beats;
}

float CapturedRhythm::similarityTo(const CapturedRhythm& other) const {
    if (hitTimes.empty() || other.hitTimes.empty()) return 0.0f;
    
    // Compare tempo
    float tempoSim = 1.0f - std::abs(detectedTempo - other.detectedTempo) / 180.0f;
    tempoSim = std::clamp(tempoSim, 0.0f, 1.0f);
    
    // Compare pattern structure
    auto beats1 = getBeats();
    auto beats2 = other.getBeats();
    
    float structureSim = 0.0f;
    if (!beats1.empty() && !beats2.empty()) {
        // Calculate cross-correlation of quantized beats
        int matches = 0;
        int total = std::max(beats1.size(), beats2.size());
        
        for (size_t i = 0; i < std::min(beats1.size(), beats2.size()); ++i) {
            float diff = std::abs(std::fmod(beats1[i], 4.0f) - std::fmod(beats2[i], 4.0f));
            if (diff < 0.25f) matches++;
        }
        
        structureSim = (float)matches / total;
    }
    
    return (tempoSim * 0.3f + structureSim * 0.7f);
}

// CapturedMelody methods
std::vector<int> CapturedMelody::getIntervals() const {
    std::vector<int> intervals;
    for (size_t i = 1; i < midiNotes.size(); ++i) {
        intervals.push_back(midiNotes[i] - midiNotes[i-1]);
    }
    return intervals;
}

std::string CapturedMelody::getContour() const {
    std::string contour;
    for (size_t i = 1; i < midiNotes.size(); ++i) {
        int diff = midiNotes[i] - midiNotes[i-1];
        if (diff > 0) contour += 'U';       // Up
        else if (diff < 0) contour += 'D';  // Down
        else contour += 'S';                 // Same
    }
    return contour;
}

float CapturedMelody::similarityTo(const CapturedMelody& other) const {
    if (midiNotes.empty() || other.midiNotes.empty()) return 0.0f;
    
    // Compare intervals (more important than absolute pitches)
    auto intervals1 = getIntervals();
    auto intervals2 = other.getIntervals();
    
    if (intervals1.empty() || intervals2.empty()) return 0.0f;
    
    int matches = 0;
    int total = std::min(intervals1.size(), intervals2.size());
    
    for (int i = 0; i < total; ++i) {
        if (std::abs(intervals1[i] - intervals2[i]) <= 1) {  // Allow 1 semitone difference
            matches++;
        }
    }
    
    float intervalSim = (float)matches / total;
    
    // Compare contours
    std::string contour1 = getContour();
    std::string contour2 = other.getContour();
    
    int contourMatches = 0;
    int contourTotal = std::min(contour1.length(), contour2.length());
    
    for (size_t i = 0; i < contourTotal; ++i) {
        if (contour1[i] == contour2[i]) contourMatches++;
    }
    
    float contourSim = contourTotal > 0 ? (float)contourMatches / contourTotal : 0.0f;
    
    return (intervalSim * 0.6f + contourSim * 0.4f);
}

// PatternCaptureEngine implementation
PatternCaptureEngine::PatternCaptureEngine() 
    : capturing_(false), audioDevice_(nullptr), sampleRate_(44100.0f),
      currentLevel_(0.0f), metronomeEnabled_(false), metronomeBPM_(120.0f),
      metronomePhase_(0.0f), audioThreadRunning_(false), prevEnergy_(0.0f) {
    
    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    }
}

PatternCaptureEngine::~PatternCaptureEngine() {
    stopCapture();
    Pa_Terminate();
}

// Audio callback for PortAudio
static int audioCallback(const void* inputBuffer, void* outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo* timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void* userData) {
    
    PatternCaptureEngine* engine = (PatternCaptureEngine*)userData;
    const float* in = (const float*)inputBuffer;
    
    if (in && framesPerBuffer > 0) {
        engine->processAudioFrame(in, framesPerBuffer);
    }
    
    return paContinue;
}

bool PatternCaptureEngine::startCapture(const std::string& mode) {
    if (capturing_) return false;
    
    captureMode_ = mode;
    capturedRhythm_ = CapturedRhythm();
    capturedMelody_ = CapturedMelody();
    audioBuffer_.clear();
    
    // Open audio stream
    PaStreamParameters inputParams;
    inputParams.device = Pa_GetDefaultInputDevice();
    if (inputParams.device == paNoDevice) {
        std::cerr << "No default input device" << std::endl;
        return false;
    }
    
    inputParams.channelCount = 1;  // Mono
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = Pa_GetDeviceInfo(inputParams.device)->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;
    
    PaError err = Pa_OpenStream(
        (PaStream**)&audioDevice_,
        &inputParams,
        nullptr,  // No output
        sampleRate_,
        256,      // Frames per buffer
        paClipOff,
        audioCallback,
        this
    );
    
    if (err != paNoError) {
        std::cerr << "Cannot open audio stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    
    err = Pa_StartStream((PaStream*)audioDevice_);
    if (err != paNoError) {
        Pa_CloseStream((PaStream*)audioDevice_);
        audioDevice_ = nullptr;
        return false;
    }
    
    capturing_ = true;
    audioThreadRunning_ = true;
    
    if (progressCallback_) {
        progressCallback_(0.0f, "Capturing " + mode + "...");
    }
    
    return true;
}

void PatternCaptureEngine::stopCapture() {
    if (!capturing_) return;
    
    capturing_ = false;
    audioThreadRunning_ = false;
    
    if (audioDevice_) {
        Pa_StopStream((PaStream*)audioDevice_);
        Pa_CloseStream((PaStream*)audioDevice_);
        audioDevice_ = nullptr;
    }
    
    // Finalize captured data
    if (captureMode_ == "rhythm" && !capturedRhythm_.hitTimes.empty()) {
        capturedRhythm_.totalDuration = capturedRhythm_.hitTimes.back();
        capturedRhythm_.detectedTempo = detectTempo(capturedRhythm_.hitTimes);
    } else if (captureMode_ == "melody" && !capturedMelody_.noteTimes.empty()) {
        capturedMelody_.totalDuration = capturedMelody_.noteTimes.back();
        capturedMelody_.scale = analyzeScale(capturedMelody_);
    }
    
    if (progressCallback_) {
        progressCallback_(1.0f, "Capture complete");
    }
}

void PatternCaptureEngine::processAudioFrame(const float* samples, int numSamples) {
    if (!capturing_) return;
    
    // Calculate current audio level
    float level = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        level += std::abs(samples[i]);
    }
    currentLevel_ = level / numSamples;
    
    if (captureMode_ == "rhythm") {
        // Detect onsets (hits)
        if (detectOnset(samples, numSamples)) {
            float time = capturedRhythm_.hitTimes.empty() ? 0.0f : 
                        capturedRhythm_.hitTimes.back() + (numSamples / sampleRate_);
            
            float velocity = calculateOnsetStrength(samples, numSamples);
            
            capturedRhythm_.hitTimes.push_back(time);
            capturedRhythm_.hitVelocities.push_back(velocity);
        }
    } else if (captureMode_ == "melody") {
        // Detect pitch
        float pitch = detectPitch(samples, numSamples);
        
        if (pitch > 50.0f && pitch < 2000.0f) {  // Valid pitch range
            int midiNote = frequencyToMIDI(pitch);
            
            float time = capturedMelody_.noteTimes.empty() ? 0.0f :
                        capturedMelody_.noteTimes.back() + (numSamples / sampleRate_);
            
            // Check if this is a new note or continuation
            bool isNewNote = capturedMelody_.midiNotes.empty() ||
                            std::abs(midiNote - capturedMelody_.midiNotes.back()) > 1;
            
            if (isNewNote) {
                capturedMelody_.noteTimes.push_back(time);
                capturedMelody_.frequencies.push_back(pitch);
                capturedMelody_.midiNotes.push_back(midiNote);
                capturedMelody_.noteVelocities.push_back(currentLevel_);
                capturedMelody_.noteDurations.push_back(0.0f);
            } else if (!capturedMelody_.noteDurations.empty()) {
                // Extend duration of last note
                capturedMelody_.noteDurations.back() += numSamples / sampleRate_;
            }
        }
    }
}

bool PatternCaptureEngine::detectOnset(const float* samples, int numSamples) {
    // Simple onset detection using energy increase
    float energy = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        energy += samples[i] * samples[i];
    }
    energy /= numSamples;
    
    bool isOnset = (energy > prevEnergy_ * 1.5f) && (energy > 0.01f);
    prevEnergy_ = energy;
    
    return isOnset;
}

float PatternCaptureEngine::calculateOnsetStrength(const float* samples, int numSamples) {
    float maxAmp = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        maxAmp = std::max(maxAmp, std::abs(samples[i]));
    }
    return std::clamp(maxAmp, 0.0f, 1.0f);
}

float PatternCaptureEngine::detectPitch(const float* samples, int numSamples) {
    // Autocorrelation pitch detection
    int maxLag = (int)(sampleRate_ / 50.0f);  // Lowest freq: 50 Hz
    int minLag = (int)(sampleRate_ / 2000.0f); // Highest freq: 2000 Hz
    
    float bestCorr = 0.0f;
    int bestLag = 0;
    
    for (int lag = minLag; lag < maxLag && lag < numSamples; ++lag) {
        float corr = 0.0f;
        for (int i = 0; i < numSamples - lag; ++i) {
            corr += samples[i] * samples[i + lag];
        }
        
        if (corr > bestCorr) {
            bestCorr = corr;
            bestLag = lag;
        }
    }
    
    if (bestLag > 0) {
        return sampleRate_ / bestLag;
    }
    
    return 0.0f;
}

int PatternCaptureEngine::frequencyToMIDI(float frequency) {
    if (frequency <= 0.0f) return 0;
    return (int)(12.0f * std::log2(frequency / 440.0f) + 69);
}

PatternAnalysis PatternCaptureEngine::analyzeRhythm(const CapturedRhythm& rhythm) {
    PatternAnalysis analysis;
    
    analysis.groove = analyzeGroove(rhythm);
    analysis.syncopation = analyzeSyncopation(rhythm);
    analysis.complexity = (float)rhythm.hitTimes.size() / 32.0f;  // Normalize to 32 hits
    analysis.complexity = std::clamp(analysis.complexity, 0.0f, 1.0f);
    analysis.humanFeel = analyzeHumanFeel(rhythm);
    
    analysis.creativity = (analysis.groove * 0.3f + 
                          analysis.syncopation * 0.3f +
                          analysis.complexity * 0.2f +
                          analysis.humanFeel * 0.2f);
    
    // Generate description
    std::ostringstream desc;
    desc << "Rhythmus: ";
    if (analysis.groove > 0.7f) desc << "sehr groovy, ";
    if (analysis.syncopation > 0.6f) desc << "synkopiert, ";
    if (analysis.complexity > 0.6f) desc << "komplex, ";
    if (analysis.humanFeel > 0.7f) desc << "menschlich/organisch";
    else if (analysis.humanFeel < 0.3f) desc << "präzise/mechanisch";
    
    analysis.description = desc.str();
    
    return analysis;
}

PatternAnalysis PatternCaptureEngine::analyzeMelody(const CapturedMelody& melody) {
    PatternAnalysis analysis;
    
    analysis.melodicInterest = analyzeMelodicInterest(melody);
    analysis.tension = analyzeHarmonicTension(melody);
    
    auto intervals = melody.getIntervals();
    float avgInterval = 0.0f;
    for (int interval : intervals) {
        avgInterval += std::abs(interval);
    }
    if (!intervals.empty()) {
        avgInterval /= intervals.size();
        analysis.motionQuality = 1.0f - (avgInterval / 12.0f);  // Smoother = higher
        analysis.motionQuality = std::clamp(analysis.motionQuality, 0.0f, 1.0f);
    }
    
    analysis.creativity = (analysis.melodicInterest * 0.4f +
                          analysis.tension * 0.3f +
                          analysis.motionQuality * 0.3f);
    
    // Generate description
    std::ostringstream desc;
    desc << "Melodie: ";
    if (analysis.melodicInterest > 0.7f) desc << "interessant, ";
    if (analysis.tension > 0.6f) desc << "spannungsreich, ";
    else if (analysis.tension < 0.4f) desc << "entspannt, ";
    if (analysis.motionQuality > 0.7f) desc << "fließend";
    else if (analysis.motionQuality < 0.4f) desc << "sprungreich";
    
    analysis.description = desc.str();
    
    return analysis;
}

std::string PatternCaptureEngine::learnPattern(const std::string& name,
                                                const CapturedRhythm* rhythm,
                                                const CapturedMelody* melody,
                                                float userRating) {
    LearnedPattern pattern;
    pattern.id = std::to_string(patternLibrary_.size() + 1);
    pattern.name = name;
    pattern.userRating = userRating;
    pattern.useCount = 0;
    
    if (rhythm) {
        pattern.type = "rhythm";
        pattern.rhythm = *rhythm;
        pattern.analysis = analyzeRhythm(*rhythm);
    } else if (melody) {
        pattern.type = "melody";
        pattern.melody = *melody;
        pattern.analysis = analyzeMelody(*melody);
    }
    
    // Auto-generate tags based on analysis
    if (pattern.analysis.groove > 0.7f) pattern.tags.push_back("groovy");
    if (pattern.analysis.syncopation > 0.6f) pattern.tags.push_back("syncopated");
    if (pattern.analysis.complexity > 0.6f) pattern.tags.push_back("complex");
    if (pattern.analysis.humanFeel > 0.7f) pattern.tags.push_back("organic");
    if (pattern.analysis.tension > 0.6f) pattern.tags.push_back("tense");
    if (pattern.analysis.motionQuality > 0.7f) pattern.tags.push_back("smooth");
    
    patternLibrary_.push_back(pattern);
    
    return pattern.id;
}

std::vector<LearnedPattern> PatternCaptureEngine::findSimilarPatterns(const CapturedRhythm& rhythm, int maxResults) {
    std::vector<std::pair<float, LearnedPattern>> scored;
    
    for (const auto& pattern : patternLibrary_) {
        if (pattern.type == "rhythm") {
            float similarity = rhythm.similarityTo(pattern.rhythm);
            scored.push_back({similarity, pattern});
        }
    }
    
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    std::vector<LearnedPattern> results;
    for (size_t i = 0; i < std::min((size_t)maxResults, scored.size()); ++i) {
        results.push_back(scored[i].second);
    }
    
    return results;
}

std::vector<LearnedPattern> PatternCaptureEngine::findSimilarPatterns(const CapturedMelody& melody, int maxResults) {
    std::vector<std::pair<float, LearnedPattern>> scored;
    
    for (const auto& pattern : patternLibrary_) {
        if (pattern.type == "melody") {
            float similarity = melody.similarityTo(pattern.melody);
            scored.push_back({similarity, pattern});
        }
    }
    
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    std::vector<LearnedPattern> results;
    for (size_t i = 0; i < std::min((size_t)maxResults, scored.size()); ++i) {
        results.push_back(scored[i].second);
    }
    
    return results;
}

float PatternCaptureEngine::analyzeGroove(const CapturedRhythm& rhythm) {
    // Groove = consistent but slightly varied timing
    auto beats = rhythm.getBeats();
    if (beats.size() < 4) return 0.5f;
    
    float variance = 0.0f;
    for (size_t i = 1; i < beats.size(); ++i) {
        float interval = beats[i] - beats[i-1];
        float expected = 1.0f;  // 1 beat
        variance += std::abs(interval - expected);
    }
    variance /= beats.size();
    
    // Sweet spot: some variation but not too much
    float groove = 1.0f - std::abs(variance - 0.05f) / 0.1f;
    return std::clamp(groove, 0.0f, 1.0f);
}

float PatternCaptureEngine::analyzeSyncopation(const CapturedRhythm& rhythm) {
    auto beats = rhythm.getBeats();
    if (beats.empty()) return 0.0f;
    
    int offBeats = 0;
    for (float beat : beats) {
        float beatPos = std::fmod(beat, 1.0f);
        if (beatPos > 0.4f && beatPos < 0.6f) {  // Off-beat
            offBeats++;
        }
    }
    
    return std::clamp((float)offBeats / beats.size(), 0.0f, 1.0f);
}

float PatternCaptureEngine::analyzeHumanFeel(const CapturedRhythm& rhythm) {
    // Human feel = micro-timing variations
    auto beats = rhythm.getBeats();
    if (beats.size() < 3) return 0.5f;
    
    float totalVariation = 0.0f;
    for (size_t i = 1; i < beats.size(); ++i) {
        float interval = beats[i] - beats[i-1];
        float quantized = std::round(interval * 4.0f) / 4.0f;  // Quantize to 16th
        float deviation = std::abs(interval - quantized);
        totalVariation += deviation;
    }
    
    float avgVariation = totalVariation / beats.size();
    return std::clamp(avgVariation * 20.0f, 0.0f, 1.0f);
}

float PatternCaptureEngine::analyzeMelodicInterest(const CapturedMelody& melody) {
    if (melody.midiNotes.size() < 3) return 0.5f;
    
    // Interest = variety in intervals and direction changes
    auto intervals = melody.getIntervals();
    std::set<int> uniqueIntervals(intervals.begin(), intervals.end());
    
    float variety = (float)uniqueIntervals.size() / intervals.size();
    
    // Count direction changes
    int dirChanges = 0;
    for (size_t i = 1; i < intervals.size(); ++i) {
        if ((intervals[i] > 0) != (intervals[i-1] > 0)) {
            dirChanges++;
        }
    }
    
    float contourInterest = (float)dirChanges / intervals.size();
    
    return (variety * 0.5f + contourInterest * 0.5f);
}

float PatternCaptureEngine::analyzeHarmonicTension(const CapturedMelody& melody) {
    if (melody.midiNotes.empty()) return 0.5f;
    
    // Count dissonant intervals (tritones, minor 2nds, major 7ths)
    auto intervals = melody.getIntervals();
    int dissonances = 0;
    
    for (int interval : intervals) {
        int absInt = std::abs(interval) % 12;
        if (absInt == 1 || absInt == 6 || absInt == 11) {
            dissonances++;
        }
    }
    
    return std::clamp((float)dissonances / intervals.size(), 0.0f, 1.0f);
}

std::string PatternCaptureEngine::analyzeScale(const CapturedMelody& melody) {
    if (melody.midiNotes.size() < 3) return "Unknown";
    
    // Count note classes
    std::map<int, int> noteCount;
    for (int note : melody.midiNotes) {
        noteCount[note % 12]++;
    }
    
    // Find most common note (likely tonic)
    int tonic = 0;
    int maxCount = 0;
    for (const auto& pair : noteCount) {
        if (pair.second > maxCount) {
            maxCount = pair.second;
            tonic = pair.first;
        }
    }
    
    // Check for major vs minor
    bool hasMinorThird = noteCount.find((tonic + 3) % 12) != noteCount.end();
    bool hasMajorThird = noteCount.find((tonic + 4) % 12) != noteCount.end();
    
    if (hasMinorThird && !hasMajorThird) return "Minor";
    if (hasMajorThird && !hasMinorThird) return "Major";
    
    return "Modal";
}

float PatternCaptureEngine::detectTempo(const std::vector<float>& hitTimes) {
    if (hitTimes.size() < 4) return 120.0f;
    
    // Calculate inter-onset intervals
    std::vector<float> intervals;
    for (size_t i = 1; i < hitTimes.size(); ++i) {
        intervals.push_back(hitTimes[i] - hitTimes[i-1]);
    }
    
    // Find median interval
    std::sort(intervals.begin(), intervals.end());
    float medianInterval = intervals[intervals.size() / 2];
    
    // Convert to BPM
    if (medianInterval > 0.0f) {
        return 60.0f / medianInterval;
    }
    
    return 120.0f;
}

void PatternCaptureEngine::enableMetronome(bool enable, float bpm) {
    metronomeEnabled_ = enable;
    metronomeBPM_ = bpm;
    metronomePhase_ = 0.0f;
}

LearnedPattern PatternCaptureEngine::getPattern(const std::string& id) const {
    for (const auto& pattern : patternLibrary_) {
        if (pattern.id == id) return pattern;
    }
    return LearnedPattern();
}

bool PatternCaptureEngine::saveLibrary(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    file << patternLibrary_.size() << "\n";
    
    for (const auto& pattern : patternLibrary_) {
        file << pattern.id << "\n";
        file << pattern.name << "\n";
        file << pattern.type << "\n";
        file << pattern.userRating << "\n";
        file << pattern.useCount << "\n";
        
        // Save rhythm or melody data...
        // (Simplified for brevity)
    }
    
    file.close();
    return true;
}

bool PatternCaptureEngine::loadLibrary(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    patternLibrary_.clear();
    
    size_t count;
    file >> count;
    
    // Load patterns...
    // (Simplified for brevity)
    
    file.close();
    return true;
}

void PatternCaptureEngine::clearLibrary() {
    patternLibrary_.clear();
}

// PatternAnalogyEngine implementation
PatternAnalogyEngine::PatternAnalogyEngine() {}

std::vector<PatternAnalogyEngine::Analogy> PatternAnalogyEngine::findAnalogies(
    const std::vector<LearnedPattern>& patterns) {
    
    std::vector<Analogy> analogies;
    
    for (size_t i = 0; i < patterns.size(); ++i) {
        for (size_t j = i + 1; j < patterns.size(); ++j) {
            float similarity = calculatePatternSimilarity(patterns[i], patterns[j]);
            
            if (similarity > 0.5f && similarity < 0.95f) {  // Similar but not identical
                Analogy analogy;
                analogy.sourcePattern = patterns[i];
                analogy.targetPattern = patterns[j];
                analogy.analogyStrength = similarity;
                analogy.relationship = identifyRelationship(patterns[i], patterns[j]);
                analogy.description = "Pattern '" + patterns[i].name + "' is a " +
                                     analogy.relationship + " of '" + patterns[j].name + "'";
                
                analogies.push_back(analogy);
            }
        }
    }
    
    return analogies;
}

CapturedRhythm PatternAnalogyEngine::invertRhythm(const CapturedRhythm& rhythm) {
    CapturedRhythm inverted = rhythm;
    
    // Invert velocities (strong become weak, weak become strong)
    for (auto& vel : inverted.hitVelocities) {
        vel = 1.0f - vel;
    }
    
    return inverted;
}

CapturedRhythm PatternAnalogyEngine::augmentRhythm(const CapturedRhythm& rhythm) {
    CapturedRhythm augmented = rhythm;
    
    // Double all time values (make slower)
    for (auto& time : augmented.hitTimes) {
        time *= 2.0f;
    }
    augmented.totalDuration *= 2.0f;
    augmented.detectedTempo /= 2.0f;
    
    return augmented;
}

CapturedRhythm PatternAnalogyEngine::diminishRhythm(const CapturedRhythm& rhythm) {
    CapturedRhythm diminished = rhythm;
    
    // Halve all time values (make faster)
    for (auto& time : diminished.hitTimes) {
        time /= 2.0f;
    }
    diminished.totalDuration /= 2.0f;
    diminished.detectedTempo *= 2.0f;
    
    return diminished;
}

CapturedRhythm PatternAnalogyEngine::reverseRhythm(const CapturedRhythm& rhythm) {
    CapturedRhythm reversed = rhythm;
    
    // Reverse timing
    std::reverse(reversed.hitTimes.begin(), reversed.hitTimes.end());
    std::reverse(reversed.hitVelocities.begin(), reversed.hitVelocities.end());
    
    // Recalculate times from end
    float duration = rhythm.totalDuration;
    for (auto& time : reversed.hitTimes) {
        time = duration - time;
    }
    
    return reversed;
}

CapturedMelody PatternAnalogyEngine::invertMelody(const CapturedMelody& melody) {
    CapturedMelody inverted = melody;
    
    if (inverted.midiNotes.empty()) return inverted;
    
    // Invert around first note
    int axis = inverted.midiNotes[0];
    for (auto& note : inverted.midiNotes) {
        note = axis - (note - axis);
    }
    
    return inverted;
}

CapturedMelody PatternAnalogyEngine::retrograde(const CapturedMelody& melody) {
    CapturedMelody retro = melody;
    
    std::reverse(retro.midiNotes.begin(), retro.midiNotes.end());
    std::reverse(retro.frequencies.begin(), retro.frequencies.end());
    std::reverse(retro.noteVelocities.begin(), retro.noteVelocities.end());
    
    return retro;
}

CapturedMelody PatternAnalogyEngine::transpose(const CapturedMelody& melody, int semitones) {
    CapturedMelody transposed = melody;
    
    for (auto& note : transposed.midiNotes) {
        note += semitones;
    }
    
    for (auto& freq : transposed.frequencies) {
        freq *= std::pow(2.0f, semitones / 12.0f);
    }
    
    return transposed;
}

CapturedMelody PatternAnalogyEngine::expandIntervals(const CapturedMelody& melody, float factor) {
    CapturedMelody expanded = melody;
    
    if (expanded.midiNotes.size() < 2) return expanded;
    
    int reference = expanded.midiNotes[0];
    for (size_t i = 1; i < expanded.midiNotes.size(); ++i) {
        int interval = expanded.midiNotes[i] - reference;
        expanded.midiNotes[i] = reference + (int)(interval * factor);
    }
    
    return expanded;
}

float PatternAnalogyEngine::calculatePatternSimilarity(const LearnedPattern& a, const LearnedPattern& b) {
    if (a.type != b.type) return 0.0f;
    
    if (a.type == "rhythm") {
        return a.rhythm.similarityTo(b.rhythm);
    } else if (a.type == "melody") {
        return a.melody.similarityTo(b.melody);
    }
    
    return 0.0f;
}

std::string PatternAnalogyEngine::identifyRelationship(const LearnedPattern& a, const LearnedPattern& b) {
    // Analyze what kind of transformation connects the patterns
    
    if (a.type == "rhythm") {
        float tempoRatio = a.rhythm.detectedTempo / b.rhythm.detectedTempo;
        if (tempoRatio > 1.8f && tempoRatio < 2.2f) return "augmentation";
        if (tempoRatio > 0.4f && tempoRatio < 0.6f) return "diminution";
    } else if (a.type == "melody") {
        auto contourA = a.melody.getContour();
        auto contourB = b.melody.getContour();
        
        // Check if contours are inverted
        bool inverted = true;
        for (size_t i = 0; i < std::min(contourA.length(), contourB.length()); ++i) {
            if (contourA[i] == 'U' && contourB[i] != 'D') inverted = false;
            if (contourA[i] == 'D' && contourB[i] != 'U') inverted = false;
        }
        
        if (inverted) return "inversion";
        
        // Check for retrograde
        std::string revA(contourA.rbegin(), contourA.rend());
        if (revA == contourB) return "retrograde";
    }
    
    return "variation";
}

} // namespace SongGen
