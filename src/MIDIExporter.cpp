#include "../include/MIDIExporter.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace SongGen {

// MIDITrack implementation
void MIDITrack::addNote(const MIDINote& note) {
    notes.push_back(note);
}

void MIDITrack::addNote(float time, float duration, int pitch, int velocity) {
    MIDINote note(0, time, duration, pitch, velocity, channel);
    notes.push_back(note);
}

// MIDIFile implementation
MIDIFile::MIDIFile() 
    : tempo_(120.0f), timeSignatureNum_(4), timeSignatureDen_(4) {}

void MIDIFile::setTempo(float bpm) {
    tempo_ = bpm;
}

void MIDIFile::setTimeSignature(int numerator, int denominator) {
    timeSignatureNum_ = numerator;
    timeSignatureDen_ = denominator;
}

void MIDIFile::addTrack(const MIDITrack& track) {
    tracks_.push_back(track);
}

MIDITrack& MIDIFile::getTrack(size_t index) {
    return tracks_[index];
}

void MIDIFile::writeVariableLength(std::vector<uint8_t>& data, uint32_t value) {
    uint32_t buffer = value & 0x7F;
    while ((value >>= 7)) {
        buffer <<= 8;
        buffer |= ((value & 0x7F) | 0x80);
    }
    
    while (true) {
        data.push_back(buffer & 0xFF);
        if (buffer & 0x80)
            buffer >>= 8;
        else
            break;
    }
}

uint32_t MIDIFile::beatsToTicks(float beats, int ticksPerBeat) {
    return (uint32_t)(beats * ticksPerBeat);
}

void MIDIFile::writeHeader(std::vector<uint8_t>& data) {
    // "MThd"
    data.push_back('M');
    data.push_back('T');
    data.push_back('h');
    data.push_back('d');
    
    // Header length (6 bytes)
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(6);
    
    // Format type (1 = multiple tracks, synchronous)
    data.push_back(0);
    data.push_back(1);
    
    // Number of tracks
    uint16_t numTracks = tracks_.size();
    data.push_back((numTracks >> 8) & 0xFF);
    data.push_back(numTracks & 0xFF);
    
    // Ticks per quarter note
    int ticksPerBeat = 480;
    data.push_back((ticksPerBeat >> 8) & 0xFF);
    data.push_back(ticksPerBeat & 0xFF);
}

void MIDIFile::writeTrack(std::vector<uint8_t>& data, const MIDITrack& track) {
    std::vector<uint8_t> trackData;
    
    // Track name
    if (!track.name.empty()) {
        writeVariableLength(trackData, 0);  // Delta time
        trackData.push_back(0xFF);  // Meta event
        trackData.push_back(0x03);  // Track name
        writeVariableLength(trackData, track.name.length());
        for (char c : track.name) {
            trackData.push_back(c);
        }
    }
    
    // Tempo (only in first track typically)
    uint32_t microsecondsPerBeat = 60000000 / tempo_;
    writeVariableLength(trackData, 0);
    trackData.push_back(0xFF);
    trackData.push_back(0x51);
    trackData.push_back(0x03);
    trackData.push_back((microsecondsPerBeat >> 16) & 0xFF);
    trackData.push_back((microsecondsPerBeat >> 8) & 0xFF);
    trackData.push_back(microsecondsPerBeat & 0xFF);
    
    // Time signature
    writeVariableLength(trackData, 0);
    trackData.push_back(0xFF);
    trackData.push_back(0x58);
    trackData.push_back(0x04);
    trackData.push_back(timeSignatureNum_);
    trackData.push_back(timeSignatureDen_ == 2 ? 1 : 2);  // 2^n
    trackData.push_back(24);
    trackData.push_back(8);
    
    // Program change
    writeVariableLength(trackData, 0);
    trackData.push_back(0xC0 | track.channel);  // Program change
    trackData.push_back(track.program);
    
    // Sort notes by time
    std::vector<std::pair<uint32_t, MIDINote>> events;
    for (const auto& note : track.notes) {
        uint32_t startTick = beatsToTicks(note.time);
        uint32_t endTick = beatsToTicks(note.time + note.duration);
        events.push_back({startTick, note});
        
        MIDINote offNote = note;
        offNote.time = note.time + note.duration;
        events.push_back({endTick, offNote});
    }
    
    std::sort(events.begin(), events.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    // Write note events
    uint32_t lastTick = 0;
    for (size_t i = 0; i < events.size(); i += 2) {
        if (i + 1 >= events.size()) break;
        
        const auto& noteOn = events[i];
        const auto& noteOff = events[i + 1];
        
        // Note on
        uint32_t deltaOn = noteOn.first - lastTick;
        writeVariableLength(trackData, deltaOn);
        trackData.push_back(0x90 | noteOn.second.channel);  // Note on
        trackData.push_back(noteOn.second.pitch);
        trackData.push_back(noteOn.second.velocity);
        lastTick = noteOn.first;
        
        // Note off
        uint32_t deltaOff = noteOff.first - lastTick;
        writeVariableLength(trackData, deltaOff);
        trackData.push_back(0x80 | noteOff.second.channel);  // Note off
        trackData.push_back(noteOff.second.pitch);
        trackData.push_back(0);
        lastTick = noteOff.first;
    }
    
    // End of track
    writeVariableLength(trackData, 0);
    trackData.push_back(0xFF);
    trackData.push_back(0x2F);
    trackData.push_back(0x00);
    
    // Write track header
    data.push_back('M');
    data.push_back('T');
    data.push_back('r');
    data.push_back('k');
    
    // Track length
    uint32_t trackLength = trackData.size();
    data.push_back((trackLength >> 24) & 0xFF);
    data.push_back((trackLength >> 16) & 0xFF);
    data.push_back((trackLength >> 8) & 0xFF);
    data.push_back(trackLength & 0xFF);
    
    // Track data
    data.insert(data.end(), trackData.begin(), trackData.end());
}

bool MIDIFile::save(const std::string& filename) {
    std::vector<uint8_t> data;
    
    writeHeader(data);
    
    for (const auto& track : tracks_) {
        writeTrack(data, track);
    }
    
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    
    return true;
}

// MIDIExporter implementation
MIDIExporter::MIDIExporter() {}

bool MIDIExporter::exportToMIDI(const std::string& filename,
                                  const std::vector<MIDITrack>& tracks,
                                  float tempo) {
    midiFile_ = MIDIFile();
    midiFile_.setTempo(tempo);
    
    for (const auto& track : tracks) {
        midiFile_.addTrack(track);
    }
    
    return midiFile_.save(filename);
}

MIDITrack MIDIExporter::createTrackFromNotes(const std::string& name,
                                               const std::vector<MIDINote>& notes,
                                               int channel,
                                               int program) {
    MIDITrack track(name, channel, program);
    for (const auto& note : notes) {
        track.addNote(note);
    }
    return track;
}

int MIDIExporter::getGMProgram(const std::string& instrument) {
    // General MIDI instrument mapping
    if (instrument == "Piano" || instrument == "piano") return 0;
    if (instrument == "Guitar" || instrument == "guitar") return 24;
    if (instrument == "Bass" || instrument == "bass") return 33;
    if (instrument == "Strings") return 48;
    if (instrument == "Synth Lead" || instrument == "lead") return 80;
    if (instrument == "Synth Pad" || instrument == "pad") return 88;
    if (instrument == "Drums" || instrument == "drums") return 0;  // Channel 10
    
    return 0;  // Default to piano
}

} // namespace SongGen
