#pragma once

#include "ChordProgressionEngine.h"
#include "RhythmEngine.h"
#include <vector>
#include <string>
#include <random>

namespace SongGen {

// Bass note pattern
struct BassNote {
    int pitch;          // MIDI note
    float time;         // In beats
    float duration;     // In beats
    float velocity;     // 0 to 1
    bool isSlide;       // Slide to next note
    
    BassNote() : pitch(48), time(0.0f), duration(0.5f), velocity(0.8f), isSlide(false) {}
    BassNote(int p, float t, float d, float v = 0.8f) 
        : pitch(p), time(t), duration(d), velocity(v), isSlide(false) {}
};

// Bass line pattern
struct BassLine {
    std::vector<BassNote> notes;
    float lengthInBeats;
    std::string style;  // "root", "walking", "syncopated", "pedal"
    
    BassLine() : lengthInBeats(4.0f), style("root") {}
    
    void addNote(const BassNote& note);
    void transpose(int semitones);
    void setOctave(int octave);  // 0-8, where 4 = middle C
};

class BassLineEngine {
public:
    BassLineEngine();
    ~BassLineEngine() = default;
    
    // Generate bass line from chord progression
    BassLine generateFromChords(const ChordProgression& chords,
                                  const std::string& genre,
                                  const std::string& style = "auto");
    
    // Genre-specific bass lines
    BassLine generateRockBass(const ChordProgression& chords);
    BassLine generateJazzWalkingBass(const ChordProgression& chords);
    BassLine generateFunkBass(const ChordProgression& chords);
    BassLine generateTrapBass(const ChordProgression& chords);
    BassLine generateMetalBass(const ChordProgression& chords);
    BassLine generateEDMBass(const ChordProgression& chords);
    
    // Bass patterns
    BassLine rootNotesPattern(const ChordProgression& chords);        // Simple roots
    BassLine walkingBassPattern(const ChordProgression& chords);      // Walking bass
    BassLine syncopatedPattern(const ChordProgression& chords);       // Syncopated
    BassLine pedalTonePattern(const ChordProgression& chords, int pedalNote);  // Pedal point
    BassLine arpeggiatedPattern(const ChordProgression& chords);      // Arpeggios
    BassLine octavePattern(const ChordProgression& chords);           // Octave jumps
    
    // Add rhythm to bass line
    void applyRhythm(BassLine& bassLine, const RhythmPattern& rhythm);
    
    // Bass articulation
    void addSlides(BassLine& bassLine, float probability = 0.2f);
    void addGhostNotes(BassLine& bassLine, float probability = 0.15f);
    void humanize(BassLine& bassLine, float amount = 0.3f);
    
    // Octave utilities
    void constrainToRange(BassLine& bassLine, int minNote = 28, int maxNote = 67);  // E1 to G4
    
private:
    std::mt19937 rng_;
    
    float getRandomFloat(float min, float max);
    bool randomChance(float probability);
    
    // Helper functions
    int getChordRoot(const Chord& chord);
    int getChordFifth(const Chord& chord);
    int getChordThird(const Chord& chord);
    int getNoteInOctave(int note, int targetOctave);
    
    // Walking bass helpers
    int findStepToNextRoot(int currentNote, int targetRoot);
    std::vector<int> findScaleNotes(int root, const std::string& scaleType);
};

} // namespace SongGen
