#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>

namespace SongGen {

// Chord types
enum class ChordType {
    MAJOR,
    MINOR,
    DIMINISHED,
    AUGMENTED,
    MAJOR7,
    MINOR7,
    DOMINANT7,
    DIMINISHED7,
    SUS2,
    SUS4,
    ADD9,
    POWER  // Power chord (root + fifth only)
};

// Chord quality for progression analysis
enum class ChordQuality {
    TONIC,          // I - Home, stable
    SUBDOMINANT,    // IV - Mild tension, prepares dominant
    DOMINANT,       // V - Strong tension, wants to resolve
    MEDIANT,        // iii/III - Color chord
    SUBMEDIANT,     // vi/VI - Relative minor/major
    SUPERTONIC,     // ii/II - Pre-dominant
    LEADING_TONE    // vii° - Strong tension
};

// Single chord definition
struct Chord {
    int root;                    // MIDI note number (0-11 for C-B)
    ChordType type;
    ChordQuality quality;
    std::vector<int> notes;      // MIDI note numbers
    std::string name;            // e.g., "Cmaj7", "Am", "G7"
    
    Chord() : root(0), type(ChordType::MAJOR), quality(ChordQuality::TONIC) {}
    Chord(int r, ChordType t, ChordQuality q = ChordQuality::TONIC);
    
    // Generate chord notes in given octave
    void generateNotes(int octave = 4);
    
    // Voice leading: smooth transition to next chord
    std::vector<int> voiceLeadTo(const Chord& next) const;
};

// Chord progression for a section
struct ChordProgression {
    std::vector<Chord> chords;
    std::vector<float> durations;  // Duration of each chord in beats
    float totalBeats;
    std::string name;              // e.g., "I-V-vi-IV", "12-bar blues"
    
    ChordProgression() : totalBeats(0.0f) {}
    
    void addChord(const Chord& chord, float duration);
    Chord getChordAtBeat(float beat) const;
    int getCurrentChordIndex(float beat) const;
};

class ChordProgressionEngine {
public:
    ChordProgressionEngine();
    ~ChordProgressionEngine() = default;
    
    // Generate progressions by genre
    ChordProgression generateProgression(const std::string& genre, int numBars = 4);
    
    // Common progressions
    ChordProgression pop4ChordProgression(int key = 0);      // I-V-vi-IV
    ChordProgression blues12Bar(int key = 0);                // 12-bar blues
    ChordProgression jazzIIVI(int key = 0);                  // ii-V-I
    ChordProgression rockProgression(int key = 0);           // I-bVII-IV
    ChordProgression trapProgression(int key = 0);           // i-VI-III-VII
    ChordProgression metalProgression(int key = 0);          // i-VI-III-VII (minor key)
    ChordProgression edmBuildUp(int key = 0);                // Tension building progression
    
    // Analyze and detect chords from frequency spectrum
    Chord detectChord(const std::vector<float>& spectrum, float sampleRate);
    
    // Music theory utilities
    static std::vector<int> getScale(int rootNote, const std::string& scaleType);
    static int getNoteInScale(int scaleIndex, int rootNote, const std::string& scaleType);
    static std::string getNoteName(int midiNote);
    static std::string getChordName(int root, ChordType type);
    
    // Tension and resolution
    float calculateTension(const Chord& chord, ChordQuality targetQuality = ChordQuality::TONIC);
    bool shouldResolve(const Chord& current, const Chord& next);
    
    // Voice leading
    std::vector<int> smoothVoiceLead(const std::vector<int>& fromNotes, const std::vector<int>& toNotes);
    
private:
    std::map<std::string, std::vector<std::vector<int>>> genreProgressions_;
    
    void initializeGenreProgressions();
    ChordProgression buildProgressionFromPattern(const std::vector<int>& pattern, 
                                                   int key, 
                                                   bool isMinor,
                                                   const std::vector<float>& durations);
    
    // Chord construction helpers
    std::vector<int> buildChordNotes(int root, ChordType type, int octave = 4);
    ChordQuality getChordQualityInKey(int degree, bool isMinor);
};

} // namespace SongGen
