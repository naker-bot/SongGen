#include "../include/ChordProgressionEngine.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace SongGen {

// Note names for display
static const char* NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// Chord type suffixes
static const char* CHORD_SUFFIXES[] = {
    "",      // MAJOR
    "m",     // MINOR
    "dim",   // DIMINISHED
    "aug",   // AUGMENTED
    "maj7",  // MAJOR7
    "m7",    // MINOR7
    "7",     // DOMINANT7
    "dim7",  // DIMINISHED7
    "sus2",  // SUS2
    "sus4",  // SUS4
    "add9",  // ADD9
    "5"      // POWER
};

// Chord constructor
Chord::Chord(int r, ChordType t, ChordQuality q) 
    : root(r % 12), type(t), quality(q) {
    name = ChordProgressionEngine::getChordName(root, type);
}

void Chord::generateNotes(int octave) {
    notes.clear();
    int baseNote = root + (octave * 12);
    
    switch (type) {
        case ChordType::MAJOR:
            notes = {baseNote, baseNote + 4, baseNote + 7};
            break;
        case ChordType::MINOR:
            notes = {baseNote, baseNote + 3, baseNote + 7};
            break;
        case ChordType::DIMINISHED:
            notes = {baseNote, baseNote + 3, baseNote + 6};
            break;
        case ChordType::AUGMENTED:
            notes = {baseNote, baseNote + 4, baseNote + 8};
            break;
        case ChordType::MAJOR7:
            notes = {baseNote, baseNote + 4, baseNote + 7, baseNote + 11};
            break;
        case ChordType::MINOR7:
            notes = {baseNote, baseNote + 3, baseNote + 7, baseNote + 10};
            break;
        case ChordType::DOMINANT7:
            notes = {baseNote, baseNote + 4, baseNote + 7, baseNote + 10};
            break;
        case ChordType::DIMINISHED7:
            notes = {baseNote, baseNote + 3, baseNote + 6, baseNote + 9};
            break;
        case ChordType::SUS2:
            notes = {baseNote, baseNote + 2, baseNote + 7};
            break;
        case ChordType::SUS4:
            notes = {baseNote, baseNote + 5, baseNote + 7};
            break;
        case ChordType::ADD9:
            notes = {baseNote, baseNote + 4, baseNote + 7, baseNote + 14};
            break;
        case ChordType::POWER:
            notes = {baseNote, baseNote + 7};
            break;
    }
}

std::vector<int> Chord::voiceLeadTo(const Chord& next) const {
    std::vector<int> result = next.notes;
    
    // Simple voice leading: minimize movement of each voice
    for (size_t i = 0; i < result.size() && i < notes.size(); ++i) {
        int currentNote = notes[i];
        int targetNote = result[i];
        
        // Find closest octave of target note to current note
        while (targetNote < currentNote - 6) targetNote += 12;
        while (targetNote > currentNote + 6) targetNote -= 12;
        
        result[i] = targetNote;
    }
    
    return result;
}

// ChordProgression methods
void ChordProgression::addChord(const Chord& chord, float duration) {
    chords.push_back(chord);
    durations.push_back(duration);
    totalBeats += duration;
}

Chord ChordProgression::getChordAtBeat(float beat) const {
    if (chords.empty()) {
        return Chord(0, ChordType::MAJOR);
    }
    
    int idx = getCurrentChordIndex(beat);
    return chords[idx];
}

int ChordProgression::getCurrentChordIndex(float beat) const {
    if (chords.empty()) return 0;
    
    float elapsed = 0.0f;
    for (size_t i = 0; i < durations.size(); ++i) {
        elapsed += durations[i];
        if (beat < elapsed) {
            return i;
        }
    }
    
    return chords.size() - 1;
}

// ChordProgressionEngine implementation
ChordProgressionEngine::ChordProgressionEngine() {
    initializeGenreProgressions();
}

void ChordProgressionEngine::initializeGenreProgressions() {
    // Pop: I-V-vi-IV (C-G-Am-F)
    genreProgressions_["Pop"] = {{0, 7, 9, 5}};
    
    // Rock: I-bVII-IV (C-Bb-F)
    genreProgressions_["Rock"] = {{0, 10, 5}};
    
    // Trap/Hip-Hop: i-VI-III-VII (Am-F-C-G in A minor)
    genreProgressions_["Trap"] = {{0, 8, 3, 10}};  // Relative to minor key
    
    // Metal: i-VI-III-VII power chords
    genreProgressions_["Metal"] = {{0, 8, 3, 10}};
    
    // Jazz: ii-V-I-VI (Dm7-G7-Cmaj7-Am7)
    genreProgressions_["Jazz"] = {{2, 7, 0, 9}};
    
    // Blues: I-I-I-I-IV-IV-I-I-V-IV-I-V
    genreProgressions_["Blues"] = {{0, 0, 0, 0, 5, 5, 0, 0, 7, 5, 0, 7}};
    
    // EDM: i-VII-VI-VII build-up
    genreProgressions_["EDM"] = {{0, 10, 8, 10}};
    
    // Techno: i-i-i-i (emphasis on rhythm over harmony)
    genreProgressions_["Techno"] = {{0, 0, 0, 0}};
}

ChordProgression ChordProgressionEngine::generateProgression(const std::string& genre, int numBars) {
    if (genre == "Pop") return pop4ChordProgression(0);
    if (genre == "Blues") return blues12Bar(0);
    if (genre == "Jazz") return jazzIIVI(0);
    if (genre == "Rock") return rockProgression(0);
    if (genre == "Trap" || genre == "Hip-Hop") return trapProgression(0);
    if (genre == "Metal") return metalProgression(0);
    if (genre == "EDM" || genre == "House" || genre == "Trance") return edmBuildUp(0);
    
    // Default: simple I-V-vi-IV
    return pop4ChordProgression(0);
}

ChordProgression ChordProgressionEngine::pop4ChordProgression(int key) {
    ChordProgression prog;
    prog.name = "I-V-vi-IV";
    
    Chord I(key, ChordType::MAJOR, ChordQuality::TONIC);
    I.generateNotes(4);
    
    Chord V((key + 7) % 12, ChordType::MAJOR, ChordQuality::DOMINANT);
    V.generateNotes(4);
    
    Chord vi((key + 9) % 12, ChordType::MINOR, ChordQuality::SUBMEDIANT);
    vi.generateNotes(4);
    
    Chord IV((key + 5) % 12, ChordType::MAJOR, ChordQuality::SUBDOMINANT);
    IV.generateNotes(4);
    
    prog.addChord(I, 4.0f);   // 1 bar
    prog.addChord(V, 4.0f);
    prog.addChord(vi, 4.0f);
    prog.addChord(IV, 4.0f);
    
    return prog;
}

ChordProgression ChordProgressionEngine::blues12Bar(int key) {
    ChordProgression prog;
    prog.name = "12-Bar Blues";
    
    Chord I(key, ChordType::DOMINANT7, ChordQuality::TONIC);
    I.generateNotes(3);
    
    Chord IV((key + 5) % 12, ChordType::DOMINANT7, ChordQuality::SUBDOMINANT);
    IV.generateNotes(3);
    
    Chord V((key + 7) % 12, ChordType::DOMINANT7, ChordQuality::DOMINANT);
    V.generateNotes(3);
    
    // Classic 12-bar blues pattern
    prog.addChord(I, 4.0f);   // Bar 1
    prog.addChord(I, 4.0f);   // Bar 2
    prog.addChord(I, 4.0f);   // Bar 3
    prog.addChord(I, 4.0f);   // Bar 4
    prog.addChord(IV, 4.0f);  // Bar 5
    prog.addChord(IV, 4.0f);  // Bar 6
    prog.addChord(I, 4.0f);   // Bar 7
    prog.addChord(I, 4.0f);   // Bar 8
    prog.addChord(V, 4.0f);   // Bar 9
    prog.addChord(IV, 4.0f);  // Bar 10
    prog.addChord(I, 4.0f);   // Bar 11
    prog.addChord(V, 4.0f);   // Bar 12 (turnaround)
    
    return prog;
}

ChordProgression ChordProgressionEngine::jazzIIVI(int key) {
    ChordProgression prog;
    prog.name = "ii-V-I-vi";
    
    Chord ii((key + 2) % 12, ChordType::MINOR7, ChordQuality::SUPERTONIC);
    ii.generateNotes(4);
    
    Chord V((key + 7) % 12, ChordType::DOMINANT7, ChordQuality::DOMINANT);
    V.generateNotes(4);
    
    Chord I(key, ChordType::MAJOR7, ChordQuality::TONIC);
    I.generateNotes(4);
    
    Chord vi((key + 9) % 12, ChordType::MINOR7, ChordQuality::SUBMEDIANT);
    vi.generateNotes(4);
    
    prog.addChord(ii, 4.0f);
    prog.addChord(V, 4.0f);
    prog.addChord(I, 4.0f);
    prog.addChord(vi, 4.0f);
    
    return prog;
}

ChordProgression ChordProgressionEngine::rockProgression(int key) {
    ChordProgression prog;
    prog.name = "I-bVII-IV";
    
    Chord I(key, ChordType::POWER, ChordQuality::TONIC);
    I.generateNotes(3);
    
    Chord bVII((key + 10) % 12, ChordType::POWER, ChordQuality::SUBDOMINANT);
    bVII.generateNotes(3);
    
    Chord IV((key + 5) % 12, ChordType::POWER, ChordQuality::SUBDOMINANT);
    IV.generateNotes(3);
    
    prog.addChord(I, 4.0f);
    prog.addChord(bVII, 4.0f);
    prog.addChord(IV, 4.0f);
    prog.addChord(I, 4.0f);
    
    return prog;
}

ChordProgression ChordProgressionEngine::trapProgression(int key) {
    ChordProgression prog;
    prog.name = "i-VI-III-VII (minor)";
    
    // Trap typically in minor key
    Chord i(key, ChordType::MINOR, ChordQuality::TONIC);
    i.generateNotes(4);
    
    Chord VI((key + 8) % 12, ChordType::MAJOR, ChordQuality::SUBMEDIANT);
    VI.generateNotes(4);
    
    Chord III((key + 3) % 12, ChordType::MAJOR, ChordQuality::MEDIANT);
    III.generateNotes(4);
    
    Chord VII((key + 10) % 12, ChordType::MAJOR, ChordQuality::DOMINANT);
    VII.generateNotes(4);
    
    prog.addChord(i, 4.0f);
    prog.addChord(VI, 4.0f);
    prog.addChord(III, 4.0f);
    prog.addChord(VII, 4.0f);
    
    return prog;
}

ChordProgression ChordProgressionEngine::metalProgression(int key) {
    ChordProgression prog;
    prog.name = "i-VI-III-VII (power)";
    
    // Metal uses power chords in minor tonality
    Chord i(key, ChordType::POWER, ChordQuality::TONIC);
    i.generateNotes(2);  // Lower octave for metal
    
    Chord VI((key + 8) % 12, ChordType::POWER, ChordQuality::SUBMEDIANT);
    VI.generateNotes(2);
    
    Chord III((key + 3) % 12, ChordType::POWER, ChordQuality::MEDIANT);
    III.generateNotes(2);
    
    Chord VII((key + 10) % 12, ChordType::POWER, ChordQuality::DOMINANT);
    VII.generateNotes(2);
    
    prog.addChord(i, 2.0f);   // Faster chord changes
    prog.addChord(VI, 2.0f);
    prog.addChord(III, 2.0f);
    prog.addChord(VII, 2.0f);
    prog.addChord(i, 2.0f);
    prog.addChord(VI, 2.0f);
    prog.addChord(III, 2.0f);
    prog.addChord(VII, 2.0f);
    
    return prog;
}

ChordProgression ChordProgressionEngine::edmBuildUp(int key) {
    ChordProgression prog;
    prog.name = "i-VII-VI-VII (build-up)";
    
    Chord i(key, ChordType::MINOR, ChordQuality::TONIC);
    i.generateNotes(4);
    
    Chord VII((key + 10) % 12, ChordType::MAJOR, ChordQuality::DOMINANT);
    VII.generateNotes(4);
    
    Chord VI((key + 8) % 12, ChordType::MAJOR, ChordQuality::SUBMEDIANT);
    VI.generateNotes(4);
    
    prog.addChord(i, 4.0f);
    prog.addChord(VII, 4.0f);
    prog.addChord(VI, 4.0f);
    prog.addChord(VII, 4.0f);
    
    return prog;
}

Chord ChordProgressionEngine::detectChord(const std::vector<float>& spectrum, float sampleRate) {
    // Simplified chord detection from spectrum
    // Find peaks in spectrum and match to note frequencies
    
    std::vector<int> detectedNotes;
    
    for (size_t i = 10; i < spectrum.size() && i < 4000; ++i) {
        float freq = (float)i * sampleRate / (2.0f * spectrum.size());
        
        if (spectrum[i] > 0.1f) {  // Threshold for note detection
            // Convert frequency to MIDI note
            int midiNote = (int)(12.0f * std::log2(freq / 440.0f) + 69);
            if (midiNote >= 0 && midiNote < 128) {
                detectedNotes.push_back(midiNote % 12);
            }
        }
    }
    
    if (detectedNotes.empty()) {
        return Chord(0, ChordType::MAJOR);
    }
    
    // Simple detection: assume root is lowest note
    int root = *std::min_element(detectedNotes.begin(), detectedNotes.end());
    
    // Detect chord type based on intervals
    bool hasMinorThird = std::find(detectedNotes.begin(), detectedNotes.end(), (root + 3) % 12) != detectedNotes.end();
    bool hasMajorThird = std::find(detectedNotes.begin(), detectedNotes.end(), (root + 4) % 12) != detectedNotes.end();
    bool hasFifth = std::find(detectedNotes.begin(), detectedNotes.end(), (root + 7) % 12) != detectedNotes.end();
    
    ChordType type = ChordType::MAJOR;
    if (hasMinorThird && hasFifth) {
        type = ChordType::MINOR;
    } else if (hasMajorThird && hasFifth) {
        type = ChordType::MAJOR;
    }
    
    return Chord(root, type);
}

std::vector<int> ChordProgressionEngine::getScale(int rootNote, const std::string& scaleType) {
    std::vector<int> intervals;
    
    if (scaleType == "Major") {
        intervals = {0, 2, 4, 5, 7, 9, 11};  // W-W-H-W-W-W-H
    } else if (scaleType == "Minor" || scaleType == "Natural Minor") {
        intervals = {0, 2, 3, 5, 7, 8, 10};  // W-H-W-W-H-W-W
    } else if (scaleType == "Minor Pentatonic") {
        intervals = {0, 3, 5, 7, 10};
    } else if (scaleType == "Blues") {
        intervals = {0, 3, 5, 6, 7, 10};
    } else if (scaleType == "Phrygian") {
        intervals = {0, 1, 3, 5, 7, 8, 10};
    } else {
        intervals = {0, 2, 4, 5, 7, 9, 11};  // Default to major
    }
    
    std::vector<int> scale;
    for (int interval : intervals) {
        scale.push_back((rootNote + interval) % 12);
    }
    
    return scale;
}

int ChordProgressionEngine::getNoteInScale(int scaleIndex, int rootNote, const std::string& scaleType) {
    std::vector<int> scale = getScale(rootNote, scaleType);
    int octave = scaleIndex / scale.size();
    int noteInScale = scaleIndex % scale.size();
    
    return scale[noteInScale] + (octave * 12) + 60;  // Start from middle C octave
}

std::string ChordProgressionEngine::getNoteName(int midiNote) {
    int noteClass = midiNote % 12;
    int octave = (midiNote / 12) - 1;
    
    return std::string(NOTE_NAMES[noteClass]) + std::to_string(octave);
}

std::string ChordProgressionEngine::getChordName(int root, ChordType type) {
    return std::string(NOTE_NAMES[root % 12]) + CHORD_SUFFIXES[(int)type];
}

float ChordProgressionEngine::calculateTension(const Chord& chord, ChordQuality targetQuality) {
    float tension = 0.0f;
    
    switch (chord.quality) {
        case ChordQuality::TONIC:
            tension = 0.0f;  // No tension
            break;
        case ChordQuality::SUBDOMINANT:
            tension = 0.3f;  // Mild tension
            break;
        case ChordQuality::DOMINANT:
            tension = 0.8f;  // Strong tension
            break;
        case ChordQuality::LEADING_TONE:
            tension = 0.9f;  // Very strong tension
            break;
        default:
            tension = 0.2f;
            break;
    }
    
    // Dominant7 and diminished chords add tension
    if (chord.type == ChordType::DOMINANT7 || chord.type == ChordType::DIMINISHED7) {
        tension += 0.2f;
    }
    
    return std::min(1.0f, tension);
}

bool ChordProgressionEngine::shouldResolve(const Chord& current, const Chord& next) {
    // Dominant chords should resolve to tonic
    if (current.quality == ChordQuality::DOMINANT && next.quality == ChordQuality::TONIC) {
        return true;
    }
    
    // Leading tone resolves to tonic
    if (current.quality == ChordQuality::LEADING_TONE && next.quality == ChordQuality::TONIC) {
        return true;
    }
    
    return false;
}

std::vector<int> ChordProgressionEngine::smoothVoiceLead(const std::vector<int>& fromNotes, 
                                                          const std::vector<int>& toNotes) {
    std::vector<int> result = toNotes;
    
    for (size_t i = 0; i < result.size() && i < fromNotes.size(); ++i) {
        int from = fromNotes[i];
        int to = result[i];
        
        // Move to to closest octave
        while (to < from - 6) to += 12;
        while (to > from + 6) to -= 12;
        
        result[i] = to;
    }
    
    return result;
}

ChordProgression ChordProgressionEngine::buildProgressionFromPattern(
    const std::vector<int>& pattern, 
    int key, 
    bool isMinor,
    const std::vector<float>& durations) {
    
    ChordProgression prog;
    
    for (size_t i = 0; i < pattern.size(); ++i) {
        int degree = pattern[i];
        int noteOffset = degree;
        
        // Build chord from scale degree
        std::vector<int> scale = getScale(key, isMinor ? "Minor" : "Major");
        int root = (key + scale[degree % scale.size()]) % 12;
        
        ChordType type = isMinor ? ChordType::MINOR : ChordType::MAJOR;
        ChordQuality quality = getChordQualityInKey(degree, isMinor);
        
        Chord chord(root, type, quality);
        chord.generateNotes(4);
        
        float duration = i < durations.size() ? durations[i] : 4.0f;
        prog.addChord(chord, duration);
    }
    
    return prog;
}

std::vector<int> ChordProgressionEngine::buildChordNotes(int root, ChordType type, int octave) {
    Chord chord(root, type);
    chord.generateNotes(octave);
    return chord.notes;
}

ChordQuality ChordProgressionEngine::getChordQualityInKey(int degree, bool isMinor) {
    if (isMinor) {
        switch (degree) {
            case 0: return ChordQuality::TONIC;
            case 1: return ChordQuality::SUPERTONIC;
            case 2: return ChordQuality::MEDIANT;
            case 3: return ChordQuality::SUBDOMINANT;
            case 4: return ChordQuality::DOMINANT;
            case 5: return ChordQuality::SUBMEDIANT;
            case 6: return ChordQuality::LEADING_TONE;
            default: return ChordQuality::TONIC;
        }
    } else {
        switch (degree) {
            case 0: return ChordQuality::TONIC;
            case 1: return ChordQuality::SUPERTONIC;
            case 2: return ChordQuality::MEDIANT;
            case 3: return ChordQuality::SUBDOMINANT;
            case 4: return ChordQuality::DOMINANT;
            case 5: return ChordQuality::SUBMEDIANT;
            case 6: return ChordQuality::LEADING_TONE;
            default: return ChordQuality::TONIC;
        }
    }
}

} // namespace SongGen
