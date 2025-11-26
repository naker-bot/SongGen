#include "../include/RhythmEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace SongGen {

// RhythmicNote is defined in header

// RhythmPattern methods
void RhythmPattern::addNote(const RhythmicNote& note) {
    notes.push_back(note);
    lengthInBeats = std::max(lengthInBeats, note.time + note.duration);
}

void RhythmPattern::applySwing(float amount) {
    swing = amount;
    for (auto& note : notes) {
        // Apply swing to off-beat notes
        float beatPos = fmod(note.time, 1.0f);
        if (beatPos > 0.4f && beatPos < 0.6f) {  // Around 8th note offbeat
            note.time += amount * 0.1f;  // Delay offbeat notes
        }
    }
}

void RhythmPattern::applyHumanization(float amount) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> timeDist(-amount * 0.02f, amount * 0.02f);
    std::uniform_real_distribution<float> velDist(-amount * 0.1f, amount * 0.1f);
    
    for (auto& note : notes) {
        note.time += timeDist(gen);
        note.velocity = std::clamp(note.velocity + velDist(gen), 0.0f, 1.0f);
    }
}

void RhythmPattern::addGhostNotes(float probability) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    
    std::vector<RhythmicNote> ghostNotes;
    
    for (size_t i = 0; i < notes.size() - 1; ++i) {
        if (prob(gen) < probability) {
            float midTime = (notes[i].time + notes[i + 1].time) / 2.0f;
            RhythmicNote ghost = notes[i];
            ghost.time = midTime;
            ghost.velocity *= 0.3f;  // Very quiet
            ghost.isGhost = true;
            ghostNotes.push_back(ghost);
        }
    }
    
    notes.insert(notes.end(), ghostNotes.begin(), ghostNotes.end());
    std::sort(notes.begin(), notes.end(), 
              [](const RhythmicNote& a, const RhythmicNote& b) { return a.time < b.time; });
}

// RhythmEngine implementation
RhythmEngine::RhythmEngine() {
    std::random_device rd;
    rng_ = std::mt19937(rd());
    initializePatterns();
}

void RhythmEngine::initializePatterns() {
    // Kick patterns (1 = hit, 0 = rest) for 16th notes
    kickPatterns_["FourOnFloor"] = {1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0};  // EDM/House
    kickPatterns_["Trap"] = {1,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0};         // Trap
    kickPatterns_["Rock"] = {1,0,0,0, 0,0,1,0, 0,0,0,0, 0,0,1,0};         // Rock
    kickPatterns_["DnB"] = {1,0,0,1, 0,0,0,0, 1,0,0,0, 0,0,0,0};          // Drum & Bass
    
    // Snare patterns
    snarePatterns_["Basic"] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};       // Backbeat
    snarePatterns_["Trap"] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1};        // Trap rolls
    snarePatterns_["Rock"] = {0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0};        // Rock backbeat
    
    // Hihat patterns
    hihatPatterns_["Closed8th"] = {1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};   // 8th notes
    hihatPatterns_["Closed16th"] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};  // 16th notes
    hihatPatterns_["OpenClosed"] = {1,0,0,0, 0,1,0,0, 1,0,0,0, 0,1,0,0};  // Open/closed pattern
}

RhythmArrangement RhythmEngine::generateRhythm(const std::string& genre, float tempo, int bars) {
    if (genre == "Trap" || genre == "Hip-Hop") {
        return generateTrapBeat(tempo, bars);
    } else if (genre == "House" || genre == "Techno" || genre == "EDM") {
        return generateHousebeat(tempo, bars);
    } else if (genre == "Rock") {
        return generateRockBeat(tempo, bars);
    } else if (genre == "Jazz") {
        return generateJazzSwing(tempo, bars);
    } else if (genre == "Metal") {
        return generateMetalDoubleKick(tempo, bars);
    } else if (genre == "Breakbeat" || genre == "DnB") {
        return generateBreakbeat(tempo, bars);
    }
    
    // Default: simple 4/4 beat
    return generateRockBeat(tempo, bars);
}

RhythmPattern RhythmEngine::generateKickPattern(const std::string& genre, int bars) {
    RhythmPattern pattern;
    pattern.name = genre + " Kick";
    pattern.lengthInBeats = bars * 4.0f;
    
    std::vector<float> basePattern;
    if (kickPatterns_.find(genre) != kickPatterns_.end()) {
        basePattern = kickPatterns_[genre];
    } else {
        basePattern = kickPatterns_["Rock"];  // Default
    }
    
    // Repeat pattern for all bars
    for (int bar = 0; bar < bars; ++bar) {
        for (size_t i = 0; i < basePattern.size(); ++i) {
            if (basePattern[i] > 0.5f) {
                float time = bar * 4.0f + (i * 0.25f);  // 16th note grid
                pattern.addNote(RhythmicNote(time, 0.9f, 0.1f, 36));  // MIDI note 36 = kick
            }
        }
    }
    
    return pattern;
}

RhythmPattern RhythmEngine::generateSnarePattern(const std::string& genre, int bars) {
    RhythmPattern pattern;
    pattern.name = genre + " Snare";
    pattern.lengthInBeats = bars * 4.0f;
    
    std::vector<float> basePattern;
    if (snarePatterns_.find(genre) != snarePatterns_.end()) {
        basePattern = snarePatterns_[genre];
    } else {
        basePattern = snarePatterns_["Basic"];
    }
    
    for (int bar = 0; bar < bars; ++bar) {
        for (size_t i = 0; i < basePattern.size(); ++i) {
            if (basePattern[i] > 0.5f) {
                float time = bar * 4.0f + (i * 0.25f);
                pattern.addNote(RhythmicNote(time, 0.85f, 0.1f, 38));  // MIDI note 38 = snare
            }
        }
    }
    
    return pattern;
}

RhythmPattern RhythmEngine::generateHihatPattern(const std::string& genre, int bars) {
    RhythmPattern pattern;
    pattern.name = genre + " Hihat";
    pattern.lengthInBeats = bars * 4.0f;
    
    std::vector<float> basePattern;
    if (genre == "Trap" || genre == "Hip-Hop") {
        basePattern = hihatPatterns_["Closed16th"];
    } else if (genre == "House" || genre == "Techno") {
        basePattern = hihatPatterns_["Closed16th"];
    } else {
        basePattern = hihatPatterns_["Closed8th"];
    }
    
    for (int bar = 0; bar < bars; ++bar) {
        for (size_t i = 0; i < basePattern.size(); ++i) {
            if (basePattern[i] > 0.5f) {
                float time = bar * 4.0f + (i * 0.25f);
                float velocity = 0.6f + getRandomFloat(-0.1f, 0.1f);  // Vary velocity
                pattern.addNote(RhythmicNote(time, velocity, 0.05f, 42));  // Closed hihat
            }
        }
    }
    
    return pattern;
}

RhythmArrangement RhythmEngine::generateTrapBeat(float tempo, int bars) {
    RhythmArrangement arr;
    arr.tempo = tempo;
    
    // Kick: Sparse, with sub-bass hits
    arr.kick = generateKickPattern("Trap", bars);
    
    // Snare: Backbeat on 2 and 4, with rolls
    arr.snare = generateSnarePattern("Trap", bars);
    
    // Hihat: Fast 16th or 32nd note rolls
    arr.hihat = generateHihatPattern("Trap", bars);
    
    // Add rolls to last bar
    for (float t = (bars - 1) * 4.0f + 3.0f; t < bars * 4.0f; t += 0.125f) {
        arr.hihat.addNote(RhythmicNote(t, 0.7f, 0.05f, 42));
    }
    
    humanize(arr.kick, 0.3f);
    humanize(arr.snare, 0.4f);
    
    return arr;
}

RhythmArrangement RhythmEngine::generateHousebeat(float tempo, int bars) {
    RhythmArrangement arr;
    arr.tempo = tempo;
    
    // Four-on-the-floor kick
    arr.kick = generateKickPattern("FourOnFloor", bars);
    
    // Snare/clap on 2 and 4
    arr.snare = generateSnarePattern("Basic", bars);
    
    // Constant 16th note hihat
    arr.hihat = generateHihatPattern("House", bars);
    
    // Minimal humanization for electronic feel
    humanize(arr.kick, 0.1f);
    humanize(arr.snare, 0.1f);
    
    return arr;
}

RhythmArrangement RhythmEngine::generateRockBeat(float tempo, int bars) {
    RhythmArrangement arr;
    arr.tempo = tempo;
    
    arr.kick = generateKickPattern("Rock", bars);
    arr.snare = generateSnarePattern("Rock", bars);
    arr.hihat = generateHihatPattern("Rock", bars);
    
    // Add ghost notes to snare
    addGhostNotes(arr.snare, 0.2f);
    
    // Strong humanization for organic feel
    humanize(arr.kick, 0.5f);
    humanize(arr.snare, 0.6f);
    humanize(arr.hihat, 0.4f);
    
    return arr;
}

RhythmArrangement RhythmEngine::generateJazzSwing(float tempo, int bars) {
    RhythmArrangement arr;
    arr.tempo = tempo;
    
    // Jazz ride cymbal pattern
    for (int bar = 0; bar < bars; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            float time = bar * 4.0f + beat;
            arr.hihat.addNote(RhythmicNote(time, 0.7f, 0.4f, 51));  // Ride cymbal
            arr.hihat.addNote(RhythmicNote(time + 0.5f, 0.5f, 0.2f, 51));
        }
    }
    
    // Swing feel
    applySwing(arr.hihat, 0.66f);
    
    // Walking bass rhythm (quarter notes)
    for (int bar = 0; bar < bars; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            float time = bar * 4.0f + beat;
            arr.kick.addNote(RhythmicNote(time, 0.6f, 0.2f, 36));
        }
    }
    
    // Backbeat on 2 and 4
    for (int bar = 0; bar < bars; ++bar) {
        arr.snare.addNote(RhythmicNote(bar * 4.0f + 1.0f, 0.75f, 0.1f, 38));
        arr.snare.addNote(RhythmicNote(bar * 4.0f + 3.0f, 0.75f, 0.1f, 38));
    }
    
    humanize(arr.kick, 0.7f);
    humanize(arr.snare, 0.7f);
    humanize(arr.hihat, 0.6f);
    
    return arr;
}

RhythmArrangement RhythmEngine::generateMetalDoubleKick(float tempo, int bars) {
    RhythmArrangement arr;
    arr.tempo = tempo;
    
    // Fast double kick pattern
    for (int bar = 0; bar < bars; ++bar) {
        for (int i = 0; i < 16; ++i) {
            float time = bar * 4.0f + (i * 0.25f);
            if (i % 2 == 0) {  // 8th note double kick
                arr.kick.addNote(RhythmicNote(time, 0.9f, 0.05f, 36));
            }
        }
    }
    
    // Snare backbeat
    arr.snare = generateSnarePattern("Rock", bars);
    
    // Constant hihat
    arr.hihat = generateHihatPattern("Metal", bars);
    
    humanize(arr.kick, 0.2f);
    humanize(arr.snare, 0.3f);
    
    return arr;
}

RhythmArrangement RhythmEngine::generateBreakbeat(float tempo, int bars) {
    RhythmArrangement arr;
    arr.tempo = tempo;
    
    // Syncopated breakbeat pattern
    arr.kick = generateKickPattern("DnB", bars);
    
    // Complex snare pattern
    for (int bar = 0; bar < bars; ++bar) {
        arr.snare.addNote(RhythmicNote(bar * 4.0f + 1.0f, 0.9f, 0.1f, 38));
        arr.snare.addNote(RhythmicNote(bar * 4.0f + 2.75f, 0.7f, 0.1f, 38));
        arr.snare.addNote(RhythmicNote(bar * 4.0f + 3.0f, 0.85f, 0.1f, 38));
    }
    
    // Fast hihat
    arr.hihat = generateHihatPattern("DnB", bars);
    
    humanize(arr.kick, 0.4f);
    humanize(arr.snare, 0.5f);
    humanize(arr.hihat, 0.3f);
    
    return arr;
}

void RhythmEngine::applyGroove(RhythmPattern& pattern, float grooveAmount) {
    // Groove: slight timing variations that create feel
    for (size_t i = 0; i < pattern.notes.size(); ++i) {
        float beatPos = fmod(pattern.notes[i].time, 1.0f);
        
        // Push or pull notes slightly
        if (beatPos > 0.4f && beatPos < 0.6f) {
            pattern.notes[i].time += grooveAmount * 0.05f;
        } else if (beatPos > 0.9f || beatPos < 0.1f) {
            pattern.notes[i].time -= grooveAmount * 0.02f;
        }
    }
}

void RhythmEngine::humanize(RhythmPattern& pattern, float humanizeAmount) {
    pattern.applyHumanization(humanizeAmount);
}

void RhythmEngine::quantize(RhythmPattern& pattern, float gridSize) {
    for (auto& note : pattern.notes) {
        note.time = std::round(note.time / gridSize) * gridSize;
    }
}

RhythmPattern RhythmEngine::createPolyrhythm(int over, int under, int bars) {
    RhythmPattern pattern;
    pattern.name = std::to_string(over) + ":" + std::to_string(under) + " Polyrhythm";
    pattern.lengthInBeats = bars * 4.0f;
    
    float totalBeats = bars * 4.0f;
    float interval = totalBeats / over;
    
    for (int i = 0; i < over; ++i) {
        float time = i * interval;
        pattern.addNote(RhythmicNote(time, 0.8f, 0.1f, 60));
    }
    
    return pattern;
}

void RhythmEngine::applyAccents(RhythmPattern& pattern, const std::vector<int>& accentBeats) {
    for (auto& note : pattern.notes) {
        int beat = (int)note.time;
        if (std::find(accentBeats.begin(), accentBeats.end(), beat) != accentBeats.end()) {
            note.velocity = std::min(1.0f, note.velocity + 0.2f);
            note.isAccent = true;
        }
    }
}

void RhythmEngine::addGhostNotes(RhythmPattern& pattern, float probability) {
    pattern.addGhostNotes(probability);
}

void RhythmEngine::applySwing(RhythmPattern& pattern, float swingAmount) {
    pattern.applySwing(swingAmount);
}

void RhythmEngine::applyShuffle(RhythmPattern& pattern) {
    applySwing(pattern, 0.66f);  // Shuffle is strong swing
    pattern.shuffle = true;
}

RhythmPattern RhythmEngine::generateFill(const std::string& genre, float beats) {
    RhythmPattern fill;
    fill.name = genre + " Fill";
    fill.lengthInBeats = beats;
    
    // Simple tom fill going down
    int numHits = (int)(beats * 4);  // 16th notes
    for (int i = 0; i < numHits; ++i) {
        float time = i * 0.25f;
        int pitch = 47 - (i / 4);  // Descending toms (MIDI 47, 45, 43, 41)
        float velocity = 0.7f + (i * 0.05f);  // Crescendo
        fill.addNote(RhythmicNote(time, velocity, 0.15f, pitch));
    }
    
    return fill;
}

std::vector<RhythmicNote> RhythmEngine::getRhythmAtTime(const RhythmArrangement& arr,
                                                          float startTime,
                                                          float endTime) {
    std::vector<RhythmicNote> result;
    
    // Collect notes from all patterns
    auto collectNotes = [&](const RhythmPattern& pattern) {
        for (const auto& note : pattern.notes) {
            if (note.time >= startTime && note.time < endTime) {
                result.push_back(note);
            }
        }
    };
    
    collectNotes(arr.kick);
    collectNotes(arr.snare);
    collectNotes(arr.hihat);
    collectNotes(arr.percussion);
    
    // Sort by time
    std::sort(result.begin(), result.end(),
              [](const RhythmicNote& a, const RhythmicNote& b) { return a.time < b.time; });
    
    return result;
}

float RhythmEngine::getRandomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
}

bool RhythmEngine::randomChance(float probability) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng_) < probability;
}

void RhythmEngine::addNoteWithHumanization(RhythmPattern& pattern, float time, 
                                            float velocity, int pitch, float humanize) {
    float timingVariation = getRandomFloat(-humanize, humanize);
    float velocityVariation = getRandomFloat(-humanize * 5.0f, humanize * 5.0f);
    
    RhythmicNote note(time + timingVariation, 
                     std::clamp(velocity + velocityVariation, 0.0f, 1.0f),
                     0.1f, pitch);
    pattern.addNote(note);
}

} // namespace SongGen
