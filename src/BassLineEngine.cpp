#include "../include/BassLineEngine.h"
#include <algorithm>
#include <cmath>

namespace SongGen {

void BassLine::addNote(const BassNote& note) {
    notes.push_back(note);
    lengthInBeats = std::max(lengthInBeats, note.time + note.duration);
}

void BassLine::transpose(int semitones) {
    for (auto& note : notes) {
        note.pitch += semitones;
    }
}

void BassLine::setOctave(int octave) {
    for (auto& note : notes) {
        int noteClass = note.pitch % 12;
        note.pitch = noteClass + (octave * 12) + 12;  // MIDI octave offset
    }
}

BassLineEngine::BassLineEngine() {
    std::random_device rd;
    rng_ = std::mt19937(rd());
}

BassLine BassLineEngine::generateFromChords(const ChordProgression& chords,
                                              const std::string& genre,
                                              const std::string& style) {
    std::string actualStyle = style;
    
    // Auto-select style based on genre
    if (style == "auto") {
        if (genre == "Jazz") {
            actualStyle = "walking";
        } else if (genre == "Funk") {
            actualStyle = "syncopated";
        } else if (genre == "Trap" || genre == "EDM") {
            actualStyle = "syncopated";
        } else if (genre == "Metal") {
            actualStyle = "root";
        } else {
            actualStyle = "root";
        }
    }
    
    // Generate based on style
    if (actualStyle == "walking") {
        return walkingBassPattern(chords);
    } else if (actualStyle == "syncopated") {
        return syncopatedPattern(chords);
    } else if (actualStyle == "arpeggiated") {
        return arpeggiatedPattern(chords);
    } else if (actualStyle == "octave") {
        return octavePattern(chords);
    } else {
        return rootNotesPattern(chords);
    }
}

BassLine BassLineEngine::generateRockBass(const ChordProgression& chords) {
    BassLine bass = rootNotesPattern(chords);
    bass.style = "Rock";
    
    // Add some octave jumps
    for (size_t i = 0; i < bass.notes.size(); i += 2) {
        if (i + 1 < bass.notes.size() && randomChance(0.5f)) {
            bass.notes[i + 1].pitch -= 12;  // Octave down
        }
    }
    
    humanize(bass, 0.5f);
    constrainToRange(bass);
    return bass;
}

BassLine BassLineEngine::generateJazzWalkingBass(const ChordProgression& chords) {
    return walkingBassPattern(chords);
}

BassLine BassLineEngine::generateFunkBass(const ChordProgression& chords) {
    BassLine bass = syncopatedPattern(chords);
    bass.style = "Funk";
    
    addGhostNotes(bass, 0.3f);
    addSlides(bass, 0.25f);
    humanize(bass, 0.6f);
    constrainToRange(bass);
    return bass;
}

BassLine BassLineEngine::generateTrapBass(const ChordProgression& chords) {
    BassLine bass;
    bass.style = "Trap";
    bass.lengthInBeats = chords.totalBeats;
    
    // Very sparse, sub-bass style
    for (size_t i = 0; i < chords.chords.size(); ++i) {
        int root = getChordRoot(chords.chords[i]);
        int bassPitch = getNoteInOctave(root, 1);  // Very low octave
        
        float chordStart = 0.0f;
        for (size_t j = 0; j < i; ++j) {
            chordStart += chords.durations[j];
        }
        
        // Long sustained notes
        bass.addNote(BassNote(bassPitch, chordStart, chords.durations[i] * 0.9f, 0.95f));
    }
    
    constrainToRange(bass, 24, 48);  // Very low range
    return bass;
}

BassLine BassLineEngine::generateMetalBass(const ChordProgression& chords) {
    BassLine bass;
    bass.style = "Metal";
    bass.lengthInBeats = chords.totalBeats;
    
    // Follow kick drum pattern - fast, tight notes
    for (size_t i = 0; i < chords.chords.size(); ++i) {
        int root = getChordRoot(chords.chords[i]);
        int bassPitch = getNoteInOctave(root, 2);  // Low octave
        
        float chordStart = 0.0f;
        for (size_t j = 0; j < i; ++j) {
            chordStart += chords.durations[j];
        }
        
        // Fast 8th note pattern
        float duration = chords.durations[i];
        for (float t = 0; t < duration; t += 0.5f) {
            bass.addNote(BassNote(bassPitch, chordStart + t, 0.3f, 0.9f));
        }
    }
    
    constrainToRange(bass, 28, 55);
    return bass;
}

BassLine BassLineEngine::generateEDMBass(const ChordProgression& chords) {
    BassLine bass = syncopatedPattern(chords);
    bass.style = "EDM";
    
    // Lower octave for EDM
    bass.setOctave(2);
    
    constrainToRange(bass, 28, 60);
    return bass;
}

BassLine BassLineEngine::rootNotesPattern(const ChordProgression& chords) {
    BassLine bass;
    bass.style = "root";
    bass.lengthInBeats = chords.totalBeats;
    
    for (size_t i = 0; i < chords.chords.size(); ++i) {
        int root = getChordRoot(chords.chords[i]);
        int bassPitch = getNoteInOctave(root, 3);  // Bass octave
        
        float chordStart = 0.0f;
        for (size_t j = 0; j < i; ++j) {
            chordStart += chords.durations[j];
        }
        
        // Root note on each chord change
        bass.addNote(BassNote(bassPitch, chordStart, chords.durations[i] * 0.9f, 0.85f));
    }
    
    return bass;
}

BassLine BassLineEngine::walkingBassPattern(const ChordProgression& chords) {
    BassLine bass;
    bass.style = "walking";
    bass.lengthInBeats = chords.totalBeats;
    
    int currentPitch = 48;  // C3
    
    for (size_t i = 0; i < chords.chords.size(); ++i) {
        int targetRoot = getChordRoot(chords.chords[i]);
        int targetPitch = getNoteInOctave(targetRoot, 3);
        
        float chordStart = 0.0f;
        for (size_t j = 0; j < i; ++j) {
            chordStart += chords.durations[j];
        }
        
        float duration = chords.durations[i];
        int numSteps = (int)(duration);  // One note per beat
        
        for (int step = 0; step < numSteps; ++step) {
            float time = chordStart + step;
            
            if (step == 0) {
                // Start on root
                currentPitch = targetPitch;
            } else if (step == numSteps - 1 && i + 1 < chords.chords.size()) {
                // Approach next chord's root
                int nextRoot = getChordRoot(chords.chords[i + 1]);
                int nextPitch = getNoteInOctave(nextRoot, 3);
                currentPitch = findStepToNextRoot(currentPitch, nextPitch);
            } else {
                // Walk through chord tones
                if (step == 1) {
                    currentPitch = getChordThird(chords.chords[i]);
                } else if (step == 2) {
                    currentPitch = getChordFifth(chords.chords[i]);
                } else {
                    currentPitch = targetPitch + (step % 2 ? 2 : -2);  // Neighbor tones
                }
            }
            
            bass.addNote(BassNote(currentPitch, time, 0.9f, 0.8f));
        }
    }
    
    return bass;
}

BassLine BassLineEngine::syncopatedPattern(const ChordProgression& chords) {
    BassLine bass;
    bass.style = "syncopated";
    bass.lengthInBeats = chords.totalBeats;
    
    for (size_t i = 0; i < chords.chords.size(); ++i) {
        int root = getChordRoot(chords.chords[i]);
        int bassPitch = getNoteInOctave(root, 3);
        
        float chordStart = 0.0f;
        for (size_t j = 0; j < i; ++j) {
            chordStart += chords.durations[j];
        }
        
        float duration = chords.durations[i];
        
        // Syncopated rhythm: on 1, off-beat of 2, on 3 and a half
        bass.addNote(BassNote(bassPitch, chordStart, 0.3f, 0.9f));
        bass.addNote(BassNote(bassPitch, chordStart + 1.5f, 0.3f, 0.7f));
        bass.addNote(BassNote(bassPitch, chordStart + 2.75f, 0.3f, 0.8f));
    }
    
    return bass;
}

BassLine BassLineEngine::pedalTonePattern(const ChordProgression& chords, int pedalNote) {
    BassLine bass;
    bass.style = "pedal";
    bass.lengthInBeats = chords.totalBeats;
    
    int bassPitch = getNoteInOctave(pedalNote, 3);
    
    // Sustained pedal throughout
    bass.addNote(BassNote(bassPitch, 0.0f, chords.totalBeats * 0.95f, 0.85f));
    
    return bass;
}

BassLine BassLineEngine::arpeggiatedPattern(const ChordProgression& chords) {
    BassLine bass;
    bass.style = "arpeggiated";
    bass.lengthInBeats = chords.totalBeats;
    
    for (size_t i = 0; i < chords.chords.size(); ++i) {
        const Chord& chord = chords.chords[i];
        
        float chordStart = 0.0f;
        for (size_t j = 0; j < i; ++j) {
            chordStart += chords.durations[j];
        }
        
        float duration = chords.durations[i];
        int numNotes = (int)(duration * 2);  // 8th notes
        
        for (int n = 0; n < numNotes; ++n) {
            float time = chordStart + (n * 0.5f);
            
            // Cycle through chord tones
            int noteIndex = n % 3;
            int pitch = 48;  // Default
            
            if (noteIndex == 0) {
                pitch = getChordRoot(chord);
            } else if (noteIndex == 1) {
                pitch = getChordThird(chord);
            } else {
                pitch = getChordFifth(chord);
            }
            
            pitch = getNoteInOctave(pitch, 3);
            bass.addNote(BassNote(pitch, time, 0.4f, 0.75f));
        }
    }
    
    return bass;
}

BassLine BassLineEngine::octavePattern(const ChordProgression& chords) {
    BassLine bass;
    bass.style = "octave";
    bass.lengthInBeats = chords.totalBeats;
    
    for (size_t i = 0; i < chords.chords.size(); ++i) {
        int root = getChordRoot(chords.chords[i]);
        
        float chordStart = 0.0f;
        for (size_t j = 0; j < i; ++j) {
            chordStart += chords.durations[j];
        }
        
        float duration = chords.durations[i];
        int bassPitch = getNoteInOctave(root, 3);
        
        // Root and octave alternating
        for (float t = 0; t < duration; t += 1.0f) {
            int pitch = (((int)t) % 2 == 0) ? bassPitch : (bassPitch - 12);
            bass.addNote(BassNote(pitch, chordStart + t, 0.8f, 0.85f));
        }
    }
    
    return bass;
}

void BassLineEngine::applyRhythm(BassLine& bassLine, const RhythmPattern& rhythm) {
    // Match bass notes to rhythm pattern timing
    if (rhythm.notes.empty()) return;
    
    BassLine newBass;
    newBass.style = bassLine.style;
    newBass.lengthInBeats = bassLine.lengthInBeats;
    
    for (const auto& rhythmNote : rhythm.notes) {
        // Find bass pitch at this time
        int pitch = 48;  // Default
        for (const auto& bassNote : bassLine.notes) {
            if (rhythmNote.time >= bassNote.time && 
                rhythmNote.time < bassNote.time + bassNote.duration) {
                pitch = bassNote.pitch;
                break;
            }
        }
        
        newBass.addNote(BassNote(pitch, rhythmNote.time, rhythmNote.duration, rhythmNote.velocity));
    }
    
    bassLine = newBass;
}

void BassLineEngine::addSlides(BassLine& bassLine, float probability) {
    for (size_t i = 0; i < bassLine.notes.size() - 1; ++i) {
        if (randomChance(probability)) {
            bassLine.notes[i].isSlide = true;
        }
    }
}

void BassLineEngine::addGhostNotes(BassLine& bassLine, float probability) {
    std::vector<BassNote> ghostNotes;
    
    for (size_t i = 0; i < bassLine.notes.size() - 1; ++i) {
        if (randomChance(probability)) {
            float midTime = (bassLine.notes[i].time + bassLine.notes[i + 1].time) / 2.0f;
            BassNote ghost = bassLine.notes[i];
            ghost.time = midTime;
            ghost.duration = 0.1f;
            ghost.velocity *= 0.3f;
            ghostNotes.push_back(ghost);
        }
    }
    
    bassLine.notes.insert(bassLine.notes.end(), ghostNotes.begin(), ghostNotes.end());
    std::sort(bassLine.notes.begin(), bassLine.notes.end(),
              [](const BassNote& a, const BassNote& b) { return a.time < b.time; });
}

void BassLineEngine::humanize(BassLine& bassLine, float amount) {
    std::uniform_real_distribution<float> timeDist(-amount * 0.02f, amount * 0.02f);
    std::uniform_real_distribution<float> velDist(-amount * 0.1f, amount * 0.1f);
    
    for (auto& note : bassLine.notes) {
        note.time += timeDist(rng_);
        note.velocity = std::clamp(note.velocity + velDist(rng_), 0.0f, 1.0f);
    }
}

void BassLineEngine::constrainToRange(BassLine& bassLine, int minNote, int maxNote) {
    for (auto& note : bassLine.notes) {
        while (note.pitch < minNote) note.pitch += 12;
        while (note.pitch > maxNote) note.pitch -= 12;
    }
}

float BassLineEngine::getRandomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
}

bool BassLineEngine::randomChance(float probability) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng_) < probability;
}

int BassLineEngine::getChordRoot(const Chord& chord) {
    return chord.root;
}

int BassLineEngine::getChordFifth(const Chord& chord) {
    return (chord.root + 7) % 12;
}

int BassLineEngine::getChordThird(const Chord& chord) {
    if (chord.type == ChordType::MINOR || chord.type == ChordType::MINOR7) {
        return (chord.root + 3) % 12;  // Minor third
    }
    return (chord.root + 4) % 12;  // Major third
}

int BassLineEngine::getNoteInOctave(int note, int targetOctave) {
    int noteClass = note % 12;
    return noteClass + (targetOctave * 12) + 12;  // MIDI octave offset
}

int BassLineEngine::findStepToNextRoot(int currentNote, int targetRoot) {
    int currentClass = currentNote % 12;
    int targetClass = targetRoot % 12;
    
    int distance = targetClass - currentClass;
    if (distance > 6) distance -= 12;
    if (distance < -6) distance += 12;
    
    // Move one step toward target
    if (distance > 0) {
        return currentNote + 1;
    } else if (distance < 0) {
        return currentNote - 1;
    }
    
    return currentNote;
}

std::vector<int> BassLineEngine::findScaleNotes(int root, const std::string& scaleType) {
    return ChordProgressionEngine::getScale(root, scaleType);
}

} // namespace SongGen
