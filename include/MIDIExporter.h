#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace SongGen {

// MIDI note event
struct MIDINote {
    int track;
    float time;         // In beats
    float duration;     // In beats
    int pitch;          // MIDI note number 0-127
    int velocity;       // 0-127
    int channel;        // 0-15
    
    MIDINote() : track(0), time(0.0f), duration(1.0f), pitch(60), velocity(64), channel(0) {}
    MIDINote(int t, float tm, float dur, int p, int vel = 64, int ch = 0)
        : track(t), time(tm), duration(dur), pitch(p), velocity(vel), channel(ch) {}
};

// MIDI track
struct MIDITrack {
    std::string name;
    std::vector<MIDINote> notes;
    int channel;
    int program;  // Instrument (0-127)
    
    MIDITrack() : channel(0), program(0) {}
    MIDITrack(const std::string& n, int ch = 0, int prog = 0)
        : name(n), channel(ch), program(prog) {}
    
    void addNote(const MIDINote& note);
    void addNote(float time, float duration, int pitch, int velocity = 64);
};

// Complete MIDI file
class MIDIFile {
public:
    MIDIFile();
    ~MIDIFile() = default;
    
    void setTempo(float bpm);
    void setTimeSignature(int numerator, int denominator);
    void addTrack(const MIDITrack& track);
    
    MIDITrack& getTrack(size_t index);
    size_t getTrackCount() const { return tracks_.size(); }
    
    bool save(const std::string& filename);
    
private:
    float tempo_;  // BPM
    int timeSignatureNum_;
    int timeSignatureDen_;
    std::vector<MIDITrack> tracks_;
    
    // MIDI file writing helpers
    void writeHeader(std::vector<uint8_t>& data);
    void writeTrack(std::vector<uint8_t>& data, const MIDITrack& track);
    void writeVariableLength(std::vector<uint8_t>& data, uint32_t value);
    uint32_t beatsToTicks(float beats, int ticksPerBeat = 480);
};

// MIDI Exporter - converts generated music to MIDI
class MIDIExporter {
public:
    MIDIExporter();
    ~MIDIExporter() = default;
    
    // Export song to MIDI file
    bool exportToMIDI(const std::string& filename,
                       const std::vector<MIDITrack>& tracks,
                       float tempo = 120.0f);
    
    // Create MIDI track from note data
    static MIDITrack createTrackFromNotes(const std::string& name,
                                            const std::vector<MIDINote>& notes,
                                            int channel = 0,
                                            int program = 0);
    
    // Preset instruments (General MIDI)
    static int getGMProgram(const std::string& instrument);
    
private:
    MIDIFile midiFile_;
};

} // namespace SongGen
