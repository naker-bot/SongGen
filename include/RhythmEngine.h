#pragma once

#include <vector>
#include <string>
#include <map>
#include <random>

namespace SongGen {

// Single rhythmic hit/note
struct RhythmicNote {
    float time;           // Time in beats
    float velocity;       // 0.0 to 1.0
    float duration;       // Note length in beats
    int pitch;            // MIDI note or drum sound ID
    bool isAccent;        // Accented note
    bool isGhost;         // Ghost note (soft)
    
    RhythmicNote() : time(0.0f), velocity(0.7f), duration(0.25f), 
                     pitch(60), isAccent(false), isGhost(false) {}
    RhythmicNote(float t, float v, float d, int p) 
        : time(t), velocity(v), duration(d), pitch(p), 
          isAccent(false), isGhost(false) {}
};

// Rhythm pattern for a single instrument
struct RhythmPattern {
    std::vector<RhythmicNote> notes;
    float lengthInBeats;
    std::string name;
    float swing;          // 0.0 = straight, 0.5 = triplet, 0.66 = hard swing
    bool shuffle;
    
    RhythmPattern() : lengthInBeats(4.0f), swing(0.0f), shuffle(false) {}
    
    void addNote(const RhythmicNote& note);
    void applySwing(float amount);
    void applyHumanization(float amount);
    void addGhostNotes(float probability);
};

// Complete rhythm arrangement (drums, percussion, etc.)
struct RhythmArrangement {
    RhythmPattern kick;
    RhythmPattern snare;
    RhythmPattern hihat;
    RhythmPattern percussion;
    RhythmPattern bass;       // Bass rhythm (not bass notes, just rhythm)
    
    float tempo;
    int timeSignatureNum;     // e.g., 4 in 4/4
    int timeSignatureDen;     // e.g., 4 in 4/4
    
    RhythmArrangement() : tempo(120.0f), timeSignatureNum(4), timeSignatureDen(4) {}
};

class RhythmEngine {
public:
    RhythmEngine();
    ~RhythmEngine() = default;
    
    // Generate complete rhythm arrangement by genre
    RhythmArrangement generateRhythm(const std::string& genre, float tempo, int bars = 4);
    
    // Individual drum patterns
    RhythmPattern generateKickPattern(const std::string& genre, int bars = 4);
    RhythmPattern generateSnarePattern(const std::string& genre, int bars = 4);
    RhythmPattern generateHihatPattern(const std::string& genre, int bars = 4);
    
    // Genre-specific complete patterns
    RhythmArrangement generateTrapBeat(float tempo, int bars = 4);
    RhythmArrangement generateHousebeat(float tempo, int bars = 4);
    RhythmArrangement generateRockBeat(float tempo, int bars = 4);
    RhythmArrangement generateJazzSwing(float tempo, int bars = 4);
    RhythmArrangement generateMetalDoubleKick(float tempo, int bars = 4);
    RhythmArrangement generateBreakbeat(float tempo, int bars = 4);
    
    // Rhythm manipulation
    void applyGroove(RhythmPattern& pattern, float grooveAmount);
    void humanize(RhythmPattern& pattern, float humanizeAmount);
    void quantize(RhythmPattern& pattern, float gridSize);  // Snap to grid
    
    // Polyrhythm support
    RhythmPattern createPolyrhythm(int over, int under, int bars = 4);  // e.g., 3 over 4
    
    // Velocity dynamics
    void applyAccents(RhythmPattern& pattern, const std::vector<int>& accentBeats);
    void addGhostNotes(RhythmPattern& pattern, float probability = 0.3f);
    
    // Swing and shuffle
    void applySwing(RhythmPattern& pattern, float swingAmount);
    void applyShuffle(RhythmPattern& pattern);
    
    // Fill patterns
    RhythmPattern generateFill(const std::string& genre, float beats = 2.0f);
    
    // Convert rhythm to audio events
    std::vector<RhythmicNote> getRhythmAtTime(const RhythmArrangement& arr, 
                                                float startTime, 
                                                float endTime);
    
private:
    std::mt19937 rng_;
    
    // Pattern templates
    void initializePatterns();
    std::map<std::string, std::vector<float>> kickPatterns_;
    std::map<std::string, std::vector<float>> snarePatterns_;
    std::map<std::string, std::vector<float>> hihatPatterns_;
    
    // Helper functions
    float getRandomFloat(float min, float max);
    bool randomChance(float probability);
    void addNoteWithHumanization(RhythmPattern& pattern, float time, float velocity, 
                                   int pitch, float humanize = 0.02f);
};

} // namespace SongGen
