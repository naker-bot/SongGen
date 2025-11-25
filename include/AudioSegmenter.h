#ifndef AUDIOSEGMENTER_H
#define AUDIOSEGMENTER_H

#include <vector>
#include <string>

/**
 * AudioSegmenter - Erkennt Songstruktur (Intro, Verse, Chorus, Bridge, Solo, Outro)
 * 
 * Analysiert:
 * - Energie-Levels und Änderungen
 * - Spektrale Features
 * - Wiederholungsmuster
 * - Melodie-Konturen
 */

struct SongSegment {
    enum Type {
        INTRO,
        VERSE,
        CHORUS,
        BRIDGE,
        SOLO,
        OUTRO,
        UNKNOWN
    };
    
    Type type;
    float startTime;    // Sekunden
    float duration;     // Sekunden
    float energy;       // 0.0 - 1.0
    float complexity;   // 0.0 - 1.0
    std::string description;
};

class AudioSegmenter {
public:
    /**
     * Analysiert WAV-Datei und erkennt Songstruktur
     * @param wavPath Pfad zur WAV-Datei
     * @return Erkannte Segmente (Intro, Verse, Chorus, etc.)
     */
    static std::vector<SongSegment> analyzeStructure(const std::string& wavPath);
    
private:
    static std::vector<float> calculateEnergyEnvelope(const std::vector<float>& samples, int sampleRate);
    static std::vector<float> detectOnsets(const std::vector<float>& samples, int sampleRate);
    static std::vector<SongSegment> findRepetitions(const std::vector<float>& energy);
    static SongSegment::Type classifySegment(float energy, float complexity, float position);
};

#endif // AUDIOSEGMENTER_H
